#include "serial_utility.h"
#include "Modem.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <stdint.h>
#include <time.h>
#include <fcntl.h>
#include <termios.h>
#include <pthread.h>

CallState call_state = CALL_IDLE;
int fn = -1;
int  modem_present = 0;
char modem_error[128] = "";

static pthread_mutex_t incoming_mutex = PTHREAD_MUTEX_INITIALIZER;
static char incoming_number[64] = "";

void modem_set_incoming_number(const char *num)
{
	pthread_mutex_lock(&incoming_mutex);
	strncpy(incoming_number, num, sizeof(incoming_number) - 1);
	incoming_number[sizeof(incoming_number) - 1] = '\0';
	pthread_mutex_unlock(&incoming_mutex);
}

void modem_get_incoming_number(char *buf, size_t buf_size)
{
	if (!buf || buf_size == 0) return;
	pthread_mutex_lock(&incoming_mutex);
	strncpy(buf, incoming_number, buf_size - 1);
	buf[buf_size - 1] = '\0';
	pthread_mutex_unlock(&incoming_mutex);
}

void modem_clear_incoming_number(void)
{
	pthread_mutex_lock(&incoming_mutex);
	incoming_number[0] = '\0';
	pthread_mutex_unlock(&incoming_mutex);
}

// ── pending SMS queue ───────────────────────────────────────────────────────
// Fixed-size ring: drop newest if full. Drained on every pop, so the only way
// to fill it is if the UI hasn't polled in a while.

#define SMS_QUEUE_MAX 16

typedef struct {
	char sender[64];
	char body[160];
} PendingSms;

static pthread_mutex_t sms_mutex = PTHREAD_MUTEX_INITIALIZER;
static PendingSms sms_queue[SMS_QUEUE_MAX];
static int sms_count = 0;

void modem_push_pending_sms(const char *sender, const char *body)
{
	pthread_mutex_lock(&sms_mutex);
	if (sms_count < SMS_QUEUE_MAX) {
		PendingSms *e = &sms_queue[sms_count];
		snprintf(e->sender, sizeof(e->sender), "%s", sender ? sender : "");
		snprintf(e->body,   sizeof(e->body),   "%s", body   ? body   : "");
		sms_count++;
	}
	pthread_mutex_unlock(&sms_mutex);
}

int modem_pending_sms_count(void)
{
	pthread_mutex_lock(&sms_mutex);
	int n = sms_count;
	pthread_mutex_unlock(&sms_mutex);
	return n;
}

int modem_pending_sms_at(int index,
                         char *sender, size_t sender_size,
                         char *body,   size_t body_size)
{
	pthread_mutex_lock(&sms_mutex);
	if (index < 0 || index >= sms_count) {
		pthread_mutex_unlock(&sms_mutex);
		return -1;
	}
	if (sender && sender_size) snprintf(sender, sender_size, "%s", sms_queue[index].sender);
	if (body   && body_size)   snprintf(body,   body_size,   "%s", sms_queue[index].body);
	pthread_mutex_unlock(&sms_mutex);
	return 0;
}

void modem_clear_pending_sms(void)
{
	pthread_mutex_lock(&sms_mutex);
	sms_count = 0;
	pthread_mutex_unlock(&sms_mutex);
}

int modem_init()
{
	fn = open_port();
	if (fn == -1)
	{
		return -1;
	}

	return fn;
}
int modem_configure()
{
	at_cmd(fn, "ATE0", 2000);
	at_cmd(fn, "ATQ0", 2000);
	at_cmd(fn, "ATV1", 2000);
	at_cmd(fn, "AT+CMEE=2", 2000);
	at_cmd(fn, "AT+CMGF=1", 2000);
	at_cmd(fn, "AT+CNMI=2,2,0,0,0", 2000);
	at_cmd(fn, "AT+CLIP=1", 2000);
	return 1;
}

