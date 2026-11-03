#define STORAGE_DATA_DIR "data"
#define main storage_daemon_main
#include "../blackhand-external/package/blackhand-storage/src/storage_service.c"
#undef main
#include <assert.h>
int main(void) {
    char dir[] = "/tmp/blackhand-wipe-XXXXXX";
    assert(mkdtemp(dir)); assert(chdir(dir) == 0);
    ensure_dirs();
    assert(mkdir("data/numbers/+1234", 0700) == 0);
    FILE *f = fopen("data/numbers/+1234/sms.json", "w"); assert(f); fputs("[]", f); fclose(f);
    f = fopen("data/numbers/+1234/calls.json", "w"); assert(f); fputs("[]", f); fclose(f);
    assert(remove_history() == 0);
    assert(access("data/numbers/+1234", F_OK) != 0);
    assert(remove_history() == 0); /* repeat after restart is safe */
    assert(mkdir("data/numbers/+1234", 0700) == 0);
    assert(mkdir("data/numbers/+1234/unexpected", 0700) == 0);
    assert(remove_history() != 0); /* cannot silently claim complete */
    int pair[2]; assert(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
    cJSON *params = cJSON_Parse("{\"name\":\"Alice\",\"number\":\"+1234\"}");
    h_contacts_save(pair[0], 1, params); char response[512];
    assert(recv_msg(pair[1], response, sizeof(response)) > 0);
    assert(strstr(response, "result"));
    h_contacts_save(pair[0], 1, params);
    assert(recv_msg(pair[1], response, sizeof(response)) > 0);
    assert(strstr(response, "error"));
    cJSON_AddStringToObject(params, "original_number", "+1234");
    cJSON_ReplaceItemInObject(params, "number", cJSON_CreateString("+5678"));
    h_contacts_save(pair[0], 1, params);
    assert(recv_msg(pair[1], response, sizeof(response)) > 0);
    assert(strstr(response, "result"));
    cJSON_Delete(params); close(pair[0]); close(pair[1]);
    cJSON *contacts = load_json(CONTACTS_PATH, 1, NULL);
    assert(cJSON_GetArraySize(contacts) == 1);
    assert(!strcmp(cJSON_GetObjectItem(cJSON_GetArrayItem(contacts, 0), "number")->valuestring, "+5678"));
    cJSON_Delete(contacts);
    puts("PASS: persisted history removal, repeated wipe, partial failure; contact collision and atomic renumber");
}
