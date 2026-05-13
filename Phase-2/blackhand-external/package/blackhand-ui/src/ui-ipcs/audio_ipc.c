#include "audio_ipc.h"
#include "ipc.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>

#define AUDIO_SOCKET_PATH  "/run/bh-audio.sock"
#define AUDIO_RESPONSE_SIZE 4096

static int audio_response_is_ok(const char *response)
{
	cJSON *root = cJSON_Parse(response);
	if (!root)
		return -1;

	cJSON *result = cJSON_GetObjectItem(root, "result");
	int ok = cJSON_IsString(result) && strcmp(result->valuestring,
											  "ok") == 0;

	cJSON_Delete(root);
	return ok ? 0 : -1;
}

int audio_ipc_ping(void)
{
	char response[AUDIO_RESPONSE_SIZE];

	int rc = ipc_request(AUDIO_SOCKET_PATH, "{\"jsonrpc\":\"2.0\",\"method\":\"ping\",\"id\":1}", response, sizeof(response));

	if (rc != IPC_OK)
		return -1;

	cJSON *root = cJSON_Parse(response);
	if (!root)
		return -1;

	cJSON *result = cJSON_GetObjectItem(root, "result");

	int ok = 0;

	if (cJSON_IsString(result) && strcmp(result->valuestring, "pong") == 0)
		ok = 1;

	cJSON_Delete(root);

	return ok ? 0 : -1;
}

int audio_ipc_play(const char *filepath)
{
	if (filepath == NULL)
		return -1;

	char request[1024];
	char response[AUDIO_RESPONSE_SIZE];

	int n = snprintf(request,
					 sizeof(request),
					 "{\"jsonrpc\":\"2.0\",\"method\":\"play\",\"params\":{\"file\":\"%s\"},\"id\":1}",
					 filepath);

	if (n < 0 || (size_t)n >= sizeof(request))
		return -1;

	int rc = ipc_request(AUDIO_SOCKET_PATH,
						 request,
						 response,
						 sizeof(response));

	if (rc != IPC_OK)
		return -1;

	return audio_response_is_ok(response);
}

int audio_ipc_pause(void)
{
	char response[AUDIO_RESPONSE_SIZE];

	int rc = ipc_request(AUDIO_SOCKET_PATH, "{\"jsonrpc\":\"2.0\",\"method\":\"pause\",\"id\":1}", response, sizeof(response));

	if (rc != IPC_OK)
		return -1;

	return audio_response_is_ok(response);
}

int audio_ipc_stop(void)
{
	char response[AUDIO_RESPONSE_SIZE];

	int rc = ipc_request(AUDIO_SOCKET_PATH,
						 "{\"jsonrpc\":\"2.0\",\"method\":\"stop\",\"id\":1}",
						 response, sizeof(response));

	if (rc != IPC_OK)
		return -1;

	return audio_response_is_ok(response);
}

int audio_ipc_volume(int percent)
{
	if (percent < 0 || percent > 100)
		return -1;

	char request[256];
	char response[AUDIO_RESPONSE_SIZE];

	int n = snprintf(request,
					 sizeof(request),
					 "{\"jsonrpc\":\"2.0\",\"method\":\"volume\",\"params\":{\"volume\":%d},\"id\":1}",
					 percent);

	if (n < 0 || (size_t)n >= sizeof(request))
		return -1;

	int rc = ipc_request(AUDIO_SOCKET_PATH,
						 request,
						 response,
						 sizeof(response));

	if (rc != IPC_OK)
		return -1;

	return audio_response_is_ok(response);
}

int audio_ipc_status(int *out_playing, int *out_volume)
{
	if (out_playing == NULL || out_volume == NULL)
		return -1;

	char response[AUDIO_RESPONSE_SIZE];

	int rc = ipc_request(AUDIO_SOCKET_PATH,
						 "{\"jsonrpc\":\"2.0\",\"method\":\"status\",\"id\":1}",
						 response,
						 sizeof(response));

	if (rc != IPC_OK)
		return -1;

	cJSON *root = cJSON_Parse(response);
	if (!root)
		return -1;

	cJSON *result = cJSON_GetObjectItem(root, "result");
	if (!cJSON_IsObject(result))
	{
		cJSON_Delete(root);
		return -1;
	}

	cJSON *playing = cJSON_GetObjectItem(result, "playing");
	cJSON *volume = cJSON_GetObjectItem(result, "volume");

	if (!cJSON_IsBool(playing) || !cJSON_IsNumber(volume))
	{
		cJSON_Delete(root);
		return -1;
	}

	*out_playing = cJSON_IsTrue(playing) ? 1 : 0;
	*out_volume = volume->valueint;

	cJSON_Delete(root);
	return 0;
}

int audio_ipc_get_volume(void)
{
	int playing = 0, vol = -1;
	if (audio_ipc_status(&playing, &vol) != 0)
		return -1;
	return vol;
}