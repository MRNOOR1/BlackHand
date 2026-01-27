/*
 * IPC library public header.
 * Keep this API stable so all services can share it.
 */

#pragma once

int ipc_server_listen(const char *path);
int ipc_client_connect(const char *path);
