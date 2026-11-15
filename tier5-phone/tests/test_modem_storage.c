#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ipc.h"
#include "modem_storage.h"
static int mode;
int ipc_request(const char *path, const char *json, char *out, size_t size) {
    (void)path; (void)json;
    if (mode == 0) snprintf(out, size, "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":\"ok\"}");
    else if (mode == 1) snprintf(out, size, "{\"jsonrpc\":\"2.0\",\"id\":1,\"error\":{\"message\":\"disk full\"}}");
    else if (mode == 2) snprintf(out, size, "not JSON");
    else return IPC_ERR_TIMEOUT;
    return IPC_OK;
}
int main(void) {
    assert(storage_record_sms("+1234", "in", "hello") == 0);
    for (mode = 1; mode < 4; mode++) assert(storage_record_sms("+1234", "in", "hello") != 0);
    puts("PASS: modem distinguishes transport, RPC, and malformed-response failures");
}
