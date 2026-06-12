#include "serial_utility.h"
#include "Modem.h"
#include "urc.h"
#include "modem_storage.h"
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

/* ── call session ───────────────────────────────────────────────────────────
 * Tracks the one live call from first event (ATD / RING) to terminal event,
 * then writes exactly ONE history record via modem_storage. Both the URC
 * thread and the dispatch thread touch this, hence the mutex. The `active`
 * flag is what prevents double-recording when a local hangup (ATH) is
 * followed by the modem's own VOICE CALL: END / NO CARRIER. */

typedef struct {
	int    active;
	int    incoming;    /* 1 = in, 0 = out */
	int    answered;
	char   number[64];
	time_t ts_start;    /* when dialling/ringing began */
	time_t ts_answer;   /* when the call connected     */
} CallSession;

static pthread_mutex_t session_mutex = PTHREAD_MUTEX_INITIALIZER;
static CallSession session;

void call_session_start(int incoming, const char *number)
{
	pthread_mutex_lock(&session_mutex);
	// RING repeats every few seconds — don't restart an active session.
	if (!session.active) {
		memset(&session, 0, sizeof(session));
		session.active   = 1;
		session.incoming = incoming;
		session.ts_start = time(NULL);
		if (number)
			snprintf(session.number, sizeof(session.number), "%s", number);
	}
	pthread_mutex_unlock(&session_mutex);
}

void call_session_set_number(const char *number)
{
	pthread_mutex_lock(&session_mutex);
	if (session.active && number && number[0])
		snprintf(session.number, sizeof(session.number), "%s", number);
	pthread_mutex_unlock(&session_mutex);
}

void call_session_answered(void)
{
	pthread_mutex_lock(&session_mutex);
	if (session.active && !session.answered) {
		session.answered  = 1;
		session.ts_answer = time(NULL);
	}
	pthread_mutex_unlock(&session_mutex);
}

/* outcome_hint: explicit outcome ("busy", "rejected", "missed"), or NULL to
 * derive: answered → "answered"; unanswered in → "missed"; out → "no_answer" */
void call_session_end(const char *outcome_hint)
{
	CallSession snap;

	pthread_mutex_lock(&session_mutex);
	if (!session.active) {
		pthread_mutex_unlock(&session_mutex);
		return;
	}
	snap = session;
	session.active = 0;
	pthread_mutex_unlock(&session_mutex);

	const char *outcome = outcome_hint;
	if (!outcome)
		outcome = snap.answered ? "answered"
		        : (snap.incoming ? "missed" : "no_answer");

	int duration = 0;
	if (snap.answered)
		duration = (int)(time(NULL) - snap.ts_answer);

	// Outside the lock — this is a blocking IPC round-trip to storage.
	storage_record_call(snap.number,
	                    snap.incoming ? "in" : "out",
	                    outcome, (long)snap.ts_start, duration);
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

/* ── bring-up / port switching ──────────────────────────────────────────────
 * Both the background bring-up thread (main.c) and the set_port IPC handler
 * can (re)open the modem. This lock makes open/close/configure atomic so a
 * port switch can't race a background probe. */

static pthread_mutex_t bringup_lock = PTHREAD_MUTEX_INITIALIZER;

// One probe attempt. Returns 1 if the modem is (now) up, 0 if not.
int modem_attempt_bringup(void)
{
	pthread_mutex_lock(&bringup_lock);
	if (modem_present) {
		pthread_mutex_unlock(&bringup_lock);
		return 1;
	}
	int fd = open_port_once();
	if (fd < 0) {
		pthread_mutex_unlock(&bringup_lock);
		return 0;
	}
	fn = fd;
	urc_start(fn);
	modem_configure();
	modem_error[0] = '\0';
	modem_present  = 1;
	pthread_mutex_unlock(&bringup_lock);
	return 1;
}

/* Switch to a specific port ("auto" restores the sweep). Tears down the
 * current connection if any, pins the new port, and tries it immediately.
 * Returns 0 if the modem answered on the new port, -1 otherwise (the
 * background thread keeps retrying the pinned port every 5s after that). */
int modem_select_port(const char *port)
{
	pthread_mutex_lock(&bringup_lock);

	if (modem_present) {
		modem_present = 0;          // gate dispatch handlers first
		call_state    = CALL_IDLE;
		urc_stop();                 // joins the URC thread (≤2s read timeout)
		close_port(fn);
		fn = -1;
		modem_clear_incoming_number();
	}

	serial_set_forced_port(port);

	int fd = open_port_once();
	if (fd < 0) {
		snprintf(modem_error, sizeof(modem_error),
		         "no AT response on %s",
		         (port && port[0] && strcmp(port, "auto") != 0)
		             ? port : "any port (auto sweep)");
		pthread_mutex_unlock(&bringup_lock);
		return -1;
	}

	fn = fd;
	urc_start(fn);
	modem_configure();
	modem_error[0] = '\0';
	modem_present  = 1;
	pthread_mutex_unlock(&bringup_lock);
	return 0;
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

/* Cached CSQ — refreshed by the background thread every ~5s. The UI polls
 * modem_status every second; running AT+CSQ inline there blocked the UI for
 * up to 2s per poll (the "UI is very slow" bug, part 2). */
static int s_cached_signal = -1;

int modem_signal_cached(void)
{
	return s_cached_signal;
}

void modem_signal_poll(void)
{
	if (modem_present)
		s_cached_signal = modem_signal();
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
		call_session_start(0, number);
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
			// Belt and braces: VOICE CALL: BEGIN also marks answered, but
			// some firmware skips it for mobile-terminated calls.
			call_session_answered();
			return 1;
		}
	}
	return -1;
}
int modem_hangup()
{
	if (call_state != CALL_IDLE)
	{
		// Hanging up a ringing incoming call = rejecting it. Decide the
		// outcome BEFORE sending CHUP, while call_state still tells us why.
		const char *outcome = NULL;   /* NULL = derive (answered/no_answer) */
		if (call_state == CALL_RINGING)
			outcome = "rejected";

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
			call_session_end(outcome);
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

	// Capture the body up front — the write loop below advances `message`,
	// and storage_record_sms needs the original pointer.
	const char *body_for_log = message;

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
	if (strstr(buffer, "+CMGS:") != NULL) {
		// Persist to storage before returning so the UI's next sync finds it.
		storage_record_sms(number, "out", body_for_log);
		return 1;
	}
	return -1;
}
