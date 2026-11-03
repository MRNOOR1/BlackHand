/*
 * ipc.c — BlackHand IPC library implementation.
 *
 * Implements server setup, client connect, one-shot request, and the
 * length-prefixed send/recv transport used by every service.
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <stdint.h>
#include <sys/time.h>
#include "ipc.h"

/* ── Internal helpers ────────────────────────────────────────────────────
 *
 * send_all / recv_all loop until every byte is sent or received.
 * Plain send() / recv() are allowed to transfer fewer bytes than requested
 * (partial I/O). The loop retries until done, and handles EINTR (signal
 * interrupting a syscall) by simply retrying.
 */

static int send_all(int fd, const void *buf, size_t len)
{
    const char *p = (const char *)buf;
    size_t sent = 0;
    while (sent < len) {
#ifdef MSG_NOSIGNAL
        ssize_t n = send(fd, p + sent, len - sent, MSG_NOSIGNAL);
#else
        int one = 1;
        setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
        ssize_t n = send(fd, p + sent, len - sent, 0);
#endif
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) { errno = ECONNRESET; return -1; }
        sent += (size_t)n;
    }
    return 0;
}

static int recv_all(int fd, void *buf, size_t len)
{
    char *p = (char *)buf;
    size_t received = 0;
    while (received < len) {
        ssize_t n = recv(fd, p + received, len - received, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) { errno = ECONNRESET; return -1; }  /* peer hung up */
        received += (size_t)n;
    }
    return 0;
}

/*
 * set_timeouts — bound every blocking operation on a client fd.
 *
 * Must be called BEFORE connect(). On Linux, AF_UNIX connect() waits on the
 * peer's accept backlog using SO_SNDTIMEO (unix_wait_for_peer), so setting it
 * here also bounds connect() against a server that is listening but wedged
 * and never calling accept(). SO_RCVTIMEO bounds recv, SO_SNDTIMEO also
 * bounds send once the socket buffers fill because the peer stopped reading.
 *
 * A send that moved some bytes before timing out returns that partial count,
 * so send_all restarts the clock and keeps going — that is what lets a slow
 * but healthy peer finish. Against a peer that has stopped reading entirely
 * the measured worst case is ~2x IPC_TIMEOUT_SEC (one partial write, then one
 * with no progress at all). Bounded, not unbounded.
 */
static void set_timeouts(int fd)
{
    struct timeval tv = { .tv_sec = IPC_TIMEOUT_SEC, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

/* ── Server ──────────────────────────────────────────────────────────────*/

int ipc_server_listen(const char *path)
{
    if (!path || strlen(path) >= sizeof(((struct sockaddr_un *)0)->sun_path))
        return IPC_ERR_INVALID_ARG;

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return IPC_ERR_SOCKET;

    unlink(path);

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return IPC_ERR_BIND;
    }

    if (listen(fd, 5) < 0) {
        close(fd);
        return IPC_ERR_LISTEN;
    }

    return fd;
}

/* Bound accepted peers too; a partial frame must not monopolise a daemon. */
int ipc_server_accept(int server_fd)
{
    int fd = accept(server_fd, NULL, NULL);
    if (fd >= 0) {
        struct timeval tv = { .tv_sec = 2, .tv_usec = 0 };
        if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) != 0 ||
            setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) != 0) {
            int saved = errno; close(fd); errno = saved; return -1;
        }
    }
    return fd;
}

/* Poison the stream on framing failure. The caller still owns close(). */
static int frame_failed(int fd)
{
    int saved = errno;
    shutdown(fd, SHUT_RDWR);
    errno = saved;
    return -1;
}

/* ── Client ──────────────────────────────────────────────────────────────*/

int ipc_client_connect(const char *path)
{
    if (!path || strlen(path) >= sizeof(((struct sockaddr_un *)0)->sun_path))
        return IPC_ERR_INVALID_ARG;

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return IPC_ERR_SOCKET;

    set_timeouts(fd);

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return IPC_ERR_CONNECT;
    }

    return fd;
}

int ipc_request(const char *path, const char *json, char *buf, size_t buf_size)
{
    if (!path || !json || !buf || buf_size == 0) return IPC_ERR_INVALID_ARG;

    /* ipc_client_connect already bounds connect, send and recv by
     * IPC_TIMEOUT_SEC, so no call below can block forever. */
    int fd = ipc_client_connect(path);
    if (fd < 0) return fd;

    if (send_msg(fd, json) != 0) {
        int e = errno;
        ipc_close(fd);
        return (e == EAGAIN || e == EWOULDBLOCK) ? IPC_ERR_TIMEOUT : IPC_ERR_SEND;
    }

    int n = recv_msg(fd, buf, (int)buf_size);
    int e = errno;              /* capture before close() can clobber it */
    ipc_close(fd);

    if (n >= 0)                                  return IPC_OK;
    if (e == EMSGSIZE)                           return IPC_ERR_TOO_LARGE;
    if (e == EAGAIN || e == EWOULDBLOCK)         return IPC_ERR_TIMEOUT;
    return IPC_ERR_RECV;
}

/* ── Transport ───────────────────────────────────────────────────────────*/

int send_msg(int fd, const char *msg)
{
    if (!msg) { errno = EINVAL; return -1; }
    uint32_t len = (uint32_t)strlen(msg);
    uint32_t net_len = htonl(len);
    if (send_all(fd, &net_len, sizeof(net_len)) < 0) return frame_failed(fd);
    if (send_all(fd, msg, len) < 0) return frame_failed(fd);
    return 0;
}

int recv_msg(int fd, char *buffer, int buffer_size)
{
    if (!buffer || buffer_size <= 0) { errno = EINVAL; return -1; }

    uint32_t net_len;
    if (recv_all(fd, &net_len, sizeof(net_len)) < 0) return frame_failed(fd);

    uint32_t len = ntohl(net_len);

    /* The caller's buffer is the protocol ceiling. Say so out loud: this
     * failure is data-dependent and otherwise looks like a dropped
     * connection. See the SIZE CEILING note in ipc.h. */
    if (len >= (uint32_t)buffer_size) {
        fprintf(stderr,
                "ipc: message too large - %u bytes, buffer holds %d "
                "(limit %d); response dropped\n",
                len, buffer_size, buffer_size - 1);
        errno = EMSGSIZE;
        return frame_failed(fd);
    }

    if (recv_all(fd, buffer, len) < 0) return frame_failed(fd);
    buffer[len] = '\0';
    return (int)len;
}

void ipc_close(int fd)
{
    if (fd >= 0) close(fd);
}
