/*
 * ipc_selftest.c — self-test for the IPC transport. NOT part of the library.
 *
 * Deliberately absent from Makefile SRCS: it must never end up in
 * libblackhand-ipc.a or on the target. It exists so the transport can be
 * exercised on a host over real AF_UNIX sockets, which is the strongest
 * verification available for this package (no hardware involved).
 *
 * Run it (from blackhand-external/, gcc only, no libraries, no framework):
 *
 *   gcc -Wall -Wextra -DIPC_TIMEOUT_SEC=1 \
 *       -I package/blackhand-ipc/src \
 *       package/blackhand-ipc/src/ipc.c \
 *       package/blackhand-ipc/src/ipc_selftest.c -o /tmp/ipc_selftest \
 *   && /tmp/ipc_selftest
 *
 * IPC_TIMEOUT_SEC is shortened to 1s only so the timeout cases finish in
 * seconds instead of minutes. Everything else runs against the real code.
 */

#include "ipc.h"

/* The timeout fixtures below use fixed 4s naps, which only outlive
   SO_SNDTIMEO when the timeout is short. Built with the shipped 20s default,
   the peer hangs up first and those cases fail confusingly (EPIPE/ECONNRESET
   instead of EAGAIN) while the library is in fact correct. Fail at compile
   time with an actionable message rather than at runtime with a misleading
   assertion. */
#if IPC_TIMEOUT_SEC > 3
#error "build the selftest with -DIPC_TIMEOUT_SEC=1 (see the header comment above)"
#endif

#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <stdint.h>

#include "ipc.h"

#define SOCK_PATH "/tmp/bh-ipc-selftest.sock"

static int failures;

