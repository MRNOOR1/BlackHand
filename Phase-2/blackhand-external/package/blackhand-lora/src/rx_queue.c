/*
 * rx_queue.c — thread-safe ring buffer of received LoRa messages.
 * See rx_queue.h. Mutex-protected; drop-oldest on overflow.
 */
#include "rx_queue.h"

#include <string.h>
#include <pthread.h>

struct msg {
    unsigned char data[RXQ_MSG_MAX];
    size_t len;
};

static struct msg      g_ring[RXQ_CAPACITY];
static int             g_head = 0;   /* next pop */
static int             g_count = 0;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

void rxq_push(const unsigned char *data, size_t len)
{
    if (len > RXQ_MSG_MAX)
        len = RXQ_MSG_MAX;

    pthread_mutex_lock(&g_lock);

    int tail = (g_head + g_count) % RXQ_CAPACITY;
    if (g_count == RXQ_CAPACITY) {
        /* Full: overwrite the oldest by advancing head. */
        g_head = (g_head + 1) % RXQ_CAPACITY;
    } else {
        g_count++;
    }
    memcpy(g_ring[tail].data, data, len);
    /* Untrusted OTA bytes: strip C0 controls + DEL so LoRa text can't inject
     * terminal escapes into the UI. UTF-8 (>= 0x80) is preserved. */
    for (size_t i = 0; i < len; i++)
        if (g_ring[tail].data[i] < 0x20 || g_ring[tail].data[i] == 0x7F)
            g_ring[tail].data[i] = '?';
    g_ring[tail].len = len;

    pthread_mutex_unlock(&g_lock);
}

int rxq_pop(char *out, size_t out_size)
{
    int len = -1;

    pthread_mutex_lock(&g_lock);
    if (g_count > 0) {
        struct msg *m = &g_ring[g_head];
        size_t n = m->len;
        if (n > out_size - 1)
            n = out_size - 1;
        memcpy(out, m->data, n);
        out[n] = '\0';
        len = (int)n;
        g_head = (g_head + 1) % RXQ_CAPACITY;
        g_count--;
    }
    pthread_mutex_unlock(&g_lock);

    return len;
}

int rxq_count(void)
{
    pthread_mutex_lock(&g_lock);
    int c = g_count;
    pthread_mutex_unlock(&g_lock);
    return c;
}
