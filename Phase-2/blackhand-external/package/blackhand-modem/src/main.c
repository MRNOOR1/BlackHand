#include "Modem.h"
#include "urc.h"
#include "modem_dispatch.h"
#include "ipc.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <sys/socket.h>

#define MODEM_SOCK  "/run/bh-modem.sock"
#define BUFFER_SIZE 8192

/* Probe-forever bring-up. Runs in the background so the IPC server is
 * reachable from the very first frame the UI draws, and so a modem that
 * enumerates late (SIM7600 takes 15-30s after power) or gets replugged is
 * picked up without restarting the service. */
static void *modem_bringup_thread(void *arg)
{
	(void)arg;
	int attempt = 0;

	for (;;) {
		int fd = open_port_once();
		if (fd >= 0) {
			fn = fd;
			urc_start(fn);
			modem_configure();
			// Publish presence only after the port is configured so the
			// first dial/sms can't race a half-initialised modem.
			modem_error[0] = '\0';
			modem_present  = 1;
			fprintf(stderr,
			        "blackhand-modem: ready on %s — signal=%d registered=%d\n",
			        modem_active_port(), modem_signal(), modem_registered());
			return NULL;
		}

		attempt++;
		snprintf(modem_error, sizeof(modem_error),
		         "modem not detected (probe %d, /dev/ttyUSB0..3) — "
		         "check SIM7600 power + USB; still retrying", attempt);
		// Log the first miss and then once a minute — not every 5s.
		if (attempt == 1 || attempt % 12 == 0)
			fprintf(stderr, "blackhand-modem: %s\n", modem_error);
		sleep(5);
	}
	return NULL;
}

int main(void)
{
	// recv_msg returns -1 if the peer goes away — don't let SIGPIPE on a
	// dropped client kill the daemon.
	signal(SIGPIPE, SIG_IGN);

	// Start the IPC server immediately and bring the modem up in the
	// background. The UI's modem_health call needs to reach SOMEONE on
	// /run/bh-modem.sock from the start — and the SIM7600 may not have
	// enumerated yet when we launch at boot.
	modem_present = 0;
	snprintf(modem_error, sizeof(modem_error), "probing for modem hardware...");

	pthread_t bringup;
	if (pthread_create(&bringup, NULL, modem_bringup_thread, NULL) == 0) {
		pthread_detach(bringup);
	} else {
		snprintf(modem_error, sizeof(modem_error),
		         "internal error: bring-up thread failed to start");
		fprintf(stderr, "blackhand-modem: %s\n", modem_error);
	}

	int server_fd = ipc_server_listen(MODEM_SOCK);
	if (server_fd < 0) {
		fprintf(stderr, "blackhand-modem: ipc_server_listen(%s) failed\n",
		        MODEM_SOCK);
		return 1;
	}
	fprintf(stderr, "blackhand-modem: listening on " MODEM_SOCK "\n");

	while (1) {
		int client_fd = accept(server_fd, NULL, NULL);
		if (client_fd < 0) {
			perror("accept");
			continue;
		}
		while (1) {
			char buffer[BUFFER_SIZE];
			int n = recv_msg(client_fd, buffer, BUFFER_SIZE);
			if (n <= 0) break;
			modem_dispatch_handle(client_fd, buffer);
		}
		close(client_fd);
	}

	return 0;
}
