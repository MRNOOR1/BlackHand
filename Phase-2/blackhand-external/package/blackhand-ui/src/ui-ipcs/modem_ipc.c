#include "modem_ipc.h"
#include "ipc.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>

#define MODEM_SOCK     "/run/bh-modem.sock"
#define RESP_SIZE      16384

/* ── helpers ─────────────────────────────────────────────────────────────── */

static int is_ok(const char *response)
{
    cJSON *root = cJSON_Parse(response);
    if (!root) return -1;
    cJSON *result = cJSON_GetObjectItem(root, "result");
    int ok = cJSON_IsString(result) &&
             (strcmp(result->valuestring, "ok") == 0 ||
              strcmp(result->valuestring, "calling") == 0);
    cJSON_Delete(root);
    return ok ? 0 : -1;
}

static int simple_request(const char *json)
{
    char resp[RESP_SIZE];
    if (ipc_request(MODEM_SOCK, json, resp, sizeof(resp)) != IPC_OK) return -1;
    return is_ok(resp);
}

/* ── public API ──────────────────────────────────────────────────────────── */

int modem_ipc_ping(void)
{
    char resp[256];
    if (ipc_request(MODEM_SOCK,
                    "{\"jsonrpc\":\"2.0\",\"method\":\"ping\",\"id\":1}",
                    resp, sizeof(resp)) != IPC_OK) return -1;
    cJSON *root = cJSON_Parse(resp);
    if (!root) return -1;
    cJSON *result = cJSON_GetObjectItem(root, "result");
    int ok = cJSON_IsString(result) && strcmp(result->valuestring, "pong") == 0;
    cJSON_Delete(root);
    return ok ? 0 : -1;
}

int modem_ipc_send_sms(const char *number, const char *body)
{
    if (!number || !body) return -1;
    char req[1024];
    int n = snprintf(req, sizeof(req),
        "{\"jsonrpc\":\"2.0\",\"method\":\"send_sms\","
        "\"params\":{\"number\":\"%s\",\"body\":\"%s\"},\"id\":1}",
        number, body);
    if (n < 0 || (size_t)n >= sizeof(req)) return -1;
    return simple_request(req);
}

int modem_ipc_get_messages(char *buf, int buf_size)
{
    char resp[RESP_SIZE];
    if (ipc_request(MODEM_SOCK,
                    "{\"jsonrpc\":\"2.0\",\"method\":\"get_messages\",\"id\":1}",
                    resp, sizeof(resp)) != IPC_OK) return -1;
    cJSON *root = cJSON_Parse(resp);
    if (!root) return -1;
    cJSON *result = cJSON_GetObjectItem(root, "result");
    if (!cJSON_IsArray(result)) { cJSON_Delete(root); return -1; }
    char *str = cJSON_PrintUnformatted(result);
    if (str && buf && buf_size > 0) {
        strncpy(buf, str, (size_t)(buf_size - 1));
        buf[buf_size - 1] = '\0';
        free(str);
        cJSON_Delete(root);
        return 0;
    }
    free(str);
    cJSON_Delete(root);
    return -1;
}

int modem_ipc_dial(const char *number)
{
    if (!number) return -1;
    char req[256];
    snprintf(req, sizeof(req),
        "{\"jsonrpc\":\"2.0\",\"method\":\"dial\","
        "\"params\":{\"number\":\"%s\"},\"id\":1}", number);
    return simple_request(req);
}

int modem_ipc_hangup(void)
{
    return simple_request("{\"jsonrpc\":\"2.0\",\"method\":\"hangup\",\"id\":1}");
}

int modem_ipc_answer(void)
{
    return simple_request("{\"jsonrpc\":\"2.0\",\"method\":\"answer\",\"id\":1}");
}

int modem_ipc_reject(void)
{
    return simple_request("{\"jsonrpc\":\"2.0\",\"method\":\"reject\",\"id\":1}");
}

