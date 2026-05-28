#ifndef COMM_SERVICE_H
#define COMM_SERVICE_H

#include <stddef.h>

typedef struct {
    char name[48];
    char phone[32];
    char time[24];
    char type[16];   /* "incoming" | "outgoing" | "missed" */
    char icon[8];
    int  duration_sec;
    int  storage_id; /* id in calls.json, 0 if not persisted yet */
} CommCall;

typedef struct {
    char sender[48];
    char phone[32];
    char body[160];
    char stamp[24];
    int  is_outgoing;
    int  read;
    int  storage_id; /* id in messages.json, 0 if not yet persisted */
} CommMessage;

void comm_service_init(void);
void comm_service_shutdown(void);

/* Load calls + messages from the storage service (blackhand-storage).
 * Safe to call at startup and on re-enter of calls/messages screens. */
void comm_service_sync(void);

size_t comm_service_call_count(void);
const CommCall *comm_service_call_at(size_t index);
int comm_service_call_add(const char *name, const char *type);
int comm_service_call_add_full(const char *name, const char *phone,
                                const char *type, int duration_sec);
int comm_service_call_delete(size_t index);

size_t comm_service_message_count(void);
const CommMessage *comm_service_message_at(size_t index);
int comm_service_message_add(const char *sender, const char *body);
int comm_service_message_add_full(const char *sender, const char *phone,
                                   const char *body, int is_outgoing);
int comm_service_message_delete(size_t index);

void comm_service_reset(void);

#endif
