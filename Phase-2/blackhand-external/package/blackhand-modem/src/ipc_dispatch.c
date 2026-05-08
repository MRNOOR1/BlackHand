/*
 * ipc_dispatch.c — blackhand-modem service dispatch table.
 *
 * JSON-RPC methods exposed by this service:
 *   ping         — returns "pong"
 *   get_calls    — returns call log as a JSON array
 *   get_messages — returns SMS inbox as a JSON array
 *   dial         — initiate an outgoing call
 *   send_sms     — send an SMS message
 *   hangup       — hang up current call
 *
 * Phase 2: handlers return stubs (empty lists, "ok").
 * Phase 3: replace stub bodies with real AT command calls via at_parser.c.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ipc_dispatch.h"
#include "ipc_framing.h"
#include "cJSON.h"

/* ── private handler forward declarations ── */
static void handle_ping(int fd, cJSON *req);
static void handle_get_calls(int fd, cJSON *req);
static void handle_get_messages(int fd, cJSON *req);
static void handle_dial(int fd, cJSON *req);
static void handle_send_sms(int fd, cJSON *req);
static void handle_hangup(int fd, cJSON *req);

/* ── dispatch table ── */
struct handler {
    const char *method;
    void (*fn)(int, cJSON *);
};

static struct handler table[] = {
    {"ping",         handle_ping},
    {"get_calls",    handle_get_calls},
    {"get_messages", handle_get_messages},
    {"dial",         handle_dial},
    {"send_sms",     handle_send_sms},
    {"hangup",       handle_hangup},
    {NULL, NULL}
};

/* ── helpers ── */

static void send_result_str(int fd, cJSON *req, const char *result_str)
{
    cJSON *id_item = cJSON_GetObjectItem(req, "id");
    if (!id_item || !cJSON_IsNumber(id_item)) {
        send_error(fd, "invalid id");
        return;
    }
    cJSON *res = cJSON_CreateObject();
    cJSON_AddStringToObject(res, "jsonrpc", "2.0");
    cJSON_AddStringToObject(res, "result", result_str);
    cJSON_AddNumberToObject(res, "id", id_item->valueint);
    char *str = cJSON_PrintUnformatted(res);
    send_msg(fd, str);
    free(str);
    cJSON_Delete(res);
}

static void send_result_json(int fd, cJSON *req, cJSON *result_obj)
{
    cJSON *id_item = cJSON_GetObjectItem(req, "id");
    if (!id_item || !cJSON_IsNumber(id_item)) {
        send_error(fd, "invalid id");
        cJSON_Delete(result_obj);
        return;
    }
    cJSON *res = cJSON_CreateObject();
    cJSON_AddStringToObject(res, "jsonrpc", "2.0");
    cJSON_AddItemToObject(res, "result", result_obj);
    cJSON_AddNumberToObject(res, "id", id_item->valueint);
    char *str = cJSON_PrintUnformatted(res);
    send_msg(fd, str);
    free(str);
    cJSON_Delete(res);
}

/* ── dispatch() ── */
void dispatch(int fd, cJSON *req)
{
    cJSON *version = cJSON_GetObjectItem(req, "jsonrpc");
    if (!version || !cJSON_IsString(version) ||
        strcmp(version->valuestring, "2.0") != 0) {
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

/* ── send_error() ── */
void send_error(int fd, const char *message)
{
    cJSON *res = cJSON_CreateObject();
    cJSON_AddStringToObject(res, "error", message);
    char *str = cJSON_PrintUnformatted(res);
    send_msg(fd, str);
    free(str);
    cJSON_Delete(res);
}

/* ── handlers ── */

static void handle_ping(int fd, cJSON *req)
{
    send_result_str(fd, req, "pong");
}

static void handle_get_calls(int fd, cJSON *req)
{
    /*
     * Phase 3: query AT+CLCC or read call log from SIM.
     * For now return an empty array so the UI gets valid JSON.
     */
    cJSON *arr = cJSON_CreateArray();
    send_result_json(fd, req, arr);
}

static void handle_get_messages(int fd, cJSON *req)
{
    /*
     * Phase 3: query AT+CMGL="ALL" and parse PDU/text responses.
     * For now return an empty array so the UI gets valid JSON.
     */
    cJSON *arr = cJSON_CreateArray();
    send_result_json(fd, req, arr);
}

static void handle_dial(int fd, cJSON *req)
{
    cJSON *params = cJSON_GetObjectItem(req, "params");
    if (!params || !cJSON_IsObject(params)) {
        send_error(fd, "missing params");
        return;
    }
    cJSON *number = cJSON_GetObjectItem(params, "number");
    if (!number || !cJSON_IsString(number)) {
        send_error(fd, "missing number");
        return;
    }
    /* Phase 3: send ATD<number>; */
    send_result_str(fd, req, "ok");
}

static void handle_send_sms(int fd, cJSON *req)
{
    cJSON *params = cJSON_GetObjectItem(req, "params");
    if (!params || !cJSON_IsObject(params)) {
        send_error(fd, "missing params");
        return;
    }
    cJSON *number = cJSON_GetObjectItem(params, "number");
    cJSON *body   = cJSON_GetObjectItem(params, "body");
    if (!number || !cJSON_IsString(number) || !body || !cJSON_IsString(body)) {
        send_error(fd, "missing number or body");
        return;
    }
    /* Phase 3: AT+CMGS="<number>" then body then Ctrl-Z */
    send_result_str(fd, req, "ok");
}

static void handle_hangup(int fd, cJSON *req)
{
    /* Phase 3: send ATH */
    send_result_str(fd, req, "ok");
}
