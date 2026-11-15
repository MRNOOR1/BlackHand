#define _POSIX_C_SOURCE 200809L
#include "draft_service.h"
#include "file_store.h"
#include "cJSON.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#ifndef SMS_DRAFT_PATH
#define SMS_DRAFT_PATH "/data/sms-draft.json"
#endif
int draft_service_load(char *number, size_t number_size, char *body, size_t body_size)
{
    FILE *f = fopen(SMS_DRAFT_PATH, "r");
    if (!f) return errno == ENOENT ? 0 : -1;
    char data[2048];
    size_t n = fread(data, 1, sizeof(data) - 1, f);
    int ok = !ferror(f) && feof(f);
    fclose(f); data[n] = '\0';
    cJSON *root = ok ? cJSON_Parse(data) : NULL;
    cJSON *num = cJSON_GetObjectItem(root, "number"), *text = cJSON_GetObjectItem(root, "body");
    ok = cJSON_IsString(num) && cJSON_IsString(text) &&
         strlen(num->valuestring) < number_size && strlen(text->valuestring) < body_size;
    if (ok) { strcpy(number, num->valuestring); strcpy(body, text->valuestring); }
    cJSON_Delete(root);
    return ok ? 0 : -1;
}
int draft_service_save(const char *number, const char *body)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "number", number);
    cJSON_AddStringToObject(root, "body", body);
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return -1;
    char temp[1024];
    FILE *f = file_store_begin(SMS_DRAFT_PATH, temp, sizeof(temp));
    if (!f) { free(json); return -1; }
    fputs(json, f); free(json);
    return file_store_commit(f, temp, SMS_DRAFT_PATH, 0);
}