#define CHECK(cond) do {                                                   \
    if (cond) { printf("  ok   %s\n", #cond); }                            \
    else { printf("  FAIL %s  (line %d, errno=%d %s)\n",                   \
                  #cond, __LINE__, errno, strerror(errno)); failures++; }  \
} while (0)

static void nap_ms(int ms)
{
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

static long elapsed_ms(struct timespec *t0)
{
    struct timespec t1;
    clock_gettime(CLOCK_MONOTONIC, &t1);
    return (t1.tv_sec - t0->tv_sec) * 1000 + (t1.tv_nsec - t0->tv_nsec) / 1000000;
}

/* ── 1. plain round trip over a socketpair ─────────────────────────────── */
static void test_round_trip(void)
{
    printf("round trip\n");
    int sv[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);

    char buf[128];
    CHECK(send_msg(sv[0], "{\"jsonrpc\":\"2.0\"}") == 0);
    CHECK(recv_msg(sv[1], buf, sizeof(buf)) == 17);
    CHECK(strcmp(buf, "{\"jsonrpc\":\"2.0\"}") == 0);

    close(sv[0]); close(sv[1]);
}

/* ── 2. zero-length payload must not be mistaken for a closed socket ───── */
static void test_zero_length(void)
{
    printf("zero-length message\n");
    int sv[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);

    char buf[16];
    memset(buf, 'x', sizeof(buf));
    CHECK(send_msg(sv[0], "") == 0);
    CHECK(recv_msg(sv[1], buf, sizeof(buf)) == 0);
    CHECK(buf[0] == '\0');

    close(sv[0]); close(sv[1]);
}

/* ── 3. boundary: largest accepted size, and one byte past it ──────────── */
static void test_size_boundary(void)
{
    printf("size boundary and overflow guard\n");
    char big[80];
    memset(big, 'A', sizeof(big));

    /* buf is 64 usable bytes; guard byte sits immediately after. */
    char buf[65];
    buf[64] = 0x7f;

    int sv[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    big[63] = '\0';                       /* 63 chars: the largest that fits */
    CHECK(send_msg(sv[0], big) == 0);
    CHECK(recv_msg(sv[1], buf, 64) == 63);
    CHECK(buf[63] == '\0');               /* null-terminated in the last slot */
    CHECK(buf[64] == 0x7f);               /* no off-by-one past the buffer */
    close(sv[0]); close(sv[1]);

    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    memset(big, 'A', sizeof(big));
    big[64] = '\0';                       /* 64 chars: one too many */
    CHECK(send_msg(sv[0], big) == 0);
    errno = 0;
    CHECK(recv_msg(sv[1], buf, 64) == -1);
    CHECK(errno == EMSGSIZE);             /* distinguishable from a hangup */
    close(sv[0]); close(sv[1]);
}

/* ── 4. short reads/writes: peer dribbles the frame out in pieces ──────── */
static void test_chunked_peer(void)
{
    printf("chunked peer (short reads)\n");
    int sv[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);

    const char *payload = "0123456789abcdefghijklmnopqrstuvwxyz";
    uint32_t len = (uint32_t)strlen(payload);
    uint32_t net = htonl(len);

    pid_t pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        close(sv[1]);
        for (size_t i = 0; i < sizeof(net); i++) {   /* prefix byte by byte */
            ssize_t w = write(sv[0], (char *)&net + i, 1);
            (void)w;
            nap_ms(20);
        }
        for (uint32_t i = 0; i < len; i += 7) {      /* payload in chunks */
            uint32_t n = (len - i < 7) ? len - i : 7;
            ssize_t w = write(sv[0], payload + i, n);
            (void)w;
            nap_ms(20);
        }
        close(sv[0]);
        _exit(0);
    }
    close(sv[0]);

    char buf[128];
    CHECK(recv_msg(sv[1], buf, sizeof(buf)) == (int)len);
    CHECK(strcmp(buf, payload) == 0);
    close(sv[1]);
    waitpid(pid, NULL, 0);
}

/* ── 5. EINTR: a signal mid-frame must not corrupt the stream ──────────── */
static volatile sig_atomic_t got_signal;
static void on_alarm(int sig) { (void)sig; got_signal++; }

static void test_eintr(void)
{
    printf("EINTR during recv\n");
    int sv[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);

    const char *payload = "interrupted-but-intact";
    uint32_t len = (uint32_t)strlen(payload);
    uint32_t net = htonl(len);

    pid_t pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        close(sv[1]);
        ssize_t w = write(sv[0], &net, sizeof(net));
        nap_ms(300);                       /* signal lands in this gap */
        w = write(sv[0], payload, len);
        (void)w;
        close(sv[0]);
        _exit(0);
    }
    close(sv[0]);

    /* No SA_RESTART: the signal really does interrupt the blocked recv. */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_alarm;
    sigaction(SIGALRM, &sa, NULL);
    struct itimerval it;
    memset(&it, 0, sizeof(it));
    it.it_value.tv_usec = 100000;          /* 100ms */
    setitimer(ITIMER_REAL, &it, NULL);

    char buf[128];
    int n = recv_msg(sv[1], buf, sizeof(buf));
    CHECK(got_signal > 0);                 /* the interrupt actually happened */
    CHECK(n == (int)len);
    CHECK(strcmp(buf, payload) == 0);

    signal(SIGALRM, SIG_DFL);
    close(sv[1]);
    waitpid(pid, NULL, 0);
}

/* ── 6. peer hangs up mid-message: error, not a hang or garbage ────────── */
static void test_truncated_peer(void)
{
    printf("peer hangs up mid-message\n");
    int sv[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);

    uint32_t net = htonl(500);             /* promises 500 bytes */
    assert(write(sv[0], &net, sizeof(net)) == (ssize_t)sizeof(net));
    assert(write(sv[0], "half a message", 14) == 14);
    close(sv[0]);                          /* then vanishes */

    char buf[1024];
    memset(buf, 0x5a, sizeof(buf));
    errno = 0;
    CHECK(recv_msg(sv[1], buf, sizeof(buf)) == -1);
    CHECK(errno == ECONNRESET);            /* not a stale/garbage errno */
    close(sv[1]);
}

/* ── 7. send to a peer that never reads must time out, not wedge ───────── */
static void test_send_timeout(void)
{
    printf("send timeout against a non-reading peer\n");
    int fd = ipc_server_listen(SOCK_PATH);
    assert(fd >= 0);

    pid_t pid = fork();
    assert(pid >= 0);
    if (pid == 0) {                        /* server: accepts, never reads */
        int c = accept(fd, NULL, NULL);
        nap_ms(4000);
        if (c >= 0) close(c);
        close(fd);
        _exit(0);
    }
    close(fd);

    int cfd = ipc_client_connect(SOCK_PATH);
    CHECK(cfd >= 0);

    size_t big = 8u * 1024 * 1024;         /* far beyond any socket buffer */
    char *msg = malloc(big + 1);
    assert(msg);
    memset(msg, 'Z', big);
    msg[big] = '\0';

    struct timespec t0;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    errno = 0;
    int rc = send_msg(cfd, msg);
    long ms = elapsed_ms(&t0);
    CHECK(rc == -1);
    CHECK(errno == EAGAIN || errno == EWOULDBLOCK);
    printf("       (blocked %ld ms, limit %d s)\n", ms, IPC_TIMEOUT_SEC);
    CHECK(ms < (IPC_TIMEOUT_SEC + 2) * 1000);

    free(msg);
    ipc_close(cfd);
    kill(pid, SIGKILL);
    waitpid(pid, NULL, 0);
    unlink(SOCK_PATH);
}

/* ── 8. ipc_request round trip against a real server ───────────────────── */
static void test_ipc_request_ok(void)
{
    printf("ipc_request round trip\n");
    int fd = ipc_server_listen(SOCK_PATH);
    assert(fd >= 0);

    pid_t pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        int c = accept(fd, NULL, NULL);
        char in[4096];
        int n = recv_msg(c, in, sizeof(in));
        if (n >= 0) send_msg(c, "{\"result\":\"pong\"}");
        close(c); close(fd);
        _exit(0);
    }
    close(fd);

    char resp[4096];
    CHECK(ipc_request(SOCK_PATH, "{\"method\":\"ping\"}", resp, sizeof(resp)) == IPC_OK);
    CHECK(strcmp(resp, "{\"result\":\"pong\"}") == 0);

    waitpid(pid, NULL, 0);
    unlink(SOCK_PATH);
}

/* ── 9. ipc_request error codes are distinguishable ────────────────────── */
static void test_ipc_request_no_server(void)
{
    printf("ipc_request with no server\n");
    char resp[64];
    unlink(SOCK_PATH);
    CHECK(ipc_request(SOCK_PATH, "{}", resp, sizeof(resp)) == IPC_ERR_CONNECT);
    CHECK(ipc_request(NULL, "{}", resp, sizeof(resp)) == IPC_ERR_INVALID_ARG);
}

static void test_ipc_request_timeout(void)
{
    printf("ipc_request against a silent server\n");
    int fd = ipc_server_listen(SOCK_PATH);
    assert(fd >= 0);

    pid_t pid = fork();
    assert(pid >= 0);
    if (pid == 0) {                        /* accepts, reads, never answers */
        int c = accept(fd, NULL, NULL);
        char in[4096];
        recv_msg(c, in, sizeof(in));
        nap_ms((IPC_TIMEOUT_SEC + 3) * 1000);
        close(c); close(fd);
        _exit(0);
    }
    close(fd);

    char resp[4096];
    struct timespec t0;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    int rc = ipc_request(SOCK_PATH, "{\"method\":\"slow\"}", resp, sizeof(resp));
    long ms = elapsed_ms(&t0);
    CHECK(rc == IPC_ERR_TIMEOUT);          /* not collapsed into IPC_ERR_RECV */
    printf("       (waited %ld ms, limit %d s)\n", ms, IPC_TIMEOUT_SEC);
    CHECK(ms < (IPC_TIMEOUT_SEC + 2) * 1000);

    kill(pid, SIGKILL);
    waitpid(pid, NULL, 0);
    unlink(SOCK_PATH);
}

static void test_ipc_request_too_large(void)
{
    printf("ipc_request with an oversized response\n");
    int fd = ipc_server_listen(SOCK_PATH);
    assert(fd >= 0);

    pid_t pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        int c = accept(fd, NULL, NULL);
        char in[4096];
        recv_msg(c, in, sizeof(in));
        char *big = malloc(9000);
        memset(big, 'H', 8999);
        big[8999] = '\0';
        send_msg(c, big);                  /* 8999 bytes into a 4096 buffer */
        free(big);
        nap_ms(200);
        close(c); close(fd);
        _exit(0);
    }
    close(fd);

    char resp[4096];
    int rc = ipc_request(SOCK_PATH, "{\"method\":\"history\"}", resp, sizeof(resp));
    CHECK(rc == IPC_ERR_TOO_LARGE);        /* loud and distinct, not -1 */
    CHECK(rc != IPC_ERR_RECV);

    kill(pid, SIGKILL);
    waitpid(pid, NULL, 0);
    unlink(SOCK_PATH);
}

/* ── 10. connect() to a wedged server must not block forever ───────────── */
static void test_connect_timeout(void)
{
    printf("connect to a listening but never-accepting server\n");
    int fd = ipc_server_listen(SOCK_PATH);   /* backlog 5, nobody accepts */
    assert(fd >= 0);

    /* Fill the backlog, then time the connect that has nowhere to go. */
    int held[64];
    int n_held = 0;
    long ms = 0;
    int rc = 0;
    for (int i = 0; i < 64; i++) {
        struct timespec t0;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        rc = ipc_client_connect(SOCK_PATH);
        ms = elapsed_ms(&t0);
        if (rc < 0) break;
        held[n_held++] = rc;
    }
    CHECK(rc == IPC_ERR_CONNECT);            /* it gave up instead of hanging */
    printf("       (backlog full after %d connects, blocked %ld ms, limit %d s)\n",
           n_held, ms, IPC_TIMEOUT_SEC);
    CHECK(ms < (IPC_TIMEOUT_SEC + 2) * 1000);

    for (int i = 0; i < n_held; i++) ipc_close(held[i]);
    close(fd);
    unlink(SOCK_PATH);
}

int main(void)
{
    signal(SIGPIPE, SIG_IGN);
    unlink(SOCK_PATH);

    test_round_trip();
    test_zero_length();
    test_size_boundary();
    test_chunked_peer();
    test_eintr();
    test_truncated_peer();
    test_send_timeout();
    test_ipc_request_ok();
    test_ipc_request_no_server();
    test_ipc_request_timeout();
    test_ipc_request_too_large();
    test_connect_timeout();

    printf(failures ? "\n%d CHECK(S) FAILED\n" : "\nALL CHECKS PASSED\n", failures);
    return failures ? 1 : 0;
}
