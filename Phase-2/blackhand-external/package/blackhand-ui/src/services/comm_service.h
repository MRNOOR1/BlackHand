/*
 * comm_service.h — UI-side read cache of communication history.
 *
 * Backed by blackhand-storage's per-number layout:
 *   calls    ← history.calls_recent (merged, newest first)
 *   messages ← history.sms_threads  (one entry per conversation)
 *   thread   ← history.sms_list     (one open conversation)
 *
 * The UI never writes history — the modem service records calls and SMS at
 * the source (URC events). comm_service_call_add* / message_add* remain as
 * compatibility shims that simply re-sync from storage.
 */
#ifndef COMM_SERVICE_H
#define COMM_SERVICE_H

#include <stddef.h>

typedef struct {
    char name[48];    /* resolved contact name, or the number itself */
    char phone[32];
    char time[24];    /* formatted from ts                           */
    char type[16];    /* incoming | outgoing | missed | rejected | busy | no_answer */
    char icon[8];
    int  duration_sec;
    int  storage_id;  /* unused in per-number layout (kept for ABI)  */
} CommCall;

typedef struct {
    char sender[48];  /* thread list: display name; thread view: "Me"/name */
    char phone[32];
    char body[160];   /* thread list: last message; thread view: the body  */
    char stamp[24];
    int  is_outgoing;
    int  read;        /* thread list: 1 when NO unread in conversation     */
    int  storage_id;  /* thread list: unread count (repurposed)            */
} CommMessage;

void comm_service_init(void);
void comm_service_shutdown(void);

/* Re-fetch calls + threads from the storage service. */
void comm_service_sync(void);

/* 1 if the last sync reached the storage daemon. Screens show a
 * STORAGE OFFLINE banner when 0 — never a silently empty list. */
int comm_service_storage_ok(void);

/* ── recent calls (merged across all numbers) ── */
size_t comm_service_call_count(void);
const CommCall *comm_service_call_at(size_t index);

/* ── SMS threads (one entry per conversation, newest first) ── */
size_t comm_service_message_count(void);
const CommMessage *comm_service_message_at(size_t index);

/* ── one open conversation ── */
/* Loads the full message list for `number` and marks it read.
 * Returns 0 on success. */
int comm_service_thread_open(const char *number);
size_t comm_service_thread_count(void);
const CommMessage *comm_service_thread_at(size_t index);
const char *comm_service_thread_number(void);

/* ── compatibility shims (history is written by the modem service now) ── */
int comm_service_call_add(const char *name, const char *type);
int comm_service_call_add_full(const char *name, const char *phone,
                                const char *type, int duration_sec);
int comm_service_call_delete(size_t index);
int comm_service_message_add(const char *sender, const char *body);
int comm_service_message_add_full(const char *sender, const char *phone,
                                   const char *body, int is_outgoing);
int comm_service_message_delete(size_t index);

void comm_service_reset(void);

#endif
