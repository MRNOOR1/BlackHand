/*
 * modem_storage.c — fire-and-forget history writes to blackhand-storage.
 */
#include "modem_storage.h"
#include "ipc.h"
#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STORAGE_SOCK "/run/bh-storage.sock"

static void rpc_fire(const char *method, cJSON *params /* owned */)
{
	cJSON *req = cJSON_CreateObject();
	cJSON_AddStringToObject(req, "jsonrpc", "2.0");
	cJSON_AddStringToObject(req, "method", method);
	cJSON_AddItemToObject(req, "params", params);
	cJSON_AddNumberToObject(req, "id", 1);

	char *json = cJSON_PrintUnformatted(req);
	cJSON_Delete(req);
	if (!json) return;

	char resp[512];
	if (ipc_request(STORAGE_SOCK, json, resp, sizeof(resp)) != IPC_OK)
		fprintf(stderr, "blackhand-modem: storage write failed (%s) — "
		        "is blackhand-storage running?\n", method);
	free(json);
}

void storage_record_call(const char *number, const char *direction,
                         const char *outcome, long ts, int duration)
{
	if (!number || !number[0]) {
		fprintf(stderr, "blackhand-modem: call not recorded (no number)\n");
		return;
	}
	cJSON *p = cJSON_CreateObject();
	cJSON_AddStringToObject(p, "number", number);
	cJSON_AddStringToObject(p, "direction", direction);
	cJSON_AddStringToObject(p, "outcome", outcome);
	cJSON_AddNumberToObject(p, "ts", (double)ts);
	cJSON_AddNumberToObject(p, "duration", duration);
	rpc_fire("history.add_call", p);
}

void storage_record_sms(const char *number, const char *direction,
                        const char *body)
{
	if (!number || !number[0]) {
		fprintf(stderr, "blackhand-modem: sms not recorded (no number)\n");
		return;
	}
	cJSON *p = cJSON_CreateObject();
	cJSON_AddStringToObject(p, "number", number);
	cJSON_AddStringToObject(p, "direction", direction);
	cJSON_AddStringToObject(p, "body", body ? body : "");
	rpc_fire("history.add_sms", p);
}
