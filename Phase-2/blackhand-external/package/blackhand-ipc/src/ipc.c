/*
 * IPC library implementation.
 * Provide helpers for:
 * - create Unix domain socket server
 * - connect to a service
 * - send/receive JSON-RPC frames
 *
 * Example API ideas:
 * int ipc_server_listen(const char *path);
 * int ipc_client_connect(const char *path);
 */
