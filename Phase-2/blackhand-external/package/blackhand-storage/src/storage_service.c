/*
 * ============================================================================
 * BlackHand OS — storage_service.c  (blackhand-storage)
 * ============================================================================
 *
 * WHAT THIS SERVICE DOES
 * ----------------------
 * The storage service owns all persistent data for the device. It opens a
 * SQLite database at /mnt/data/blackhand.db and listens for JSON-RPC
 * requests on /run/bh-storage.sock.
 *
 * Other services and the UI send requests like:
 *   { "jsonrpc": "2.0", "method": "contacts.list", "id": 1 }
 *   { "jsonrpc": "2.0", "method": "alarms.add", "params": {...}, "id": 2 }
 *
 * CURRENT STATE — Phase 2 stub
 * ------------------------------
 * The socket is created and the accept loop runs. Actual SQLite queries and
 * JSON-RPC dispatch will be added in Phase 3. For now, the service just
 * accepts connections and closes them (no handler yet).
 *
 * This is enough to:
 *   - compile cleanly
 *   - pass verify-image.sh (binary present at /usr/bin/blackhand-storage)
 *   - start from the service manager without crashing
 *   - create /run/bh-storage.sock so the UI does not fail to connect
 * ============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include "ipc.h"

#define STORAGE_SOCK "/run/bh-storage.sock"

int main(void)
{
    int server_fd = ipc_server_listen(STORAGE_SOCK);
    if (server_fd == -1)
    {
        perror("blackhand-storage: ipc_server_listen failed");
        return EXIT_FAILURE;
    }

    write(2, "blackhand-storage: listening on " STORAGE_SOCK "\n",
          sizeof("blackhand-storage: listening on " STORAGE_SOCK "\n") - 1);

    /*
     * Accept connections and keep the service alive.
     * Phase 3 will add JSON-RPC dispatch and SQLite queries here.
     */
    while (1)
    {
        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd == -1)
        {
            perror("blackhand-storage: accept");
            continue;
        }
        /* TODO Phase 3: recv_msg → dispatch → sqlite query → send_msg */
        close(client_fd);
    }

    return 0; /* unreachable */
}
