/*
 * modem_storage.c — acknowledged history writes to blackhand-storage.
 */
#include "modem_storage.h"
#include "ipc.h"
#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
static atomic_int write_failure;
int storage_write_failed(void) { return atomic_load(&write_failure); }

#define STORAGE_SOCK "/run/bh-storage.sock"

static int rpc_fire(const char *method, cJSON *params /* owned */)
{
	cJSON *req = cJSON_CreateObject();
	cJSON_AddStringToObject(req, "jsonrpc", "2.0");
	cJSON_AddStringToObject(req, "method", method);
	cJSON_AddItemToObject(req, "params", params);
	cJSON_AddNumberToObject(req, "id", 1);

	char *json = cJSON_PrintUnformatted(req);
	cJSON_Delete(req);
	if (!json) return -1;

	char resp[512];
	int ok = 0;
	if (ipc_request(STORAGE_SOCK, json, resp, sizeof(resp)) == IPC_OK) {
		cJSON *reply = cJSON_Parse(resp);
		cJSON *result = cJSON_GetObjectItem(reply, "result");
		ok = cJSON_IsString(result) && strcmp(result->valuestring, "ok") == 0 &&
		     cJSON_GetObjectItem(reply, "error") == NULL;
		cJSON_Delete(reply);
	}
	if (!ok) atomic_store(&write_failure, 1);
	if (!ok) fprintf(stderr, "blackhand-modem: history NOT saved (%s)\n", method);
	free(json);
	return ok ? 0 : -1;
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

int storage_record_sms(const char *number, const char *direction,
                        const char *body)
{
	if (!number || !number[0]) {
		fprintf(stderr, "blackhand-modem: sms not recorded (no number)\n");
		return -1;
	}
	cJSON *p = cJSON_CreateObject();
	cJSON_AddStringToObject(p, "number", number);
	cJSON_AddStringToObject(p, "direction", direction);
	cJSON_AddStringToObject(p, "body", body ? body : "");
	return rpc_fire("history.add_sms", p);
}
