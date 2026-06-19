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
        ssize_t n = send(fd, p + sent, len - sent, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) return -1;
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
        if (n == 0) return -1;  /* connection closed */
        received += (size_t)n;
    }
    return 0;
}

/* ── Server ──────────────────────────────────────────────────────────────*/

int ipc_server_listen(const char *path)
{
    if (!path) return IPC_ERR_INVALID_ARG;

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

/* ── Client ──────────────────────────────────────────────────────────────*/

int ipc_client_connect(const char *path)
{
    if (!path) return IPC_ERR_INVALID_ARG;
    if (strlen(path) >= sizeof(((struct sockaddr_un *)0)->sun_path))
        return IPC_ERR_INVALID_ARG;

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return IPC_ERR_SOCKET;

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

    int fd = ipc_client_connect(path);
    if (fd < 0) return fd;

    /* Prevent the UI from blocking forever if a service hangs mid-response. */
    struct timeval tv = { .tv_sec = 10, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    if (send_msg(fd, json) != 0) {
        ipc_close(fd);
        return IPC_ERR_SEND;
    }

    int n = recv_msg(fd, buf, (int)buf_size);
    ipc_close(fd);
    return (n < 0) ? IPC_ERR_RECV : IPC_OK;
}

/* ── Transport ───────────────────────────────────────────────────────────*/

int send_msg(int fd, const char *msg)
{
    if (!msg) return -1;
    uint32_t len = (uint32_t)strlen(msg);
    uint32_t net_len = htonl(len);
    if (send_all(fd, &net_len, sizeof(net_len)) < 0) return -1;
    if (send_all(fd, msg, len) < 0) return -1;
    return 0;
}

int recv_msg(int fd, char *buffer, int buffer_size)
{
    if (!buffer || buffer_size <= 0) return -1;

    uint32_t net_len;
    if (recv_all(fd, &net_len, sizeof(net_len)) < 0) return -1;

    uint32_t len = ntohl(net_len);
    if (len >= (uint32_t)buffer_size) return -1;

    if (recv_all(fd, buffer, len) < 0) return -1;
    buffer[len] = '\0';
    return (int)len;
}

void ipc_close(int fd)
{
    if (fd >= 0) close(fd);
}