int modem_ipc_get_calls(char *buf, int buf_size)
{
    char resp[RESP_SIZE];
    if (ipc_request(MODEM_SOCK,
                    "{\"jsonrpc\":\"2.0\",\"method\":\"get_calls\",\"id\":1}",
                    resp, sizeof(resp)) != IPC_OK) return -1;
    cJSON *root = cJSON_Parse(resp);
    if (!root) return -1;
    cJSON *result = cJSON_GetObjectItem(root, "result");
    if (!cJSON_IsArray(result)) { cJSON_Delete(root); return -1; }
    char *str = cJSON_PrintUnformatted(result);
    if (str && buf && buf_size > 0) {
        strncpy(buf, str, (size_t)(buf_size - 1));
        buf[buf_size - 1] = '\0';
        free(str);
        cJSON_Delete(root);
        return 0;
    }
    free(str);
    cJSON_Delete(root);
    return -1;
}

int modem_ipc_gps_enable(void)
{
    return simple_request("{\"jsonrpc\":\"2.0\",\"method\":\"gps_enable\",\"id\":1}");
}

int modem_ipc_gps_disable(void)
{
    return simple_request("{\"jsonrpc\":\"2.0\",\"method\":\"gps_disable\",\"id\":1}");
}

int modem_ipc_get_location(GpsLocation *out)
{
    if (!out) return -1;
    char resp[RESP_SIZE];
    if (ipc_request(MODEM_SOCK,
                    "{\"jsonrpc\":\"2.0\",\"method\":\"get_location\",\"id\":1}",
                    resp, sizeof(resp)) != IPC_OK) return -1;
    cJSON *root = cJSON_Parse(resp);
    if (!root) return -1;
    cJSON *result = cJSON_GetObjectItem(root, "result");
    if (!cJSON_IsObject(result)) { cJSON_Delete(root); return -1; }

    cJSON *fix = cJSON_GetObjectItem(result, "fix");
    out->has_fix = cJSON_IsString(fix) && strcmp(fix->valuestring, "none") != 0;
    if (out->has_fix) {
        cJSON *lat = cJSON_GetObjectItem(result, "latitude");
        cJSON *lon = cJSON_GetObjectItem(result, "longitude");
        cJSON *alt = cJSON_GetObjectItem(result, "altitude");
        cJSON *spd = cJSON_GetObjectItem(result, "speed");
        out->latitude  = cJSON_IsNumber(lat) ? lat->valuedouble : 0.0;
        out->longitude = cJSON_IsNumber(lon) ? lon->valuedouble : 0.0;
        out->altitude  = cJSON_IsNumber(alt) ? alt->valuedouble : 0.0;
        out->speed     = cJSON_IsNumber(spd) ? spd->valuedouble : 0.0;
    }
    cJSON_Delete(root);
    return out->has_fix ? 0 : -1;
}

int modem_ipc_get_status(ModemStatus *out)
{
    if (!out) return -1;
    char resp[RESP_SIZE];
    if (ipc_request(MODEM_SOCK,
                    "{\"jsonrpc\":\"2.0\",\"method\":\"modem_status\",\"id\":1}",
                    resp, sizeof(resp)) != IPC_OK) return -1;
    cJSON *root = cJSON_Parse(resp);
    if (!root) return -1;
    cJSON *result = cJSON_GetObjectItem(root, "result");
    if (!cJSON_IsObject(result)) { cJSON_Delete(root); return -1; }

    cJSON *sig  = cJSON_GetObjectItem(result, "signal");
    cJSON *inc  = cJSON_GetObjectItem(result, "has_incoming_call");
    cJSON *num  = cJSON_GetObjectItem(result, "incoming_number");
    cJSON *sms  = cJSON_GetObjectItem(result, "pending_sms");
    cJSON *gps  = cJSON_GetObjectItem(result, "gps_enabled");

    out->signal           = cJSON_IsNumber(sig)  ? sig->valueint   : 0;
    out->has_incoming_call= cJSON_IsTrue(inc)     ? 1               : 0;
    out->pending_sms      = cJSON_IsNumber(sms)   ? sms->valueint  : 0;
    out->gps_enabled      = cJSON_IsTrue(gps)     ? 1               : 0;
    if (cJSON_IsString(num)) {
        strncpy(out->incoming_number, num->valuestring,
                sizeof(out->incoming_number) - 1);
        out->incoming_number[sizeof(out->incoming_number) - 1] = '\0';
    } else {
        out->incoming_number[0] = '\0';
    }
    cJSON_Delete(root);
    return 0;
}
