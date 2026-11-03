/*
 * ipc_dispatch.c — blackhand-lora service dispatch table.
 *
 * JSON-RPC methods exposed on /run/bh-lora.sock:
 *   ping        — returns "pong"
 *   lora_send   — transmit text ({"text": "..."}); broadcast, no ACK
 *   lora_pop_rx — pop the oldest received message; returns
 *                 { "text": "...", "len": int } or { "text": "", "len": 0 }
 *                 when the queue is empty
 *   lora_status — returns
 *                 { "ready": bool, "rx_pending": int,
 *                   "freq_hz": int, "rssi": int, "snr": int }
 *
 * Point-to-point broadcast text only — no addressing/ACK/encryption (MVP).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ipc_dispatch.h"
#include "ipc_framing.h"
#include "cJSON.h"
#include "sx1276.h"
#include "rx_queue.h"

static void handle_ping(int fd, cJSON *req);
static void handle_lora_send(int fd, cJSON *req);
static void handle_lora_pop_rx(int fd, cJSON *req);
static void handle_lora_status(int fd, cJSON *req);

struct handler {
    const char *method;
    void (*fn)(int, cJSON *);
};

static struct handler table[] = {
    {"ping",        handle_ping},
    {"lora_send",   handle_lora_send},
    {"lora_pop_rx", handle_lora_pop_rx},
    {"lora_status", handle_lora_status},
    {NULL, NULL}
};

/* ── response helpers (mirror the other blackhand services) ─────────────── */

static void send_result(int fd, cJSON *req, const char *result_str)
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
    if (str) { send_msg(fd, str); free(str); }
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
    if (str) { send_msg(fd, str); free(str); }
    cJSON_Delete(res);
}

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

void send_error(int fd, const char *message)
{
    cJSON *res = cJSON_CreateObject();
    cJSON_AddStringToObject(res, "error", message);
    char *str = cJSON_PrintUnformatted(res);
    if (str) { send_msg(fd, str); free(str); }
    cJSON_Delete(res);
}

/* ── handlers ──────────────────────────────────────────────────────────── */

static const char *param_string(cJSON *req, const char *key)
{
    cJSON *params = cJSON_GetObjectItem(req, "params");
    if (!params || !cJSON_IsObject(params)) return NULL;
    cJSON *item = cJSON_GetObjectItem(params, key);
    if (!item || !cJSON_IsString(item)) return NULL;
    return item->valuestring;
}

static void handle_ping(int fd, cJSON *req)
{
    send_result(fd, req, "pong");
}

static void handle_lora_send(int fd, cJSON *req)
{
    const char *text = param_string(req, "text");
    if (!text) { send_error(fd, "missing 'text' param"); return; }

    size_t len = strlen(text);
    if (len == 0) { send_error(fd, "empty text"); return; }
    if (len > SX1276_MAX_PAYLOAD) { send_error(fd, "text too long"); return; }

    if (!sx1276_ready()) { send_error(fd, "radio not ready"); return; }

    if (sx1276_send((const uint8_t *)text, len) != 0) {
        send_error(fd, "tx failed");
        return;
    }
    send_result(fd, req, "ok");
}

static void handle_lora_pop_rx(int fd, cJSON *req)
{
    char text[RXQ_MSG_MAX + 1];
    int len = rxq_pop(text, sizeof(text));

    cJSON *r = cJSON_CreateObject();
    if (len < 0) {
        cJSON_AddStringToObject(r, "text", "");
        cJSON_AddNumberToObject(r, "len", 0);
    } else {
        cJSON_AddStringToObject(r, "text", text);
        cJSON_AddNumberToObject(r, "len", len);
    }
    send_result_json(fd, req, r);
}

static void handle_lora_status(int fd, cJSON *req)
{
    cJSON *s = cJSON_CreateObject();
    cJSON_AddBoolToObject  (s, "ready",      sx1276_ready());
    cJSON_AddNumberToObject(s, "rx_pending", rxq_count());
    cJSON_AddNumberToObject(s, "freq_hz",    (double)SX1276_FREQ_HZ);
    cJSON_AddNumberToObject(s, "rssi",       sx1276_last_rssi());
    cJSON_AddNumberToObject(s, "snr",        sx1276_last_snr());
    send_result_json(fd, req, s);
}
