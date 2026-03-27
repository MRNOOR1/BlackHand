#ifndef COMM_SERVICE_H
#define COMM_SERVICE_H

#include <stddef.h>

typedef struct {
    char name[32];
    char time[24];
    char type[16];
    char icon[8];
} CommCall;

typedef struct {
    char sender[32];
    char body[64];
    char stamp[24];
} CommMessage;

void comm_service_init(void);
void comm_service_shutdown(void);

size_t comm_service_call_count(void);
const CommCall *comm_service_call_at(size_t index);
int comm_service_call_add(const char *name, const char *type);
int comm_service_call_delete(size_t index);

size_t comm_service_message_count(void);
const CommMessage *comm_service_message_at(size_t index);
int comm_service_message_add(const char *sender, const char *body);
int comm_service_message_delete(size_t index);
void comm_service_reset(void);

#endif
