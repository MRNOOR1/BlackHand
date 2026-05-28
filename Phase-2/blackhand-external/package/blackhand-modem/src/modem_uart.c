#include "modem_uart.h"

#include <termios.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <time.h>

#define URC_QUEUE_SIZE  16
#define URC_LINE_MAX    256
#define RESP_BUF_MAX    4096
#define LINE_MAX_LEN    512

/* Known URC prefixes — lines starting with these are routed to the URC queue
 * regardless of whether a command response is pending. */
static const char *URC_PREFIXES[] = {
    "+CMTI:", "+CLIP:", "+CREG:", "+CIEV:", "+CUSD:",
    "+CRING:", "RING",  "+CLCC:", "+CDSI:", "NO CARRIER",
    NULL
};

static struct {
    int      fd;
    volatile int running;
    pthread_t reader_thread;

    /* Serialises AT commands — only one command outstanding at a time */
    pthread_mutex_t cmd_lock;

    /* Protects resp_buf / resp_done / resp_ok */
    pthread_mutex_t resp_lock;
    pthread_cond_t  resp_ready;
    int  resp_done;
    int  resp_ok;
    char resp_buf[RESP_BUF_MAX];
    int  resp_len;

    /* URC circular queue */
    pthread_mutex_t urc_lock;
    char urc_queue[URC_QUEUE_SIZE][URC_LINE_MAX];
    int  urc_head;
    int  urc_tail;
} s;

/* ── helpers ─────────────────────────────────────────────────────────────── */

static int is_urc(const char *line)
{
    for (int i = 0; URC_PREFIXES[i]; i++) {
        size_t plen = strlen(URC_PREFIXES[i]);
        if (strncmp(line, URC_PREFIXES[i], plen) == 0)
            return 1;
    }
    return 0;
}

static int is_final(const char *line)
{
    return strcmp(line, "OK") == 0 ||
           strcmp(line, "ERROR") == 0 ||
           strncmp(line, "+CME ERROR:", 11) == 0 ||
           strncmp(line, "+CMS ERROR:", 11) == 0 ||
           strcmp(line, "NO CARRIER") == 0 ||
           strcmp(line, "BUSY") == 0 ||
           strcmp(line, "NO ANSWER") == 0;
}

static void enqueue_urc(const char *line)
{
    pthread_mutex_lock(&s.urc_lock);
    int next = (s.urc_tail + 1) % URC_QUEUE_SIZE;
    if (next != s.urc_head) {
        strncpy(s.urc_queue[s.urc_tail], line, URC_LINE_MAX - 1);
        s.urc_queue[s.urc_tail][URC_LINE_MAX - 1] = '\0';
        s.urc_tail = next;
    }
    pthread_mutex_unlock(&s.urc_lock);
}

static void route_line(const char *line)
{
    if (*line == '\0') return;

    if (is_urc(line)) {
        enqueue_urc(line);
        return;
    }

    pthread_mutex_lock(&s.resp_lock);
    if (!s.resp_done) {
        if (is_final(line)) {
            s.resp_ok   = (strcmp(line, "OK") == 0) ? 1 : 0;
            s.resp_done = 1;
            pthread_cond_signal(&s.resp_ready);
        } else {
            /* skip echo line (no leading '+' and no space) */
            int avail = RESP_BUF_MAX - s.resp_len - 1;
            if (avail > 0) {
                int n = snprintf(s.resp_buf + s.resp_len, avail + 1,
                                 "%s\n", line);
                if (n > 0 && n <= avail)
                    s.resp_len += n;
            }
        }
    } else {
        enqueue_urc(line);
    }
    pthread_mutex_unlock(&s.resp_lock);
}

/* ── reader thread ────────────────────────────────────────────────────────── */

static void *reader_thread_fn(void *arg)
{
    (void)arg;
    char line[LINE_MAX_LEN];
    int  pos = 0;
    unsigned char ch;

    while (s.running) {
        int n = (int)read(s.fd, &ch, 1);
        if (n <= 0) {
            if (n < 0 && (errno == EINTR || errno == EAGAIN)) {
                usleep(1000);
                continue;
            }
            break;
        }
        if (ch == '\r') continue;
        if (ch == '\n') {
            line[pos] = '\0';
            if (pos > 0)
                route_line(line);
            pos = 0;
        } else if (pos < (int)sizeof(line) - 1) {
            line[pos++] = (char)ch;
        }
    }
    return NULL;
}

/* ── public API ───────────────────────────────────────────────────────────── */

