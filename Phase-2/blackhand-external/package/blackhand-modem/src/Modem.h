
#ifndef MODEM_H
#define MODEM_H
#include "serial_utility.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
extern volatile int fn;

// Modem hardware presence — 1 once the AT port is open and configured.
// IPC server runs regardless so the UI can ask "are you there?" and get a
// clear answer instead of timing out on a missing daemon.
extern int  modem_present;
extern char modem_error[128];

typedef enum
{
	CALL_IDLE = 0,
	CALL_DIALLING,
	CALL_RINGING,
	CALL_ACTIVE
} CallState;

extern CallState call_state;
extern pthread_mutex_t call_state_mutex;

// Incoming caller number, tracked from +CLIP URC.
void modem_set_incoming_number(const char *num);
void modem_get_incoming_number(char *buf, size_t buf_size);
void modem_clear_incoming_number(void);

// Call session: one live call tracked from first event to terminal event,
// recorded to storage exactly once (see Modem.c).
void call_session_start(int incoming, const char *number);
void call_session_set_number(const char *number);
void call_session_answered(void);
void call_session_end(const char *outcome_hint); /* NULL = derive */

// Pending incoming SMS queue (populated by +CMT URC, drained by IPC).
void modem_push_pending_sms(const char *sender, const char *body);
int  modem_pending_sms_count(void);
int  modem_pending_sms_at(int index,
                          char *sender, size_t sender_size,
                          char *body,   size_t body_size);
void modem_clear_pending_sms(void);

int modem_init();
// One locked probe attempt — used by the background bring-up thread.
int modem_attempt_bringup(void);
// Pin + switch to a specific port ("auto" = resume sweep). 0 = modem up.
int modem_select_port(const char *port);
int modem_configure();										 // done
int modem_dial(const char *number);							 // done
int modem_answer();											 // done
int modem_hangup();											 // done
int modem_sms_send(const char *number, const char *message); // done
int modem_signal();											 // done
int  modem_signal_cached(void);  /* last polled CSQ, never blocks          */
void modem_signal_poll(void);    /* refresh cache (background thread only) */
int modem_registered();										 // done
#endif
