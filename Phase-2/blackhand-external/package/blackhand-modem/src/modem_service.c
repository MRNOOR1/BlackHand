/*
 * blackhand-modem — main service entry point.
 *
 * Creates /run/bh-modem.sock and dispatches incoming JSON-RPC requests.
 * Phase 2: dispatch stubs return empty data / "ok".
 * Phase 3: handlers in ipc_dispatch.c will issue real AT commands.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include "ipc.h"
#include "ipc_framing.h"
#include "ipc_dispatch.h"
#include "cJSON.h"

#define MODEM_SOCK  "/run/bh-modem.sock"
#define BUFFER_SIZE 4096

int main(void)
{
    int server_fd = ipc_server_listen(MODEM_SOCK);
    if (server_fd == -1) {
        perror("blackhand-modem: ipc_server_listen failed");
        return EXIT_FAILURE;
    }

    fprintf(stderr, "blackhand-modem: listening on " MODEM_SOCK "\n");

    while (1) {
        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd == -1) {
            perror("blackhand-modem: accept");
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
