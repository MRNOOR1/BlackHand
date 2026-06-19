#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <stdint.h>
#include <time.h>
#include <fcntl.h>
#include <termios.h>
#include <pthread.h>
#include <errno.h>
#include "serial_utility.h"

pthread_mutex_t response_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  response_cond  = PTHREAD_COND_INITIALIZER;
char            response_buffer[BUFFERSIZE];
int             response_ready = 0;

/* Serialises concurrent at_cmd() callers (bringup thread + dispatch thread). */
static pthread_mutex_t at_cmd_mutex = PTHREAD_MUTEX_INITIALIZER;

// We try this list of candidates at boot. Order matters — SIM7600's AT port
// is ttyUSB2 in a stock layout; the symlink is a hopeful fallback in case
// udev gets started someday. We probe each by sending "AT\r" and accepting
// the first one that replies "OK" (or "ERROR" — anything AT-shaped proves
// we're talking to the right interface).
static const char *PORT_CANDIDATES[] = {
	"/dev/blackhand-modem-at",
	"/dev/ttyUSB3",   /* this unit's AT port lives here */
	"/dev/ttyUSB2",   /* stock SIM7600 layout */
	"/dev/ttyUSB4",
	"/dev/ttyUSB1",
	"/dev/ttyUSB0",
	"/dev/ttyUSB5",
	NULL,
};

static char active_port[64] = "";

// When set (via the UI port picker → set_port IPC), the sweep is bypassed and
// ONLY this port is probed. Lets the user pin /dev/ttyUSB0..4 one at a time to
// find the AT port empirically before we bake in a permanent fix.
static char forced_port[64] = "";

const char *modem_active_port(void)
{
	return active_port[0] ? active_port : "/dev/blackhand-modem-at";
}

void serial_set_forced_port(const char *path)
{
	if (!path || !path[0] || strcmp(path, "auto") == 0) {
		forced_port[0] = '\0';
		return;
	}
	snprintf(forced_port, sizeof(forced_port), "%s", path);
}

const char *serial_get_forced_port(void)
{
	return forced_port;
}

static int apply_termios(int fd)
{
	struct termios tty;
	if (tcgetattr(fd, &tty) != 0) return -1;

	cfsetispeed(&tty, B115200);
	cfsetospeed(&tty, B115200);
	tty.c_cflag = CS8 | CLOCAL | CREAD;
	tty.c_cc[VMIN]  = 0;
	tty.c_cc[VTIME] = 1;
	tty.c_iflag = 0;
	tty.c_oflag = 0;
	tty.c_lflag = 0;

	if (tcsetattr(fd, TCSANOW, &tty) != 0) return -1;
	tcflush(fd, TCIOFLUSH);
	return 0;
}

// Sends "AT\r" and watches for "OK"/"ERROR" within timeout_ms. Returns 1 if
// the port talks AT, 0 otherwise. Reads byte-by-byte through a small buffer
// so we can detect the response even when it arrives in fragments.
static int probe_at(int fd, int timeout_ms)
{
	tcflush(fd, TCIOFLUSH);
	if (write(fd, "AT\r", 3) != 3) return 0;

	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	long deadline_ms = ts.tv_sec * 1000 + ts.tv_nsec / 1000000 + timeout_ms;

	char buf[128] = {0};
	int  pos = 0;

	while (pos < (int)sizeof(buf) - 1) {
		clock_gettime(CLOCK_MONOTONIC, &ts);
		long now_ms = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
		if (now_ms >= deadline_ms) break;

		char c;
		int r = read(fd, &c, 1);
		if (r > 0) {
			buf[pos++] = c;
			buf[pos]   = '\0';
			// Anything AT-shaped proves we found the right interface.
			// "ERROR" can come from CDC-ECM/PPP interfaces that still echo
			// — that's fine too, we won't open those long-term anyway.
			if (strstr(buf, "OK") || strstr(buf, "ERROR")) return 1;
		}
	}
	return 0;
}

static int try_open_candidate(const char *path)
{
	if (access(path, F_OK) != 0) return -1;

	int fd = open(path, O_RDWR | O_NOCTTY | O_NDELAY);
	if (fd < 0) return -1;

	// Briefly settle the port and clear NDELAY so subsequent reads block on
	// VTIME — matches the original open_port semantics.
	usleep(100000);
	tcflush(fd, TCIOFLUSH);
	fcntl(fd, F_SETFL, 0);

	if (apply_termios(fd) != 0) {
		close(fd);
		return -1;
	}

	if (!probe_at(fd, 800)) {
		close(fd);
		return -1;
	}
	return fd;
}

