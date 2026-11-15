#ifndef IPC_DISPATCH_H
#define IPC_DISPATCH_H

#include "cJSON.h"
#include "ipc_framing.h"

void dispatch(int fd, cJSON *req);
void send_error(int fd, const char *message);

#endif
