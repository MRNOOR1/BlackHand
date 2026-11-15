#include "draft_service.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
int main(void) {
    char dir[] = "/tmp/blackhand-draft-XXXXXX"; assert(mkdtemp(dir)); assert(chdir(dir) == 0);
    char num[32] = "", body[160] = "";
    assert(draft_service_load(num, sizeof(num), body, sizeof(body)) == 0);
    assert(draft_service_save("+1234", "Hi \"you\"\nsecond line") == 0);
    assert(draft_service_load(num, sizeof(num), body, sizeof(body)) == 0);
    assert(!strcmp(num, "+1234") && !strcmp(body, "Hi \"you\"\nsecond line"));
    FILE *f = fopen("draft.json", "w"); assert(f); fputs("{broken", f); fclose(f);
    assert(draft_service_load(num, sizeof(num), body, sizeof(body)) != 0);
    assert(!strcmp(body, "Hi \"you\"\nsecond line"));
    puts("PASS: draft restart round-trip and corruption protection");
}
