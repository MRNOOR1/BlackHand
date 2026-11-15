#include "pin_service.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
int main(void) {
    char dir[] = "/tmp/blackhand-pin-XXXXXX"; assert(mkdtemp(dir)); assert(chdir(dir) == 0);
    FILE *f = fopen("pin.conf", "w"); assert(f); fputs("1234\n", f); fclose(f);
    pin_service_init(); assert(pin_service_verify("1234")); assert(!pin_service_verify("0000"));
    char data[128]; f = fopen("pin.conf", "r"); assert(f); assert(fgets(data, sizeof(data), f)); fclose(f);
    assert(!strncmp(data, "bhpin1:", 7));
    struct stat st; assert(stat("pin.conf", &st) == 0); assert((st.st_mode & 0777) == 0600);
    assert(pin_service_update("1234", "9876") == 0);
    pin_service_init(); assert(pin_service_verify("9876")); assert(!pin_service_verify("1234"));
    assert(rename("pin.conf", "original") == 0); assert(mkdir("pin.conf", 0700) == 0);
    assert(pin_service_update("9876", "4567") != 0); assert(pin_service_verify("9876"));
    assert(rmdir("pin.conf") == 0);
    f = fopen("pin.conf", "w"); assert(f); fputs("corrupt", f); fclose(f);
    pin_service_init(); assert(!pin_service_verify("0000"));
    puts("PASS: PIN migration, hashing, restart, failed update and fail-closed corruption");
}
