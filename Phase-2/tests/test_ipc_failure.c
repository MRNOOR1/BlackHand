#include "ipc.h"
#include <assert.h>
#include <arpa/inet.h>
#include <signal.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
int main(void) {
    signal(SIGPIPE, SIG_IGN);
    int fds[2]; assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    struct timeval tv = {0, 10000};
    assert(setsockopt(fds[0], SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0);
    uint32_t n = htonl(4);
    assert(write(fds[1], &n, 2) == 2); /* incomplete header */
    char buf[16];
    assert(recv_msg(fds[0], buf, sizeof(buf)) < 0);
    assert(send_msg(fds[0], "new request") < 0); /* never reuse a broken frame */
    close(fds[0]); close(fds[1]);
    puts("PASS: incomplete frame invalidates connection");
}