int modem_signal()
{
	const char *response = at_cmd(fn, "AT+CSQ", 2000);
	char *start = strstr(response, "+CSQ:");
	if (start == NULL)
		return -1;
	start += 5;
	char *end = strchr(start, ',');
	static char strength[16];
	if (end != NULL)
	{
		size_t length = end - start;
		strncpy(strength, start, length);
		strength[length] = '\0';
	}
	printf("Signal strength: %s\n", strength);
	return atoi(strength);
}

int modem_registered()
{
	const char *response = at_cmd(fn, "AT+CREG?", 2000);
	char *start = strchr(response, ',');
	if (start == NULL)
		return -1;
	start += 1;
	static char stats[8];
	strncpy(stats, start, 1);
	stats[1] = '\0';
	int stat = atoi(stats);
	if (stat == 1 || stat == 5)
	{
		return 1;
	}
	return -1;
}

int modem_dial(const char *number)
{
	if (call_state != CALL_IDLE)
		return -1;

	char cmd[64];
	snprintf(cmd, sizeof(cmd), "ATD%s;", number);
	const char *response = at_cmd(fn, cmd, 10000);

	// "OK" means the modem accepted the dial command. The transition to
	// CALL_ACTIVE happens later when the URC thread sees VOICE CALL: BEGIN.
	if (strstr(response, "OK") != NULL)
	{
		call_state = CALL_DIALLING;
		return 1;
	}
	call_state = CALL_IDLE;
	return -1;
}

int modem_answer()
{
	if (call_state == CALL_RINGING)
	{
		const char *response = at_cmd(fn, "ATA", 2000);
		if (strstr(response, "OK") != NULL)
		{
			call_state = CALL_ACTIVE;
			return 1;
		}
	}
	return -1;
}
int modem_hangup()
{
	if (call_state != CALL_IDLE)
	{
		// AT+CHUP is the voice-call hangup on SIM7600 — ATH is only
		// guaranteed for data calls and silently no-ops on voice in some
		// firmware. Keep ATH as a fallback for older firmware.
		const char *response = at_cmd(fn, "AT+CHUP", 3000);
		if (strstr(response, "OK") == NULL)
			response = at_cmd(fn, "ATH", 2000);
		if (strstr(response, "OK") != NULL)
		{
			call_state = CALL_IDLE;
			modem_clear_incoming_number();
			return 1;
		}
	}
	return -1;
}

int modem_sms_send(const char *number, const char *message)
{
	char cmd[64];
	snprintf(cmd, sizeof(cmd), "AT+CMGS=\"%s\"", number);

	// Phase 1: send the command, wait for "> " prompt via the URC thread.
	const char *response = at_cmd(fn, cmd, 5000);
	if (strstr(response, ">") == NULL)
		return -1;

	// Phase 2: send body + Ctrl-Z, wait for final +CMGS:/OK or +CMS ERROR.
	// Lock and reset response_ready BEFORE writing so the URC thread can't
	// publish the final response between our write and our cond_wait.
	pthread_mutex_lock(&response_mutex);
	response_ready = 0;

	size_t mlen = strlen(message);
	while (mlen > 0)
	{
		ssize_t n = write(fn, message, mlen);
		if (n < 0)
			break;
		message += n;
		mlen -= n;
	}
	char ctrlz = 0x1A;
	write(fn, &ctrlz, 1);

	// SMS submission can take a while over poor signal.
	struct timespec deadline;
	clock_gettime(CLOCK_REALTIME, &deadline);
	deadline.tv_sec += 60;

	int rc = 0;
	while (response_ready == 0 && rc == 0)
		rc = pthread_cond_timedwait(&response_cond, &response_mutex, &deadline);

	char buffer[BUFFERSIZE];
	buffer[0] = '\0';
	if (response_ready)
		strncpy(buffer, response_buffer, BUFFERSIZE);
	pthread_mutex_unlock(&response_mutex);

	if (strstr(buffer, "+CMS ERROR") != NULL)
		return -1;
	if (strstr(buffer, "+CMGS:") != NULL)
		return 1;
	return -1;
}