int open_port_once(void)
{
	// Pinned port: probe it and nothing else, so the user's selection is
	// tested exactly — no silent fallback to another port.
	if (forced_port[0]) {
		int fd = try_open_candidate(forced_port);
		if (fd >= 0) {
			snprintf(active_port, sizeof(active_port), "%s", forced_port);
			fprintf(stderr, "blackhand-modem: AT port found at %s (pinned)\n",
			        active_port);
			return fd;
		}
		return -1;
	}

	for (int i = 0; PORT_CANDIDATES[i]; i++) {
		int fd = try_open_candidate(PORT_CANDIDATES[i]);
		if (fd >= 0) {
			snprintf(active_port, sizeof(active_port), "%s", PORT_CANDIDATES[i]);
			fprintf(stderr, "blackhand-modem: AT port found at %s\n",
			        active_port);
			return fd;
		}
	}
	return -1;
}

int open_port()
{
	// SIM7600 needs ~5-15s after USB enumeration before AT is responsive.
	// Retry the full candidate sweep up to ~20s total before giving up.
	for (int attempt = 0; attempt < 10; attempt++) {
		int fd = open_port_once();
		if (fd >= 0)
			return fd;
		fprintf(stderr, "blackhand-modem: no AT-responding port yet "
		        "(attempt %d/10) — sleeping 2s before retry\n", attempt + 1);
		sleep(2);
	}

	fprintf(stderr, "blackhand-modem: no AT-responding port found after 20s. "
	        "Candidates tried: ");
	for (int i = 0; PORT_CANDIDATES[i]; i++)
		fprintf(stderr, "%s ", PORT_CANDIDATES[i]);
	fprintf(stderr, "\n");
	return -1;
}

int close_port(int fd)
{
	close(fd);
	return 0;
}

int read_line(int fd, char *buf, int max, int timeout_ms)
{
	int pos = 0;
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	long deadline = ts.tv_sec * 1000 + ts.tv_nsec / 1000000 + timeout_ms;

	while (1)
	{
		clock_gettime(CLOCK_MONOTONIC, &ts);
		long current_time = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;

		if (current_time >= deadline)
			return 0;

		if (pos == max)
			return -1;

		char c;
		int r = read(fd, &c, 1);

		if (r < 0)
		{
			perror("read error");
			return -1;
		}

		if (r == 0)
			continue;

		if (c == '\n')
		{
			if (pos > 0 && buf[pos - 1] == '\r')
				pos--;

			buf[pos] = '\0';
			return pos;
		}

		buf[pos++] = c;

		// "> " is the SMS body-input prompt — it has no newline terminator,
		// so surface it as a complete line on its own.
		if (pos == 2 && buf[0] == '>' && buf[1] == ' ')
		{
			buf[pos] = '\0';
			return pos;
		}
	}
	return 0;
}

const char *at_cmd(int fd, const char *cmd, int timeout_ms)
{
	static __thread char buffer[BUFFERSIZE];

	pthread_mutex_lock(&at_cmd_mutex);
	memset(buffer, 0, BUFFERSIZE);
	char full_cmd[256];
	snprintf(full_cmd, sizeof(full_cmd), "%s\r", cmd);
	char *ptr = full_cmd;
	size_t len = strlen(full_cmd);

	pthread_mutex_lock(&response_mutex);
	response_ready = 0;

	while (len > 0)
	{
		ssize_t n = write(fd, ptr, len);
		if (n < 0)
			break;
		ptr += n;
		len -= n;
	}

	struct timespec deadline;
	clock_gettime(CLOCK_REALTIME, &deadline);
	deadline.tv_sec  += timeout_ms / 1000;
	deadline.tv_nsec += (timeout_ms % 1000) * 1000000L;
	if (deadline.tv_nsec >= 1000000000L) {
		deadline.tv_sec  += 1;
		deadline.tv_nsec -= 1000000000L;
	}

	int rc = 0;
	while (response_ready == 0 && rc == 0)
		rc = pthread_cond_timedwait(&response_cond, &response_mutex, &deadline);

	if (response_ready)
		strncpy(buffer, response_buffer, BUFFERSIZE - 1);
	buffer[BUFFERSIZE - 1] = '\0';
	pthread_mutex_unlock(&response_mutex);
	pthread_mutex_unlock(&at_cmd_mutex);
	return buffer;
}
