#include "pin_service.h"
#include <string.h>
#include <stdio.h>

#define DEFAULT_PIN "0000"
#define PIN_FILE "pin.conf"

static char g_pin[8] = DEFAULT_PIN;

static void load_pin(void) {
    FILE *f = fopen(PIN_FILE, "r");
    if (!f) return;
    char line[16];
    if (fgets(line, sizeof(line), f)) {
        line[4] = '\0'; /* ensure 4 digits max */
        /* Verify all digits */
        int valid = 1;
        for (int i = 0; i < 4 && line[i]; i++) {
            if (line[i] < '0' || line[i] > '9') { valid = 0; break; }
        }
        if (valid && strlen(line) == 4) {
            memcpy(g_pin, line, 5);
        }
    }
    fclose(f);
}

static void save_pin(void) {
    FILE *f = fopen(PIN_FILE, "w");
    if (!f) return;
    fprintf(f, "%s\n", g_pin);
    fclose(f);
}

void pin_service_init(void) {
    load_pin();
}

bool pin_service_verify(const char *pin) {
    if (!pin) return false;
    return (strcmp(pin, g_pin) == 0);
}

int pin_service_update(const char *old_pin, const char *new_pin) {
    if (!old_pin || !new_pin) return -1;
    if (!pin_service_verify(old_pin)) return -1;
    if (strlen(new_pin) != 4) return -1;

    /* Verify all digits */
    for (int i = 0; i < 4; i++) {
        if (new_pin[i] < '0' || new_pin[i] > '9') return -1;
    }

    memcpy(g_pin, new_pin, 5);
    save_pin();
    return 0;
}

const char *pin_service_get(void) {
    return g_pin;
}

void pin_service_reset(void) {
    memcpy(g_pin, DEFAULT_PIN, 5);
    save_pin();
}
