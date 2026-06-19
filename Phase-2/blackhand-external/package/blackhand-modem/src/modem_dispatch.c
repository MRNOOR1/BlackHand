#include "modem_dispatch.h"
#include "Modem.h"
#include "serial_utility.h"
#include "modem_storage.h"
#include "ipc.h"
#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

// Convenience guard: if the modem hardware isn't up, every modem-touching
// handler should bail out fast with a clear error instead of writing to a
// closed fd. Methods that don't touch the hardware (ping, modem_health)
// skip this check.
#define REQUIRE_MODEM_OR_FAIL(fd, id) \
	do { \
		if (!modem_present) { \
			send_err((fd), (id), "modem not present"); \
			return; \
		} \
	} while (0)

// ── response builders ──────────────────────────────────────────────────────

static void send_result_str(int fd, int id, const char *s)
{
	cJSON *res = cJSON_CreateObject();
	cJSON_AddStringToObject(res, "jsonrpc", "2.0");
	cJSON_AddStringToObject(res, "result", s);
	cJSON_AddNumberToObject(res, "id", id);
	char *str = cJSON_PrintUnformatted(res);
	if (str) {
		send_msg(fd, str);
		free(str);
	}
	cJSON_Delete(res);
}

// Takes ownership of `obj` (attached as the "result" item).
static void send_result_obj(int fd, int id, cJSON *obj)
{
	cJSON *res = cJSON_CreateObject();
	cJSON_AddStringToObject(res, "jsonrpc", "2.0");
	cJSON_AddItemToObject(res, "result", obj);
	cJSON_AddNumberToObject(res, "id", id);
	char *str = cJSON_PrintUnformatted(res);
	if (str) {
		send_msg(fd, str);
		free(str);
	}
	cJSON_Delete(res);
}

static void send_err(int fd, int id, const char *message)
{
	cJSON *res = cJSON_CreateObject();
	cJSON_AddStringToObject(res, "jsonrpc", "2.0");
	cJSON_AddStringToObject(res, "error", message);
	cJSON_AddNumberToObject(res, "id", id);
	char *str = cJSON_PrintUnformatted(res);
	if (str) {
		send_msg(fd, str);
		free(str);
	}
	cJSON_Delete(res);
}

// ── handlers ───────────────────────────────────────────────────────────────

static void handle_ping(int fd, int id, cJSON *params)
{
	(void)params;
	// Reachability check — works even when modem is down.
	send_result_str(fd, id, "pong");
}

static void handle_modem_health(int fd, int id, cJSON *params)
{
	(void)params;
	cJSON *obj = cJSON_CreateObject();
	cJSON_AddBoolToObject  (obj, "present", modem_present ? 1 : 0);
	cJSON_AddStringToObject(obj, "port",    modem_active_port());
	cJSON_AddStringToObject(obj, "error",   modem_present ? "" : modem_error);
	send_result_obj(fd, id, obj);
}

static void handle_dial(int fd, int id, cJSON *params)
{
	REQUIRE_MODEM_OR_FAIL(fd, id);
	cJSON *num = cJSON_GetObjectItem(params, "number");
	if (!cJSON_IsString(num)) { send_err(fd, id, "missing number"); return; }
	int rc = modem_dial(num->valuestring);
	send_result_str(fd, id, rc > 0 ? "calling" : "error");
}

static void handle_hangup(int fd, int id, cJSON *params)
{
	(void)params;
	REQUIRE_MODEM_OR_FAIL(fd, id);
	int rc = modem_hangup();
	send_result_str(fd, id, rc > 0 ? "ok" : "error");
}

static void handle_answer(int fd, int id, cJSON *params)
{
	(void)params;
	REQUIRE_MODEM_OR_FAIL(fd, id);
	int rc = modem_answer();
	send_result_str(fd, id, rc > 0 ? "ok" : "error");
}

static void handle_reject(int fd, int id, cJSON *params)
{
	// A rejected ringing call is just a hangup before pickup.
	(void)params;
	REQUIRE_MODEM_OR_FAIL(fd, id);
	modem_hangup();
	send_result_str(fd, id, "ok");
}

static void handle_send_sms(int fd, int id, cJSON *params)
{
	REQUIRE_MODEM_OR_FAIL(fd, id);
	cJSON *num  = cJSON_GetObjectItem(params, "number");
	cJSON *body = cJSON_GetObjectItem(params, "body");
	if (!cJSON_IsString(num) || !cJSON_IsString(body)) {
		send_err(fd, id, "missing number or body");
		return;
	}
	int rc = modem_sms_send(num->valuestring, body->valuestring);
	if (rc > 0)
		storage_record_sms(num->valuestring, "out", body->valuestring);
	send_result_str(fd, id, rc > 0 ? "ok" : "error");
}

