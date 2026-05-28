/*
 * ipc_dispatch.c — blackhand-modem JSON-RPC dispatch table.
 *
 * Phase 3 methods:
 *   ping              → "pong"
 *   get_messages      → list SMS from SIM via AT+CMGL
 *   send_sms          → send SMS via AT+CMGS
 *   dial              → ATD<number>;
 *   hangup            → ATH
 *   answer            → ATA
 *   reject            → AT+CHUP / ATH
 *   get_calls         → AT+CLCC
 *   gps_enable        → AT+CGPS=1
 *   gps_disable       → AT+CGPS=0
 *   get_location      → AT+CGPSINFO
 *   modem_status      → { signal, has_incoming, incoming_number,
 *                          pending_sms, gps_enabled }
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ipc_dispatch.h"
#include "ipc_framing.h"
#include "cJSON.h"
#include "modem_core.h"
#include "at_parser.h"

/* Forward declarations from modem_service.c */
void modem_state_get(int *incoming, char *number, size_t num_size,
                     int *pending_sms, int *gps_on, int *rssi);
void modem_state_clear_pending_sms(void);
void modem_state_clear_incoming_call(void);
void modem_state_set_gps(int enabled);

/* ── private handler forward declarations ── */
static void handle_ping(int fd, cJSON *req);
static void handle_get_messages(int fd, cJSON *req);
static void handle_send_sms(int fd, cJSON *req);
static void handle_dial(int fd, cJSON *req);
static void handle_hangup(int fd, cJSON *req);
static void handle_answer(int fd, cJSON *req);
static void handle_reject(int fd, cJSON *req);
static void handle_get_calls(int fd, cJSON *req);
static void handle_gps_enable(int fd, cJSON *req);
static void handle_gps_disable(int fd, cJSON *req);
static void handle_get_location(int fd, cJSON *req);
static void handle_modem_status(int fd, cJSON *req);

/* ── dispatch table ── */
struct handler {
    const char *method;
    void (*fn)(int, cJSON *);
};

static struct handler table[] = {
    {"ping",          handle_ping},
    {"get_messages",  handle_get_messages},
    {"send_sms",      handle_send_sms},
    {"dial",          handle_dial},
    {"hangup",        handle_hangup},
    {"answer",        handle_answer},
    {"reject",        handle_reject},
    {"get_calls",     handle_get_calls},
    {"gps_enable",    handle_gps_enable},
    {"gps_disable",   handle_gps_disable},
    {"get_location",  handle_get_location},
    {"modem_status",  handle_modem_status},
    {NULL, NULL}
};

/* ── helpers ── */

static void send_result_str(int fd, cJSON *req, const char *s)
{
    cJSON *id = cJSON_GetObjectItem(req, "id");
    if (!id || !cJSON_IsNumber(id)) { send_error(fd, "invalid id"); return; }
    cJSON *res = cJSON_CreateObject();
    cJSON_AddStringToObject(res, "jsonrpc", "2.0");
    cJSON_AddStringToObject(res, "result", s);
    cJSON_AddNumberToObject(res, "id", id->valueint);
    char *str = cJSON_PrintUnformatted(res);
    send_msg(fd, str);
    free(str);
    cJSON_Delete(res);
}

static void send_result_json(int fd, cJSON *req, cJSON *result)
{
    cJSON *id = cJSON_GetObjectItem(req, "id");
    if (!id || !cJSON_IsNumber(id)) {
        send_error(fd, "invalid id");
        cJSON_Delete(result);
        return;
    }
    cJSON *res = cJSON_CreateObject();
    cJSON_AddStringToObject(res, "jsonrpc", "2.0");
    cJSON_AddItemToObject(res, "result", result);
    cJSON_AddNumberToObject(res, "id", id->valueint);
    char *str = cJSON_PrintUnformatted(res);
    send_msg(fd, str);
    free(str);
    cJSON_Delete(res);
}

