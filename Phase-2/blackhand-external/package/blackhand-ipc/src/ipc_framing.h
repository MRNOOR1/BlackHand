#ifndef IPC_FRAMING_H
#define IPC_FRAMING_H

int send_msg(int fd, const char *msg);
int recv_msg(int fd, char *buffer, int buffer_size);

#endif
