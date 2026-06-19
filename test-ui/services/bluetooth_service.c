#include "bluetooth_service.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static BtDevice s_devices[BT_MAX_DEVICES];
static size_t s_count = 0;
static int s_available = 0;

static int read_command(const char *cmd, char *out, size_t out_sz) {
    if (!cmd || !out || out_sz == 0) return -1;
    out[0] = '\0';
    FILE *p = popen(cmd, "r");
    if (!p) return -1;
    size_t n = 0;
    while (!feof(p) && n + 1 < out_sz) {
        size_t r = fread(out + n, 1, out_sz - n - 1, p);
        if (r == 0) break;
        n += r;
    }
    out[n] = '\0';
    int rc = pclose(p);
    return rc;
}

static int run_command(const char *cmd) {
    if (!cmd) return -1;
    int rc = system(cmd);
    if (rc == -1) return -1;
    return 0;
}

static int mac_is_safe(const char *mac) {
    if (!mac) return 0;
    size_t len = strlen(mac);
    if (len < 11 || len > 23) return 0;
    for (size_t i = 0; i < len; i++) {
        char c = mac[i];
        if (!(isdigit((unsigned char)c) || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f') || c == ':')) {
            return 0;
        }
    }
    return 1;
}

static int is_audio_capable(const char *mac) {
    if (!mac_is_safe(mac)) return 0;
    char cmd[256];
    char out[8192];
    snprintf(cmd, sizeof(cmd), "bluetoothctl --timeout 3 info %s 2>/dev/null", mac);
    if (read_command(cmd, out, sizeof(out)) != 0) return 0;

    if (strstr(out, "UUID: Audio Sink") ||
        strstr(out, "UUID: Audio Source") ||
        strstr(out, "UUID: Headset") ||
        strstr(out, "UUID: A/V Remote") ||
        strstr(out, "Icon: audio-") ||
        strstr(out, "Icon: headset") ||
        strstr(out, "Icon: handsfree") ||
        strstr(out, "Icon: speaker")) {
        return 1;
    }
    return 0;
}

static void update_device_state(BtDevice *d) {
    if (!d || !mac_is_safe(d->mac)) return;
    char cmd[256];
    char out[8192];
    snprintf(cmd, sizeof(cmd), "bluetoothctl --timeout 3 info %s 2>/dev/null", d->mac);
    if (read_command(cmd, out, sizeof(out)) != 0) return;
    d->connected = (strstr(out, "Connected: yes") != NULL) ? 1 : 0;
    d->paired = (strstr(out, "Paired: yes") != NULL) ? 1 : 0;
    d->trusted = (strstr(out, "Trusted: yes") != NULL) ? 1 : 0;
    d->audio_capable = is_audio_capable(d->mac);
}

void bluetooth_service_init(void) {
    s_count = 0;
    s_available = (access("/usr/bin/bluetoothctl", X_OK) == 0 || access("/bin/bluetoothctl", X_OK) == 0) ? 1 : 0;
}

void bluetooth_service_shutdown(void) {
}

int bluetooth_service_is_available(void) {
    return s_available;
}

int bluetooth_service_set_power(int on) {
    if (!s_available) return -1;
    if (on) {
        if (run_command("bluetoothctl --timeout 4 power on >/dev/null 2>&1") != 0) return -1;
    } else {
        if (run_command("bluetoothctl --timeout 4 power off >/dev/null 2>&1") != 0) return -1;
    }
    return 0;
}

int bluetooth_service_get_power(void) {
    if (!s_available) return 0;
    char out[4096];
    if (read_command("bluetoothctl --timeout 3 show 2>/dev/null", out, sizeof(out)) != 0) return 0;
    return (strstr(out, "Powered: yes") != NULL) ? 1 : 0;
}

int bluetooth_service_refresh_devices(void) {
    if (!s_available) return -1;

    s_count = 0;
    run_command("bluetoothctl --timeout 5 scan on >/dev/null 2>&1");

    char out[16384];
    if (read_command("bluetoothctl --timeout 3 devices 2>/dev/null", out, sizeof(out)) != 0) {
        run_command("bluetoothctl --timeout 2 scan off >/dev/null 2>&1");
        return -1;
    }
    run_command("bluetoothctl --timeout 2 scan off >/dev/null 2>&1");

    char *save = NULL;
    char *line = strtok_r(out, "\n", &save);
    while (line && s_count < BT_MAX_DEVICES) {
        if (strncmp(line, "Device ", 7) == 0) {
            char mac[24] = {0};
            char name[96] = {0};
            if (sscanf(line, "Device %23s %95[^\n]", mac, name) >= 1) {
                BtDevice d;
                memset(&d, 0, sizeof(d));
                snprintf(d.mac, sizeof(d.mac), "%s", mac);
                if (name[0] != '\0') snprintf(d.name, sizeof(d.name), "%s", name);
                else snprintf(d.name, sizeof(d.name), "%s", mac);
                update_device_state(&d);
                if (d.audio_capable) {
                    s_devices[s_count++] = d;
                }
            }
        }
        line = strtok_r(NULL, "\n", &save);
    }

    return 0;
}

size_t bluetooth_service_device_count(void) {
    return s_count;
}

const BtDevice *bluetooth_service_device_at(size_t index) {
    if (index >= s_count) return NULL;
    return &s_devices[index];
}

int bluetooth_service_connect(const char *mac) {
    if (!s_available || !mac_is_safe(mac)) return -1;
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "bluetoothctl --timeout 3 trust %s >/dev/null 2>&1; bluetoothctl --timeout 6 pair %s >/dev/null 2>&1; bluetoothctl --timeout 4 connect %s >/dev/null 2>&1", mac, mac, mac);
    if (run_command(cmd) != 0) return -1;
    for (size_t i = 0; i < s_count; i++) {
        if (strcmp(s_devices[i].mac, mac) == 0) {
            update_device_state(&s_devices[i]);
            break;
        }
    }
    return 0;
}

int bluetooth_service_disconnect(const char *mac) {
    if (!s_available || !mac_is_safe(mac)) return -1;
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "bluetoothctl --timeout 3 disconnect %s >/dev/null 2>&1", mac);
    if (run_command(cmd) != 0) return -1;
    for (size_t i = 0; i < s_count; i++) {
        if (strcmp(s_devices[i].mac, mac) == 0) {
            update_device_state(&s_devices[i]);
            break;
        }
    }
    return 0;
}

int bluetooth_service_remove(const char *mac) {
    if (!s_available || !mac_is_safe(mac)) return -1;
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "bluetoothctl --timeout 3 remove %s >/dev/null 2>&1", mac);
    return run_command(cmd);
}
