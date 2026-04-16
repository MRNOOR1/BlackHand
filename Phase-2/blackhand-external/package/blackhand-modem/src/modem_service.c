/*
 * ============================================================================
 * BlackHand OS — modem_service.c  (blackhand-modem)
 * ============================================================================
 *
 * WHAT THIS SERVICE DOES
 * ----------------------
 * The modem service owns the radio modem. It communicates with the modem
 * hardware via AT commands on a serial port (e.g. /dev/ttyUSB0 or
 * /dev/ttyAMA0) and exposes a JSON-RPC API on /run/bh-modem.sock for:
 *
 *   calls — dial, answer, hangup, query call state
 *   sms   — send, receive, list
 *
 * CURRENT STATE — Phase 2 stub
 * ------------------------------
 * The socket is created and the accept loop runs. AT command parsing and
 * SMS handling are implemented in at_parser.c and sms.c (also stubs for
 * now — to be filled in Phase 3).
 *
 * This is enough to:
 *   - compile cleanly
 *   - pass verify-image.sh (binary present at /usr/bin/blackhand-modem)
 *   - start from the service manager without crashing
 *   - create /run/bh-modem.sock so the UI does not fail to connect
 * ============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include "ipc.h"

#define MODEM_SOCK "/run/bh-modem.sock"

int main(void)
{
    int server_fd = ipc_server_listen(MODEM_SOCK);
    if (server_fd == -1)
    {
        perror("blackhand-modem: ipc_server_listen failed");
        return EXIT_FAILURE;
    }

    write(2, "blackhand-modem: listening on " MODEM_SOCK "\n",
          sizeof("blackhand-modem: listening on " MODEM_SOCK "\n") - 1);

    /*
     * Accept connections and keep the service alive.
     * Phase 3 will add AT command dispatch and SMS handling here.
     */
    while (1)
    {
        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd == -1)
        {
            perror("blackhand-modem: accept");
            continue;
        }
        /* TODO Phase 3: recv_msg → dispatch → AT command → send_msg */
        close(client_fd);
    }

    return 0; /* unreachable */
}
