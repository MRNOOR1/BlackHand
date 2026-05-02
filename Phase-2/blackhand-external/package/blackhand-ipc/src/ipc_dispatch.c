/*
 * ipc_dispatch.c — TEMPLATE / REFERENCE ONLY. Not compiled into libblackhand-ipc.a.
 *
 * Each service copies this file into its own src/ directory and adds its own
 * handlers to the dispatch table. The library (blackhand-ipc) only provides
 * the transport layer — framing, send_msg, recv_msg, cJSON. The dispatch table
 * is service-specific and must live inside the service binary, not the library.
 *
 * Why? ipc_dispatch.c for blackhand-audio needs to include audio_alsa.h and call
 * audio_play(), audio_stop() etc. Those symbols don't exist in blackhand-ipc.
 * Putting service-specific code in the shared library would create a circular
 * dependency and prevent the library from building independently.
 *
 * To use: copy this file into your service's src/ directory, add your own
 * handler forward declarations, implement them, and add entries to the table.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ipc_dispatch.h"
#include "cJSON.h"
#include "ipc_framing.h"

/* ── private handler forward declarations ── */
static void handle_ping(int fd, cJSON *req);
static void handle_echo(int fd, cJSON *req);

/* ── dispatch table ── add your service handlers here ── */
struct handler {
    const char *method;
    void (*fn)(int, cJSON *);
};

static struct handler table[] = {
    {"ping", handle_ping},
    {"echo", handle_echo},
    /* add service-specific entries here */
    {NULL, NULL}
};

/* ── dispatch() — called from main.c for every incoming request ── */
void dispatch(int fd, cJSON *req)
{
    cJSON *version_item = cJSON_GetObjectItem(req, "jsonrpc");
    if (!version_item || !cJSON_IsString(version_item) ||
        strcmp(version_item->valuestring, "2.0") != 0) {
        send_error(fd, "invalid or missing jsonrpc version");
        return;
    }

    cJSON *id = cJSON_GetObjectItem(req, "id");
    if (!id || !cJSON_IsNumber(id)) {
        send_error(fd, "missing or invalid id");
        return;
    }

    cJSON *method_item = cJSON_GetObjectItem(req, "method");
    if (!method_item || !cJSON_IsString(method_item)) {
        send_error(fd, "missing method");
        return;
    }

    const char *method = method_item->valuestring;

    for (int i = 0; table[i].method != NULL; i++) {
        if (strcmp(table[i].method, method) == 0) {
            table[i].fn(fd, req);
            return;
        }
    }

    send_error(fd, "unknown method");
}

/* ── send_error() — sends a JSON-RPC error response ── */
void send_error(int fd, const char *message)
{
    cJSON *res = cJSON_CreateObject();
    cJSON_AddStringToObject(res, "error", message);
    char *str = cJSON_PrintUnformatted(res);
    send_msg(fd, str);
    free(str);
    cJSON_Delete(res);
}

/* ── built-in handlers ── */
static void handle_ping(int fd, cJSON *req)
{
    int id = cJSON_GetObjectItem(req, "id")->valueint;
    cJSON *res = cJSON_CreateObject();
    cJSON_AddStringToObject(res, "jsonrpc", "2.0");
    cJSON_AddStringToObject(res, "result", "pong");
    cJSON_AddNumberToObject(res, "id", id);
    char *str = cJSON_PrintUnformatted(res);
    send_msg(fd, str);
    free(str);
    cJSON_Delete(res);
}

static void handle_echo(int fd, cJSON *req)
{
    int id = cJSON_GetObjectItem(req, "id")->valueint;
    cJSON *res = cJSON_CreateObject();
    cJSON_AddStringToObject(res, "jsonrpc", "2.0");
    cJSON_AddStringToObject(res, "result", "this is a test echo");
    cJSON_AddNumberToObject(res, "id", id);
    char *str = cJSON_PrintUnformatted(res);
    send_msg(fd, str);
    free(str);
    cJSON_Delete(res);
}
