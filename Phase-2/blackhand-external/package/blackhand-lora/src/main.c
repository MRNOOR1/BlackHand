/*
 * blackhand-lora — LoRa text messaging service.
 *
 * Drives an RFM95W (SX1276) over /dev/spidev0.0 and exposes send/receive
 * over a Unix socket. A background thread polls the radio for incoming
 * packets (DIO0 is not wired) and queues them; IPC clients pop them.
 *
 * The socket server always starts, even if the radio failed to initialise,
 * so the UI can query lora_status and see "ready": false rather than the
 * service disappearing. sx1276_send / the RX thread are simply inert until
 * the radio is present.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "cJSON.h"
#include "ipc_framing.h"
#include "ipc_dispatch.h"
#include "sx1276.h"
#include "rx_queue.h"

#define SOCKET_PATH "/run/bh-lora.sock"
#define BUFFER_SIZE 8192

/* Background RX: poll RegIrqFlags ~50 Hz and queue any received packet. */
static void *rx_thread(void *arg)
{
    (void)arg;
    unsigned char buf[SX1276_MAX_PAYLOAD];
    struct timespec period = { 0, 20 * 1000000L };   /* 20 ms */

    while (1) {
        int len = sx1276_recv_poll(buf, sizeof(buf));
        if (len > 0)
            rxq_push(buf, (size_t)len);
        nanosleep(&period, NULL);
    }
    return NULL;
}

int main(void)
{
    /* Initialise the radio. Non-fatal on failure: the service keeps serving
       status so callers can distinguish "no radio" from "no service". */
    if (sx1276_init() != 0)
        fprintf(stderr, "lora: radio init failed; serving status only\n");

    /* Only run the poll thread when the radio is actually present. */
    if (sx1276_ready()) {
        pthread_t tid;
        if (pthread_create(&tid, NULL, rx_thread, NULL) == 0)
            pthread_detach(tid);
        else
            fprintf(stderr, "lora: could not start RX thread\n");
    }

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    unlink(SOCKET_PATH);

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    if (listen(fd, 5) == -1) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    printf("Server listening on %s\n", SOCKET_PATH);

    while (1) {
        int client_fd = ipc_server_accept(fd);
        if (client_fd == -1) {
            perror("accept");
            continue;
        }

        while (1) {
            char buffer[BUFFER_SIZE];
            int n = recv_msg(client_fd, buffer, BUFFER_SIZE);
            if (n == -1)
                break;

            cJSON *req = cJSON_Parse(buffer);
            if (!req)
                break;

            dispatch(client_fd, req);
            cJSON_Delete(req);
        }

        close(client_fd);
    }

    return 0;
}