/* ── dispatch() ── */
void dispatch(int fd, cJSON *req)
{
    cJSON *ver = cJSON_GetObjectItem(req, "jsonrpc");
    if (!ver || !cJSON_IsString(ver) || strcmp(ver->valuestring, "2.0") != 0) {
        send_error(fd, "invalid jsonrpc version");
        return;
    }
    cJSON *id = cJSON_GetObjectItem(req, "id");
    if (!id || !cJSON_IsNumber(id)) {
        send_error(fd, "missing id");
        return;
    }
    cJSON *meth = cJSON_GetObjectItem(req, "method");
    if (!meth || !cJSON_IsString(meth)) {
        send_error(fd, "missing method");
        return;
    }
    const char *method = meth->valuestring;
    for (int i = 0; table[i].method; i++) {
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

static void handle_get_messages(int fd, cJSON *req)
{
    cJSON *arr = modem_sms_list();
    send_result_json(fd, req, arr);
}

static void handle_send_sms(int fd, cJSON *req)
{
    cJSON *params = cJSON_GetObjectItem(req, "params");
    if (!params) { send_error(fd, "missing params"); return; }
    cJSON *number = cJSON_GetObjectItem(params, "number");
    cJSON *body   = cJSON_GetObjectItem(params, "body");
    if (!cJSON_IsString(number) || !cJSON_IsString(body)) {
        send_error(fd, "missing number or body");
        return;
    }
    int rc = modem_sms_send(number->valuestring, body->valuestring);
    send_result_str(fd, req, rc == 0 ? "ok" : "error");
}

static void handle_dial(int fd, cJSON *req)
{
    cJSON *params = cJSON_GetObjectItem(req, "params");
    if (!params) { send_error(fd, "missing params"); return; }
    cJSON *number = cJSON_GetObjectItem(params, "number");
    if (!cJSON_IsString(number)) { send_error(fd, "missing number"); return; }
    int rc = modem_call_dial(number->valuestring);
    send_result_str(fd, req, rc == 0 ? "calling" : "error");
}

static void handle_hangup(int fd, cJSON *req)
{
    modem_call_hangup();
    modem_state_clear_incoming_call();
    send_result_str(fd, req, "ok");
}

static void handle_answer(int fd, cJSON *req)
{
    int rc = modem_call_answer();
    modem_state_clear_incoming_call();
    send_result_str(fd, req, rc == 0 ? "ok" : "error");
}

static void handle_reject(int fd, cJSON *req)
{
    modem_call_reject();
    modem_state_clear_incoming_call();
    send_result_str(fd, req, "ok");
}

static void handle_get_calls(int fd, cJSON *req)
{
    cJSON *arr = modem_call_get_active();
    send_result_json(fd, req, arr);
}

static void handle_gps_enable(int fd, cJSON *req)
{
    modem_gps_enable();
    modem_state_set_gps(1);
    send_result_str(fd, req, "ok");
}

static void handle_gps_disable(int fd, cJSON *req)
{
    modem_gps_disable();
    modem_state_set_gps(0);
    send_result_str(fd, req, "ok");
}

static void handle_get_location(int fd, cJSON *req)
{
    double lat = 0.0, lon = 0.0, alt = 0.0, speed = 0.0;
    int rc = modem_gps_get_location(&lat, &lon, &alt, &speed);

    cJSON *obj = cJSON_CreateObject();
    if (rc == 0) {
        cJSON_AddNumberToObject(obj, "latitude",  lat);
        cJSON_AddNumberToObject(obj, "longitude", lon);
        cJSON_AddNumberToObject(obj, "altitude",  alt);
        cJSON_AddNumberToObject(obj, "speed",     speed);
        cJSON_AddStringToObject(obj, "fix",       "3D");
    } else {
        cJSON_AddStringToObject(obj, "fix", "none");
    }
    send_result_json(fd, req, obj);
}

static void handle_modem_status(int fd, cJSON *req)
{
    int incoming = 0, pending = 0, gps = 0, rssi = 0;
    char num[64] = "";
    modem_state_get(&incoming, num, sizeof(num), &pending, &gps, &rssi);

    cJSON *obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(obj,  "signal",           rssi);
    cJSON_AddBoolToObject(obj,    "has_incoming_call", incoming);
    cJSON_AddStringToObject(obj,  "incoming_number",  num);
    cJSON_AddNumberToObject(obj,  "pending_sms",      pending);
    cJSON_AddBoolToObject(obj,    "gps_enabled",      gps);
    send_result_json(fd, req, obj);
}
