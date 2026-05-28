/*
 * blackhand-modem — main service entry point.
 *
 * Starts up:
 *   1. Opens /dev/ttyUSB2 and initialises the modem (AT commands).
 *   2. Launches a URC monitor thread that tracks incoming calls and new SMS.
 *   3. Listens on /run/bh-modem.sock for JSON-RPC requests from the UI.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>

#include "ipc.h"
#include "ipc_framing.h"
#include "ipc_dispatch.h"
#include "cJSON.h"
#include "modem_uart.h"
#include "modem_core.h"
#include "at_parser.h"

#define MODEM_SOCK      "/run/bh-modem.sock"
#define MODEM_PORT      "/dev/ttyUSB2"
#define BUFFER_SIZE     8192
#define URC_POLL_US     500000   /* 0.5 s between URC polls */

/* ── URC state (accessed from dispatch handlers too) ─────────────────────── */

static pthread_mutex_t g_state_lock = PTHREAD_MUTEX_INITIALIZER;

static int  g_incoming_call_active  = 0;
static char g_incoming_call_number[64] = "";
static int  g_pending_sms_count     = 0;
static int  g_gps_enabled           = 0;
static int  g_signal_rssi           = 0;

/* Accessors used by ipc_dispatch.c */
void modem_state_get(int *incoming, char *number, size_t num_size,
                     int *pending_sms, int *gps_on, int *rssi)
{
    pthread_mutex_lock(&g_state_lock);
    if (incoming)    *incoming    = g_incoming_call_active;
    if (number && num_size > 0) {
        strncpy(number, g_incoming_call_number, num_size - 1);
        number[num_size - 1] = '\0';
    }
    if (pending_sms) *pending_sms = g_pending_sms_count;
    if (gps_on)      *gps_on      = g_gps_enabled;
    if (rssi)        *rssi        = g_signal_rssi;
    pthread_mutex_unlock(&g_state_lock);
}

void modem_state_clear_pending_sms(void)
{
    pthread_mutex_lock(&g_state_lock);
    g_pending_sms_count = 0;
    pthread_mutex_unlock(&g_state_lock);
}

void modem_state_clear_incoming_call(void)
{
    pthread_mutex_lock(&g_state_lock);
    g_incoming_call_active = 0;
    g_incoming_call_number[0] = '\0';
    pthread_mutex_unlock(&g_state_lock);
}

void modem_state_set_gps(int enabled)
{
    pthread_mutex_lock(&g_state_lock);
    g_gps_enabled = enabled;
    pthread_mutex_unlock(&g_state_lock);
}

/* ── URC monitor thread ───────────────────────────────────────────────────── */

static void *urc_monitor_fn(void *arg)
{
    (void)arg;
    char urc[256];

    while (1) {
        while (modem_uart_poll_urc(urc, sizeof(urc))) {
            if (strncmp(urc, "+CMTI:", 6) == 0) {
                /* New SMS stored on SIM: +CMTI: "SM",<idx> */
                pthread_mutex_lock(&g_state_lock);
                g_pending_sms_count++;
                pthread_mutex_unlock(&g_state_lock);

            } else if (strncmp(urc, "+CLIP:", 6) == 0) {
                /* Incoming call with caller ID */
                char num[64] = "";
                at_parse_clip(urc, num, sizeof(num));
                pthread_mutex_lock(&g_state_lock);
                g_incoming_call_active = 1;
                strncpy(g_incoming_call_number, num, sizeof(g_incoming_call_number) - 1);
                pthread_mutex_unlock(&g_state_lock);

            } else if (strcmp(urc, "RING") == 0 ||
                       strncmp(urc, "+CRING:", 7) == 0) {
                /* Incoming ring (may arrive before +CLIP) */
                pthread_mutex_lock(&g_state_lock);
                g_incoming_call_active = 1;
                pthread_mutex_unlock(&g_state_lock);

            } else if (strcmp(urc, "NO CARRIER") == 0) {
                pthread_mutex_lock(&g_state_lock);
                g_incoming_call_active = 0;
                g_incoming_call_number[0] = '\0';
                pthread_mutex_unlock(&g_state_lock);
            }
        }

        /* Periodic signal refresh */
        static int tick = 0;
        if (++tick >= 20) { /* every 20 × 0.5 s = 10 s */
            tick = 0;
            int rssi = 0;
            if (modem_get_signal_strength(&rssi) == 0) {
                pthread_mutex_lock(&g_state_lock);
                g_signal_rssi = rssi;
                pthread_mutex_unlock(&g_state_lock);
            }
        }

        usleep(URC_POLL_US);
    }

    return NULL;
}

/* ── main ─────────────────────────────────────────────────────────────────── */

int main(void)
{
    /* Open modem serial port — retry for up to 30 s at 2-s intervals */
    int modem_ok = 0;
    for (int attempts = 0; attempts < 15; attempts++) {
        if (modem_uart_open(MODEM_PORT) == 0) {
            modem_ok = 1;
            break;
        }
        fprintf(stderr, "blackhand-modem: can't open %s, retry %d/15\n",
                MODEM_PORT, attempts + 1);
        sleep(2);
    }

    if (modem_ok) {
        if (modem_init() == 0) {
            fprintf(stderr, "blackhand-modem: modem ready on %s\n", MODEM_PORT);
        } else {
            fprintf(stderr, "blackhand-modem: modem init failed (continuing)\n");
        }

        /* Start URC monitor in background */
        pthread_t urc_thread;
        pthread_create(&urc_thread, NULL, urc_monitor_fn, NULL);
        pthread_detach(urc_thread);
    } else {
        fprintf(stderr, "blackhand-modem: no modem found, running without hardware\n");
    }

    /* IPC socket */
    int server_fd = ipc_server_listen(MODEM_SOCK);
    if (server_fd < 0) {
        perror("blackhand-modem: ipc_server_listen failed");
        return EXIT_FAILURE;
    }

    fprintf(stderr, "blackhand-modem: listening on " MODEM_SOCK "\n");

    while (1) {
        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) {
            perror("blackhand-modem: accept");
            continue;
        }

        while (1) {
            char buffer[BUFFER_SIZE];
            int n = recv_msg(client_fd, buffer, BUFFER_SIZE);
            if (n <= 0) break;

            cJSON *req = cJSON_Parse(buffer);
            if (!req) break;

            dispatch(client_fd, req);
            cJSON_Delete(req);
        }

        close(client_fd);
    }

    return 0;
}
