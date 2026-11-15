#ifndef MODEM_DISPATCH_H
#define MODEM_DISPATCH_H

// Parse one JSON-RPC request from `json` and write a JSON-RPC response
// back on `client_fd` via send_msg(). Handles unknown methods and
// missing/invalid params by emitting an error response — never blocks
// waiting for the socket.
void modem_dispatch_handle(int client_fd, const char *json);

#endif
