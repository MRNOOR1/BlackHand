/*
 * ============================================================================
 * BlackHand OS — ipc.h  (IPC library public header)
 * ============================================================================
 *
 * WHAT IS IPC?
 * ------------
 * IPC stands for Inter-Process Communication. It is the mechanism that lets
 * separate processes talk to each other. In BlackHand, every service runs as
 * its own independent process — blackhand-audio, blackhand-storage, blackhand-
 * modem, blackhand-ui — and they have no shared memory. The only way they can
 * exchange data is through IPC.
 *
 * WHY DO WE NEED IT?
 * ------------------
 * Each service owns one responsibility and runs in isolation. When the user
 * presses play in the UI, blackhand-ui cannot directly call a function inside
 * blackhand-audio — they are separate processes with separate memory spaces.
 * Instead blackhand-ui sends a message to blackhand-audio over a socket. That
 * message crosses the process boundary safely, the audio service receives it,
 * and plays the audio.
 *
 * This isolation is intentional. If blackhand-audio crashes, the UI stays up.
 * If blackhand-ui crashes, audio keeps playing. Each service fails independently
 * without taking down the whole system.
 *
 * HOW DOES IT WORK IN BLACKHAND?
 * --------------------------------
 * BlackHand uses Unix domain sockets as the transport layer. A Unix socket is
 * a file on the filesystem (in /run/) that two processes use as a communication
 * endpoint — like a telephone line between two rooms in the same building.
 *
 * Each backend service creates its own socket file on startup:
 *
 *   blackhand-audio    → /run/bh-audio.sock
 *   blackhand-storage  → /run/bh-storage.sock
 *   blackhand-modem    → /run/bh-modem.sock
 *
 * The UI connects to these sockets as a client whenever it needs something.
 *
 * THE PROTOCOL — JSON-RPC 2.0
 * ----------------------------
 * Messages follow the JSON-RPC 2.0 format — a lightweight standard for
 * making remote procedure calls using JSON. Every request has:
 *
 *   { "jsonrpc": "2.0", "method": "play", "params": {...}, "id": 1 }
 *
 * Every response has:
 *
 *   { "jsonrpc": "2.0", "result": "ok", "id": 1 }
 *
 * The "id" field lets the client match each response to the request that
 * caused it, which is important when multiple requests are in flight.
 *
 * MESSAGE FRAMING
 * ---------------
 * JSON strings have no fixed length — the receiver cannot know how many
 * bytes to read without help. BlackHand solves this with a 4-byte length
 * prefix: every message is sent as [4 bytes: message length][JSON bytes].
 *
 * The receiver reads 4 bytes first, converts them to a number (ntohl for
 * byte order), then reads exactly that many bytes. This is implemented in
 * ipc_framing.c via send_msg() and recv_msg().
 *
 * THE DISPATCH TABLE
 * ------------------
 * On the server side, each service maintains a table mapping method names
 * to handler functions. When a request arrives, the dispatch function looks
 * up the method name in the table and calls the matching handler. Adding a
 * new command is one line in the table plus one new function — no if/else
 * chains needed. Implemented in ipc_dispatch.c.
 *
 * FILE STRUCTURE OF blackhand-ipc
 * --------------------------------
 *
 *   ipc.h            — this file. High-level API: server setup and client
 *                       connect helpers. The stable public interface that all
 *                       services include.
 *
 *   ipc.c            — implements ipc_server_listen() and ipc_client_connect().
 *                       Wraps the raw socket setup (socket/bind/listen/connect)
 *                       into two clean one-call functions.
 *
 *   ipc_framing.h/c  — send_msg() and recv_msg(). Handles the 4-byte length
 *                       prefix framing. Services call these instead of raw
 *                       send()/recv(). Has no knowledge of JSON.
 *
 *   ipc_dispatch.h/c — dispatch(), send_error(), and handler functions.
 *                       Parses incoming JSON-RPC requests, validates jsonrpc
 *                       version and id, looks up the method in the dispatch
 *                       table, calls the handler. Services add their own
 *                       handlers here (handle_play, handle_pause etc).
 *
 *   cJSON.h/c        — third-party JSON library (DaveGamble/cJSON).
 *                       Provides JSON object creation, serialisation, parsing,
 *                       and field access. Used by dispatch and all handlers.
 *
 * RELATIONSHIP TO init AND service_manager
 * -----------------------------------------
 * init.c mounts /run as a tmpfs. Without that mount, socket files cannot
 * be created and IPC cannot function at all. init.c is therefore the
 * prerequisite for all IPC in the system.
 *
 * service_manager.c starts the processes that use IPC but has no direct
 * involvement in the IPC protocol itself. It only manages process lifecycle.
 * Once a service is running, IPC is entirely between that service and its
 * clients — the service manager is not involved.
 *
 * LAYER DIAGRAM
 * -------------
 *
 *   blackhand-ui  ──────────────────────────────────────────────────────┐
 *        │  calls ipc_client_connect("/run/bh-audio.sock")              │
 *        │  calls send_msg(fd, json_string)                             │
 *        │  calls recv_msg(fd, buffer)                                  │
 *        │  parses response JSON                                        │
 *        ▼                                                              │
 *   [ /run/bh-audio.sock ]   ← Unix socket file, created by audio      │
 *        │                                                              │
 *        ▼                                                              │
 *   blackhand-audio                                                     │
 *        │  calls ipc_server_listen("/run/bh-audio.sock")               │
 *        │  accepts connection                                          │
 *        │  calls recv_msg(client_fd, buffer)                           │
 *        │  calls dispatch(client_fd, parsed_json)                     │
 *        │      → dispatch looks up "play" in table                    │
 *        │      → calls handle_play(fd, req)                           │
 *        │          → ALSA plays audio                                 │
 *        │          → send_msg(fd, response_json)                      │
 *        └──────────────────────────────────────────────────────────────┘
 *
 * ============================================================================
 */

#pragma once

/*
 * ipc_server_listen — create a Unix domain socket server at path.
 *
 * Performs the full server setup sequence in one call:
 *   socket() → unlink() → bind() → listen()
 *
 * unlink() removes any stale socket file from a previous run so bind()
 * does not fail with EADDRINUSE if the service was restarted after a crash.
 *
 * path    — filesystem path where the socket file will be created.
 *            e.g. "/run/bh-audio.sock"
 *
 * Returns the listening file descriptor on success.
 * Returns -1 on failure (check errno for the reason).
 *
 * The caller must then call accept() in a loop to receive connections.
 */
int ipc_server_listen(const char *path);

/*
 * ipc_client_connect — connect to a Unix domain socket server at path.
 *
 * Performs the full client setup sequence in one call:
 *   socket() → connect()
 *
 * path    — filesystem path of the server's socket file.
 *            e.g. "/run/bh-audio.sock"
 *
 * Returns the connected file descriptor on success.
 * Returns -1 on failure (the server may not be ready yet — retry with
 * a small delay if ENOENT or ECONNREFUSED is returned).
 *
 * The caller can immediately call send_msg() and recv_msg() on the
 * returned fd to exchange JSON-RPC messages with the server.
 */
int ipc_client_connect(const char *path);
