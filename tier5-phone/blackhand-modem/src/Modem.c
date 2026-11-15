#include "gps_service.h"
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
pthread_mutex_t call_state_mutex = PTHREAD_MUTEX_INITIALIZER;
volatile int fn = -1;
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
	struct timespec answered_at; /* monotonic talk-time origin */
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
		clock_gettime(CLOCK_MONOTONIC, &session.answered_at);
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
	if (snap.answered) {
		struct timespec now;
		clock_gettime(CLOCK_MONOTONIC, &now);
		duration = (int)(now.tv_sec - snap.answered_at.tv_sec);
		if (now.tv_nsec < snap.answered_at.tv_nsec) duration--;
		if (duration < 0) duration = 0;
	}

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

/* Called by the URC reader when the modem fd errors (USB unplug / driver
 * reset). Tears the modem down so the background bring-up loop re-probes and
 * re-opens. Uses trylock, NOT a blocking lock: if a manual port switch
 * (modem_select_port) is already tearing us down it holds bringup_lock and is
 * about to urc_stop()→join this very thread — blocking here would deadlock the
 * join. Returns 1 if THIS caller performed the teardown (and now owns its own
 * thread cleanup), 0 if a concurrent switch already owns it. */
int modem_reader_teardown(void)
{
	if (pthread_mutex_trylock(&bringup_lock) != 0)
		return 0;
	if (fn >= 0) {
		/* close_port_locked (not close_port): waits for any at_cmd()/
		 * at_cmd_sms() in flight on this fd to finish before closing, so a
		 * concurrent caller (dispatch thread, bring-up thread) can never be
		 * left mid-write()/read() on an fd this teardown just handed to a
		 * fresh open() elsewhere. See serial_utility.c. */
		close_port_locked(fn);
		fn = -1;
	}
	gps_reset_state();
	modem_present = 0;
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
		gps_reset_state();
		modem_present = 0;
		pthread_mutex_lock(&call_state_mutex);
		call_state = CALL_IDLE;
		pthread_mutex_unlock(&call_state_mutex);
		urc_stop();
		/* close_port_locked: see modem_reader_teardown() above for why plain
		 * close_port() is unsafe here. */
		close_port_locked(fn);
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
	return 0;
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
	char strength[16] = {0};
	if (end != NULL)
	{
		size_t length = end - start;
		if (length >= sizeof(strength)) length = sizeof(strength) - 1;
		strncpy(strength, start, length);
		strength[length] = '\0';
	}
	return atoi(strength);
}

int modem_registered()
{
	const char *response = at_cmd(fn, "AT+CREG?", 2000);
	char *start = strchr(response, ',');
	if (start == NULL)
		return -1;
	start += 1;
	char stats[4] = {0};
	strncpy(stats, start, 1);
	stats[1] = '\0';
	int stat = atoi(stats);
	if (stat == 1 || stat == 5)
		return 1;
	return -1;
}

int modem_dial(const char *number)
{
	pthread_mutex_lock(&call_state_mutex);
	int busy = (call_state != CALL_IDLE);
	pthread_mutex_unlock(&call_state_mutex);
	if (busy)
		return -1;

	if (!number || !number[0] || strlen(number) > 40 ||
	    strspn(number, "+0123456789*#") != strlen(number)) return -1;
	call_session_start(0, number);
	char cmd[64];
	snprintf(cmd, sizeof(cmd), "ATD%s;", number);
	const char *response = at_cmd(fn, cmd, 10000);

	if (strstr(response, "OK") != NULL)
	{
		pthread_mutex_lock(&call_state_mutex);
		call_state = CALL_DIALLING;
		pthread_mutex_unlock(&call_state_mutex);
		return 1;
	}
	pthread_mutex_lock(&call_state_mutex);
	call_state = CALL_IDLE;
	pthread_mutex_unlock(&call_state_mutex);
	call_session_end("failed");
	return -1;
}

int modem_answer()
{
	pthread_mutex_lock(&call_state_mutex);
	int ringing = (call_state == CALL_RINGING);
	pthread_mutex_unlock(&call_state_mutex);
	if (!ringing)
		return -1;

	const char *response = at_cmd(fn, "ATA", 2000);
	if (strstr(response, "OK") != NULL)
	{
		pthread_mutex_lock(&call_state_mutex);
		call_state = CALL_ACTIVE;
		pthread_mutex_unlock(&call_state_mutex);
		call_session_answered();
		return 1;
	}
	return -1;
}
int modem_hangup()
{
	pthread_mutex_lock(&call_state_mutex);
	CallState cs = call_state;
	pthread_mutex_unlock(&call_state_mutex);

	if (cs == CALL_IDLE)
		return 1;   /* already idle — no-op, not an error */

	const char *outcome = (cs == CALL_RINGING) ? "rejected" :
	                      (cs == CALL_DIALLING) ? "cancelled" : NULL;

	const char *response = at_cmd(fn, "AT+CHUP", 3000);
	if (strstr(response, "OK") == NULL)
		response = at_cmd(fn, "ATH", 2000);
	if (strstr(response, "OK") != NULL)
	{
		pthread_mutex_lock(&call_state_mutex);
		call_state = CALL_IDLE;
		pthread_mutex_unlock(&call_state_mutex);
		modem_clear_incoming_number();
		call_session_end(outcome);
		return 1;
	}
	return -1;
}

int modem_sms_send(const char *number, const char *message)
{
	// The whole CMGS transaction runs under at_cmd_mutex inside at_cmd_sms, so
	// a background AT+CSQ poll can't inject into the body or steal the response
	// (H5). Body wait capped at 10s (was 60s) so the single-threaded dispatch
	// loop can't be blocked for a minute on a slow send (H6).
	const char *response = at_cmd_sms(fn, number, message, 5000, 10000);

	if (strstr(response, "+CMS ERROR") != NULL)
		return -1;
	if (strstr(response, "+CMGS:") != NULL)
		return 1;
	return -1;
}