int modem_uart_open(const char *port)
{
    memset(&s, 0, sizeof(s));
    s.fd = -1;

    s.fd = open(port, O_RDWR | O_NOCTTY);
    if (s.fd < 0) {
        perror("modem_uart_open: open");
        return -1;
    }

    struct termios tio;
    if (tcgetattr(s.fd, &tio) < 0) {
        perror("modem_uart_open: tcgetattr");
        close(s.fd);
        s.fd = -1;
        return -1;
    }
    cfmakeraw(&tio);
    cfsetispeed(&tio, B115200);
    cfsetospeed(&tio, B115200);
    tio.c_cflag |= (CLOCAL | CREAD);
    tio.c_cflag &= ~CRTSCTS;
    tio.c_cc[VMIN]  = 1;
    tio.c_cc[VTIME] = 0;
    if (tcsetattr(s.fd, TCSANOW, &tio) < 0) {
        perror("modem_uart_open: tcsetattr");
        close(s.fd);
        s.fd = -1;
        return -1;
    }
    tcflush(s.fd, TCIOFLUSH);

    pthread_mutex_init(&s.cmd_lock, NULL);
    pthread_mutex_init(&s.resp_lock, NULL);
    pthread_cond_init(&s.resp_ready, NULL);
    pthread_mutex_init(&s.urc_lock, NULL);

    s.running = 1;
    if (pthread_create(&s.reader_thread, NULL, reader_thread_fn, NULL) != 0) {
        perror("modem_uart_open: pthread_create");
        close(s.fd);
        s.fd = -1;
        return -1;
    }

    return 0;
}

int at_command(const char *cmd, char *response, size_t resp_size, int timeout_ms)
{
    if (s.fd < 0 || !s.running) return -1;

    pthread_mutex_lock(&s.cmd_lock);

    pthread_mutex_lock(&s.resp_lock);
    s.resp_buf[0] = '\0';
    s.resp_len    = 0;
    s.resp_done   = 0;
    s.resp_ok     = 0;
    pthread_mutex_unlock(&s.resp_lock);

    char cmdline[512];
    int n = snprintf(cmdline, sizeof(cmdline), "%s\r", cmd);
    if (n < 0 || (size_t)n >= sizeof(cmdline) ||
        write(s.fd, cmdline, (size_t)n) != n) {
        pthread_mutex_unlock(&s.cmd_lock);
        return -1;
    }

    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec  += timeout_ms / 1000;
    deadline.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000000000L;
    }

    pthread_mutex_lock(&s.resp_lock);
    while (!s.resp_done) {
        if (pthread_cond_timedwait(&s.resp_ready, &s.resp_lock, &deadline) == ETIMEDOUT)
            break;
    }
    int ok = s.resp_ok;
    if (response && resp_size > 0) {
        strncpy(response, s.resp_buf, resp_size - 1);
        response[resp_size - 1] = '\0';
    }
    pthread_mutex_unlock(&s.resp_lock);

    pthread_mutex_unlock(&s.cmd_lock);
    return ok ? 0 : -1;
}

int modem_uart_sms_send(const char *number, const char *body)
{
    if (s.fd < 0 || !number || !body) return -1;

    pthread_mutex_lock(&s.cmd_lock);

    pthread_mutex_lock(&s.resp_lock);
    s.resp_buf[0] = '\0';
    s.resp_len    = 0;
    s.resp_done   = 0;
    s.resp_ok     = 0;
    pthread_mutex_unlock(&s.resp_lock);

    char cmd[128];
    int n = snprintf(cmd, sizeof(cmd), "AT+CMGS=\"%s\"\r", number);
    if (n < 0 || (size_t)n >= sizeof(cmd) ||
        write(s.fd, cmd, (size_t)n) != n) {
        pthread_mutex_unlock(&s.cmd_lock);
        return -1;
    }

    /* Wait for > prompt — the modem sends \r\n> without a trailing \n
     * so our reader can't process it as a line; we just sleep here. */
    usleep(400000);

    char body_with_ctrlz[512];
    n = snprintf(body_with_ctrlz, sizeof(body_with_ctrlz), "%s\x1A", body);
    if (n < 0 || (size_t)n >= sizeof(body_with_ctrlz) ||
        write(s.fd, body_with_ctrlz, (size_t)n) != n) {
        pthread_mutex_unlock(&s.cmd_lock);
        return -1;
    }

    /* SMS can take up to 15 s */
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += 15;

    pthread_mutex_lock(&s.resp_lock);
    while (!s.resp_done) {
        if (pthread_cond_timedwait(&s.resp_ready, &s.resp_lock, &deadline) == ETIMEDOUT)
            break;
    }
    int ok = s.resp_ok;
    pthread_mutex_unlock(&s.resp_lock);

    pthread_mutex_unlock(&s.cmd_lock);
    return ok ? 0 : -1;
}

int modem_uart_write_raw(const unsigned char *data, size_t len)
{
    if (s.fd < 0) return -1;
    return (write(s.fd, data, len) == (ssize_t)len) ? 0 : -1;
}

int modem_uart_poll_urc(char *urc_buf, size_t urc_size)
{
    pthread_mutex_lock(&s.urc_lock);
    if (s.urc_head == s.urc_tail) {
        pthread_mutex_unlock(&s.urc_lock);
        return 0;
    }
    strncpy(urc_buf, s.urc_queue[s.urc_head], urc_size - 1);
    urc_buf[urc_size - 1] = '\0';
    s.urc_head = (s.urc_head + 1) % URC_QUEUE_SIZE;
    pthread_mutex_unlock(&s.urc_lock);
    return 1;
}

void modem_uart_close(void)
{
    s.running = 0;
    if (s.fd >= 0) {
        close(s.fd);
        s.fd = -1;
    }
    pthread_join(s.reader_thread, NULL);
    pthread_mutex_destroy(&s.cmd_lock);
    pthread_mutex_destroy(&s.resp_lock);
    pthread_cond_destroy(&s.resp_ready);
    pthread_mutex_destroy(&s.urc_lock);
}