static void handle_pop_pending_sms(int fd, int id, cJSON *params)
{
	(void)params;
	cJSON *arr = cJSON_CreateArray();
	int n = modem_pending_sms_count();
	for (int i = 0; i < n; i++) {
		char sender[64], body[160];
		if (modem_pending_sms_at(i, sender, sizeof(sender), body, sizeof(body)) == 0) {
			cJSON *obj = cJSON_CreateObject();
			cJSON_AddStringToObject(obj, "sender", sender);
			cJSON_AddStringToObject(obj, "body",   body);
			cJSON_AddItemToArray(arr, obj);
		}
	}
	modem_clear_pending_sms();
	send_result_obj(fd, id, arr);
}

static void handle_set_port(int fd, int id, cJSON *params)
{
	// Deliberately NO modem-present guard: switching ports is exactly what
	// you do when the modem is NOT present.
	cJSON *port = cJSON_GetObjectItem(params, "port");
	if (!cJSON_IsString(port)) { send_err(fd, id, "missing port"); return; }

	if (modem_select_port(port->valuestring) == 0)
		send_result_str(fd, id, modem_active_port());
	else
		send_err(fd, id, modem_error);
}

static void handle_modem_status(int fd, int id, cJSON *params)
{
	(void)params;
	REQUIRE_MODEM_OR_FAIL(fd, id);
	cJSON *obj = cJSON_CreateObject();

	pthread_mutex_lock(&call_state_mutex);
	CallState cs = call_state;
	pthread_mutex_unlock(&call_state_mutex);

	cJSON_AddNumberToObject(obj, "signal",            modem_signal_cached());
	cJSON_AddBoolToObject  (obj, "has_incoming_call", cs == CALL_RINGING);

	const char *state_str = "idle";
	switch (cs) {
		case CALL_DIALLING: state_str = "dialling"; break;
		case CALL_RINGING:  state_str = "ringing";  break;
		case CALL_ACTIVE:   state_str = "active";   break;
		default:            state_str = "idle";     break;
	}
	cJSON_AddStringToObject(obj, "call_state", state_str);

	char num[64];
	modem_get_incoming_number(num, sizeof(num));
	cJSON_AddStringToObject(obj, "incoming_number", num);

	// SMS / GPS aren't tracked yet — return defaults so the UI parses cleanly.
	cJSON_AddNumberToObject(obj, "pending_sms", 0);
	cJSON_AddBoolToObject  (obj, "gps_enabled", 0);

	send_result_obj(fd, id, obj);
}

// ── entry point ────────────────────────────────────────────────────────────

void modem_dispatch_handle(int client_fd, const char *json)
{
	cJSON *req = cJSON_Parse(json);
	if (!req) {
		send_err(client_fd, 0, "invalid json");
		return;
	}

	cJSON *id_item = cJSON_GetObjectItem(req, "id");
	int id = cJSON_IsNumber(id_item) ? id_item->valueint : 0;

	cJSON *method = cJSON_GetObjectItem(req, "method");
	cJSON *params = cJSON_GetObjectItem(req, "params");

	if (!cJSON_IsString(method)) {
		send_err(client_fd, id, "missing method");
		cJSON_Delete(req);
		return;
	}

	const char *m = method->valuestring;
	if      (strcmp(m, "ping")         == 0) handle_ping        (client_fd, id, params);
	else if (strcmp(m, "modem_health") == 0) handle_modem_health(client_fd, id, params);
	else if (strcmp(m, "dial")         == 0) handle_dial        (client_fd, id, params);
	else if (strcmp(m, "hangup")       == 0) handle_hangup      (client_fd, id, params);
	else if (strcmp(m, "answer")       == 0) handle_answer      (client_fd, id, params);
	else if (strcmp(m, "reject")       == 0) handle_reject      (client_fd, id, params);
	else if (strcmp(m, "set_port")         == 0) handle_set_port        (client_fd, id, params);
	else if (strcmp(m, "send_sms")         == 0) handle_send_sms        (client_fd, id, params);
	else if (strcmp(m, "modem_status")     == 0) handle_modem_status    (client_fd, id, params);
	else if (strcmp(m, "pop_pending_sms")  == 0) handle_pop_pending_sms (client_fd, id, params);
	else send_err(client_fd, id, "unknown method");

	cJSON_Delete(req);
}
