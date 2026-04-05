#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ipc_dispatch.h"

/* private handler forward declarations */
static void handle_ping(int fd, cJSON *req);
static void handle_echo(int fd, cJSON *req);

struct handler
{
	const char *method;
	void (*fn)(int, cJSON *);
};

struct handler table[] = {
	{"ping", handle_ping},
	{"echo", handle_echo},
	{NULL, NULL}};

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