#include "serial_utility.h"
#include "Modem.h"
#include "urc.h"
#include <pthread.h>
#include <string.h>
#include <stdio.h>

typedef struct
{
	const char *prefix;
	void (*handler)(const char *line);
} MessageHandler;

MessageHandler handlers[] = {
	{"RING", handle_ring},
	{"+CLIP:", handle_clip},
	{"+CMT:", handle_cmt},
	{"NO CARRIER", handle_no_carrier},
	{"BUSY", handle_busy},
	{"VOICE CALL: BEGIN", handle_call_begin},
	{"MISSED_CALL", handle_missed_call},
	{"VOICE CALL: END", handle_call_end}};

static pthread_t urc_thread_id;
static int urc_running = 0;

void *urc_thread_func(void *arg)
{
	int fd = (int)(intptr_t)arg;

	char accum_buffer[BUFFERSIZE];
	int offset = 0;
	
	while (urc_running)
	{
		char line[256];
		int n = read_line(fd, line, sizeof(line), 2000);

		if (n < 0)
			break;
		if (n == 0)
			continue;

		if( process_message(line) < 0 )
		{
			// SMS body-input prompt: terminator with its own payload, so the
			// caller can strstr() for ">" to know the modem is ready for data.
			if (strcmp(line, "> ") == 0)
			{
				pthread_mutex_lock(&response_mutex);
				strncpy(response_buffer, "> ", BUFFERSIZE);
				response_ready = 1;
				pthread_cond_signal(&response_cond);
				pthread_mutex_unlock(&response_mutex);
				offset = 0;
				accum_buffer[0] = '\0';
				continue;
			}

			if (offset + n < BUFFERSIZE)
			{
				if (strcmp(line, "OK") == 0 ||
					strcmp(line, "ERROR") == 0 ||
					strcmp(line, "ABORTED") == 0 ||
					strncmp(line, "+CME ERROR", 10) == 0 ||
					strncmp(line, "+CMS ERROR", 10) == 0)
				{
					// Include the terminator in the buffer so callers can
					// distinguish OK from ERROR / +CMS ERROR.
					offset += snprintf(accum_buffer + offset, BUFFERSIZE - offset, "%s\n", line);
					// signal at_cmd() with accum_buffer
					pthread_mutex_lock(&response_mutex);
					strncpy(response_buffer, accum_buffer, BUFFERSIZE);
					response_ready = 1;
					pthread_cond_signal(&response_cond);
					pthread_mutex_unlock(&response_mutex);
					// reset accumulator
					offset = 0;
					accum_buffer[0] = '\0';
					continue;
				}
				offset += snprintf(accum_buffer + offset, BUFFERSIZE - offset, "%s\n", line);
			}
			else
			{
				// buffer overflow, reset
				offset = 0;
				accum_buffer[0] = '\0';
			}

		}
	}
	return NULL;
}

int process_message(const char *msg)
{
	int count = sizeof(handlers) / sizeof(handlers[0]);

	for (int i = 0; i < count; i++)
	{
		size_t len = strlen(handlers[i].prefix);

		if (strncmp(msg, handlers[i].prefix, len) == 0)
		{
			handlers[i].handler(msg);
			return 1;
		}
	}
	return -1;
}

void urc_start(int fd)
{
	urc_running = 1;
	pthread_create(&urc_thread_id, NULL, urc_thread_func, (void *)(intptr_t)fd);
}

void urc_stop()
{
	urc_running = 0;
	pthread_join(urc_thread_id, NULL);
}

void handle_ring(const char *line)
{
	printf("Incoming call logic\n");
	call_state = CALL_RINGING;
}

void handle_clip(const char *line)
{
	// +CLIP: "+1234567890",129,"",0,"",0  →  pull the first quoted field.
	const char *start = strchr(line, '"');
	if (start)
	{
		start++;
		const char *end = strchr(start, '"');
		if (end && end > start)
		{
			char number[64];
			size_t len = end - start;
			if (len >= sizeof(number)) len = sizeof(number) - 1;
			memcpy(number, start, len);
			number[len] = '\0';
			modem_set_incoming_number(number);
			printf("Caller ID: %s\n", number);
			return;
		}
	}
	printf("Caller ID: (unparsed) %s\n", line);
}

void handle_cmt(const char *line)
{
	// +CMT: "+1234567890","",... — pull sender from the first quoted field.
	char sender[64] = "";
	const char *q1 = strchr(line, '"');
	if (q1) {
		q1++;
		const char *q2 = strchr(q1, '"');
		if (q2 && q2 > q1) {
			size_t len = q2 - q1;
			if (len >= sizeof(sender)) len = sizeof(sender) - 1;
			memcpy(sender, q1, len);
			sender[len] = '\0';
		}
	}

	// Body is on the next line — safe to read directly because we're already
	// the sole reader of the modem fd (we ARE the URC thread).
	char body[256];
	int n = read_line(fn, body, sizeof(body), 2000);
	if (n > 0) {
		printf("SMS from %s: %s\n", sender[0] ? sender : "(unknown)", body);
		modem_push_pending_sms(sender, body);
	} else {
		printf("SMS body: <missing or timed out>\n");
	}
}

void handle_no_carrier(const char *line)
{
	printf("Call ended logic\n");
	call_state = CALL_IDLE;
	modem_clear_incoming_number();
}

void handle_busy(const char *line)
{
	printf("Line busy logic\n");
	call_state = CALL_IDLE;
	modem_clear_incoming_number();
}

void handle_call_begin(const char *line)
{
	printf("Call connected logic\n");
	call_state = CALL_ACTIVE;
}

void handle_call_end(const char *line)
{
	printf("Call ended logic\n");
	call_state = CALL_IDLE;
	modem_clear_incoming_number();
}

void handle_missed_call(const char *line)
{
	printf("Missed call logic\n");
	call_state = CALL_IDLE;
	modem_clear_incoming_number();
}