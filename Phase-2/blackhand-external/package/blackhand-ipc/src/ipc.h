/*
 * ipc.h — BlackHand IPC library public header
 *
 * Single include for everything IPC-related. Include this in any service
 * or client that needs to communicate over Unix domain sockets.
 *
 * ARCHITECTURE
 * ─────────────
 * Each service owns one socket file in /run/:
 *
 *   blackhand-audio   → /run/bh-audio.sock
 *   blackhand-modem   → /run/bh-modem.sock
 *   blackhand-stt     → /run/bh-stt.sock
 *
 * Services are SERVERS — they call ipc_server_listen() then accept().
 * The UI and other callers are CLIENTS — they call ipc_client_connect()
 * or ipc_request() to send one command and get a response back.
 *
 * WIRE FORMAT
 * ────────────
 * Every message is: [4 bytes big-endian length][JSON payload]
 * Use send_msg() and recv_msg() — never call send()/recv() directly.
 *
 * PROTOCOL
 * ─────────
 * JSON-RPC 2.0. Every request:
 *   { "jsonrpc": "2.0", "method": "play", "params": {...}, "id": 1 }
 * Every response:
 *   { "jsonrpc": "2.0", "result": "ok", "id": 1 }
 *
 * DISPATCH TABLE (server side)
 * ─────────────────────────────
 * Copy ipc_dispatch.c into your service's src/ directory and fill in your
 * own handler table. Do not put service-specific handlers in this library.
 */

#pragma once

#include <stddef.h>

/* ── Error codes ──────────────────────────────────────────────────────────
 *
 * All functions that can fail return IPC_OK (0) on success or one of these
 * negative codes on failure. Functions that return a file descriptor return
 * the fd (>= 0) on success and a negative error code on failure — so a
 * simple (fd < 0) check works for both.
 */
#define IPC_OK               0
#define IPC_ERR_INVALID_ARG -1
#define IPC_ERR_SOCKET      -2
#define IPC_ERR_CONNECT     -3
#define IPC_ERR_SEND        -4
#define IPC_ERR_RECV        -5
#define IPC_ERR_TOO_LARGE   -6
#define IPC_ERR_BIND        -7
#define IPC_ERR_LISTEN      -8

/* ── Server ───────────────────────────────────────────────────────────────
 *
 * ipc_server_listen — create a Unix domain socket server at path.
 *
 * Does the full setup in one call: socket → unlink → bind → listen.
 * unlink() removes any stale socket from a previous crash so bind()
 * never fails with EADDRINUSE on restart.
 *
 * Returns the listening fd on success, or a negative IPC_ERR_* code.
 * The caller must then call accept() in a loop to receive connections.
 */
int ipc_server_listen(const char *path);

/* ── Client ───────────────────────────────────────────────────────────────
 *
 * ipc_client_connect — connect to a Unix domain socket server at path.
 *
 * Returns the connected fd on success, or a negative IPC_ERR_* code.
 * On IPC_ERR_CONNECT the server is not ready yet — retry with a small delay.
 * The caller calls send_msg() / recv_msg() on the returned fd directly.
 */
int ipc_client_connect(const char *path);

/*
 * ipc_request — one-shot: connect → send → receive → close.
 *
 * The most convenient client call. Handles the entire round trip and closes
 * the connection when done. The JSON response is written into buf as a
 * null-terminated string.
 *
 * Returns IPC_OK on success, or a negative IPC_ERR_* code on failure.
 */
int ipc_request(const char *path, const char *json,
                char *buf, size_t buf_size);

/* ── Transport ────────────────────────────────────────────────────────────
 *
 * Use these on any open fd (client or server side) to exchange messages.
 *
 * send_msg — send a null-terminated JSON string with a 4-byte length prefix.
 *   Returns 0 on success, -1 on failure.
 *
 * recv_msg — read a length-prefixed message into buffer.
 *   Returns the number of bytes read on success, -1 on failure.
 *   buffer is always null-terminated on success.
 *
 * ipc_close — close an fd safely. No-op if fd < 0.
 */
int  send_msg(int fd, const char *msg);
int  recv_msg(int fd, char *buffer, int buffer_size);
void ipc_close(int fd);
