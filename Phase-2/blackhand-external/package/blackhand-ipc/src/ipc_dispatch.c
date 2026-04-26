#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ipc_dispatch.h"
#include "cJSON.h"
#include "ipc_framing.h"
#include "audio_alsa.h" 

/* private handler forward declarations */
static void handle_ping(int fd, cJSON *req);
static void handle_echo(int fd, cJSON *req);

/* handler forward declaration for audio */
static void handle_play(int fd, cJSON *req);
static void handle_pause(int fd, cJSON *req);
static void handle_volume(int fd, cJSON *req);
static void handle_status(int fd, cJSON *req);

struct handler
{
	const char *method;
	void (*fn)(int, cJSON *);
};

struct handler table[] = {
	{"ping", handle_ping},
	{"echo", handle_echo},
	/* Audio entries */
	{"play", handle_play},
	{"pause", handle_pause},
	{"volume", handle_volume},
	{"status", handle_status},
	{NULL, NULL}};

/* for Audio*/

/* Helper: send a JSON-RPC success response */
static void send_result(int fd, cJSON *req, const char *result_str) {
    cJSON *id_item = cJSON_GetObjectItem(req, "id"); /* Extracts id from the request & id must be int*/
    if (!id_item || !cJSON_IsNumber(id_item)) {
        send_error(fd, "invalid id");
        return;
    }
	/* Builds a JSON‑RPC 2.0 response: {"jsonrpc":"2.0","result":result_str,"id":id}. */
    int id = id_item->valueint;
    cJSON *res = cJSON_CreateObject();
    cJSON_AddStringToObject(res, "jsonrpc", "2.0");
    cJSON_AddStringToObject(res, "result", result_str);
    cJSON_AddNumberToObject(res, "id", id);
    char *str = cJSON_PrintUnformatted(res);
	/*Sends JSON-RPC containing the id using send_msg(), cleans up. */
    send_msg(fd, str);
    free(str);
    cJSON_Delete(res);
}

/* Helper: send a JSON-RPC result with a custom JSON object 
	Implementation is similar to the previous method:
	The only different result_obj instead of result_str*/
static void send_result_json(int fd, cJSON *req, cJSON *result_obj) {
    cJSON *id_item = cJSON_GetObjectItem(req, "id");
    if (!id_item || !cJSON_IsNumber(id_item)) {
        send_error(fd, "invalid id");
        return;
    }
    int id = id_item->valueint;
    cJSON *res = cJSON_CreateObject();
    cJSON_AddStringToObject(res, "jsonrpc", "2.0");
    cJSON_AddItemToObject(res, "result", result_obj);  /* moves ownership */
    cJSON_AddNumberToObject(res, "id", id);
    char *str = cJSON_PrintUnformatted(res);
    send_msg(fd, str);
    free(str);
    cJSON_Delete(res);
}

/* ========== Handler implementations ========== */

static void handle_play(int fd, cJSON *req) {
    /* Expected: { "method":"play", "params": { "file":"/path/to/file.wav" }, "id": n } */
    cJSON *params = cJSON_GetObjectItem(req, "params");
    if (!params || !cJSON_IsObject(params)) {
        send_error(fd, "missing or invalid params");
        return;
    }
    cJSON *file_item = cJSON_GetObjectItem(params, "file");
    if (!file_item || !cJSON_IsString(file_item)) {
        send_error(fd, "missing or invalid 'file' field");
        return;
    }
    const char *path = file_item->valuestring;
    audio_play(path);
    send_result(fd, req, "ok");
}

static void handle_pause(int fd, cJSON *req) {
    audio_stop();
    send_result(fd, req, "ok");
}

static void handle_volume(int fd, cJSON *req) {
    cJSON *params = cJSON_GetObjectItem(req, "params");
    if (!params || !cJSON_IsObject(params)) {
        send_error(fd, "missing or invalid params");
        return;
    }
    cJSON *vol_item = cJSON_GetObjectItem(params, "volume");
    if (!vol_item || !cJSON_IsNumber(vol_item)) {
        send_error(fd, "missing or invalid 'volume' field");
        return;
    }
    int volume = vol_item->valueint;
    if (volume < 0 || volume > 100) {
        send_error(fd, "volume must be between 0 and 100");
        return;
    }
    audio_set_volume(volume);
    send_result(fd, req, "ok");
}

static void handle_status(int fd, cJSON *req) {
    cJSON *status_obj = cJSON_CreateObject();
    cJSON_AddBoolToObject(status_obj, "playing", audio_is_playing());
    cJSON_AddNumberToObject(status_obj, "volume", audio_get_volume());
    send_result_json(fd, req, status_obj);   /* status_obj ownership transferred */
}

/* Original code */
void dispatch(int fd, cJSON *req)
{
	cJSON *version_item = cJSON_GetObjectItem(req, "jsonrpc");
	if (version_item == NULL)
	{
		send_error(fd, "version is unknown");
		return;
	}
	if (!cJSON_IsString(version_item))
	{
		send_error(fd, "missing method");
		return;
	}
	if (strcmp(version_item->valuestring, "2.0") != 0)
	{
		send_error(fd, "incorrect jsonrpc version");
		return;
	}

	cJSON *id = cJSON_GetObjectItem(req, "id");
	if (id == NULL)
	{
		send_error(fd, "id is missing");
		return;
	}
	if (!cJSON_IsNumber(id))
	{
		send_error(fd, "incorrect id type");
		return;
	}

	cJSON *method_item = cJSON_GetObjectItem(req, "method");

	if (!cJSON_IsString(method_item))
	{
		send_error(fd, "missing method");
		return;
	}

	const char *method = method_item->valuestring;

	for (int i = 0; table[i].method != NULL; i++)
	{
		if (strcmp(table[i].method, method) == 0)
		{
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