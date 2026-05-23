#include "bluetooth_service.h"

#include <ctype.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static BtDevice          s_devices[BT_MAX_DEVICES];
static size_t            s_count       = 0;
static int               s_available   = 0;
static volatile int      s_scan_running         = 0;
static volatile int      s_scan_stop_requested  = 0;
static pthread_t         s_scan_thread;
static volatile int      s_connect_running = 0;
static pthread_t         s_connect_thread;
static char              s_connect_mac[24]   = {0};
/* MAC of the currently connected BT headset; empty when none is connected.
 * Read by voice_memo_service to choose between SCO (BT mic) and ALSA default. */
static char              s_connected_mac[24] = {0};

/* ── Subprocess helpers ──────────────────────────────────────────────── */

static int read_command(const char *cmd, char *out, size_t out_sz)
{
    if (!cmd || !out || out_sz == 0) return -1;
    out[0] = '\0';
    FILE *p = popen(cmd, "r");
    if (!p) return -1;
    size_t n = 0;
    while (!feof(p) && n + 1 < out_sz)
    {
        size_t r = fread(out + n, 1, out_sz - n - 1, p);
        if (r == 0) break;
        n += r;
    }
    out[n] = '\0';
    pclose(p);
    return 0;
}

static void run_command(const char *cmd)
{
    if (cmd) system(cmd);
}

/* ── ALSA routing ────────────────────────────────────────────────────── */

/* Read the physical card number that rcS chose at boot */
static int read_boot_card(void)
{
    int card = 0;
    FILE *cf = fopen("/tmp/bh-audio-card", "r");
    if (cf) { fscanf(cf, "%d", &card); fclose(cf); }
    return card;
}

/* Route ALSA default PCM through BlueALSA for the connected headset.
 * BlueALSA creates a virtual PCM (type bluealsa) once the A2DP codec
 * is negotiated.  Setting asound.conf here routes all ALSA playback
 * through the headset and maps the default ctl to AVRCP volume. */
static void set_alsa_bt_sink(const char *mac)
{
    FILE *f = fopen("/etc/asound.conf", "w");
    if (!f) return;
    /* Named pcm.bt first, then alias default to it via plug for
     * automatic format/rate conversion.  Inline slave.pcm {} blocks
     * cause parse errors on older ALSA library versions. */
    fprintf(f,
        "pcm.bt {\n"
        "    type bluealsa\n"
        "    device \"%s\"\n"
        "    profile \"a2dp\"\n"
        "}\n"
        "pcm.!default {\n"
        "    type plug\n"
        "    slave.pcm \"bt\"\n"
        "}\n"
        "ctl.!default {\n"
        "    type bluealsa\n"
        "    device \"%s\"\n"
        "}\n",
        mac, mac);
    fclose(f);
}

/* Restore ALSA to the physical card chosen at boot */
static void restore_alsa_default(void)
{
    int card = read_boot_card();
    FILE *f = fopen("/etc/asound.conf", "w");
    if (!f) return;
    fprintf(f,
        "pcm.!default {\n"
        "    type plug\n"
        "    slave.pcm \"hw:%d,0\"\n"
        "}\n"
        "ctl.!default {\n"
        "    type hw\n"
        "    card %d\n"
        "}\n",
        card, card);
    fclose(f);
}

/* ── ANSI escape stripper ────────────────────────────────────────────── */

static void strip_ansi(char *buf)
{
    char *src = buf, *dst = buf;
    while (*src)
    {
        if (*src == '\033' && *(src + 1) == '[')
        {
            src += 2;
            while (*src && *src != 'm') src++;
            if (*src == 'm') src++;
        }
        else
        {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

/* ── Input validation ────────────────────────────────────────────────── */

/* Returns 1 if s looks like a MAC address in dash format: AA-BB-CC-DD-EE-FF
 * bluetoothctl uses this as a placeholder name when no name is known yet. */
static int name_is_dash_mac(const char *s)
{
    if (!s || strlen(s) != 17) return 0;
    for (size_t i = 0; i < 17; i++)
    {
        char c = s[i];
        if (i % 3 == 2) { if (c != '-') return 0; }
        else             { if (!isxdigit((unsigned char)c)) return 0; }
    }
    return 1;
}

int mac_is_safe(const char *mac)
{
    if (!mac) return 0;
    size_t len = strlen(mac);
    if (len != 17) return 0;
    for (size_t i = 0; i < len; i++)
    {
        char c = mac[i];
        if (i % 3 == 2)
        {
            if (c != ':') return 0;
        }
        else
        {
            if (!isxdigit((unsigned char)c)) return 0;
        }
    }
    return 1;
}

/* ── Device info ─────────────────────────────────────────────────────── */

static void update_device_state(BtDevice *d)
{
    if (!d || !mac_is_safe(d->mac)) return;
    char cmd[256];
    char out[8192];
    snprintf(cmd, sizeof(cmd),
             "bluetoothctl --timeout 2 info %s 2>/dev/null", d->mac);
    if (read_command(cmd, out, sizeof(out)) != 0) return;

    d->connected     = (strstr(out, "Connected: yes")  != NULL) ? 1 : 0;
    d->paired        = (strstr(out, "Paired: yes")     != NULL) ? 1 : 0;
    d->trusted       = (strstr(out, "Trusted: yes")    != NULL) ? 1 : 0;
    d->audio_capable = (strstr(out, "UUID: Audio Sink")   != NULL ||
                        strstr(out, "UUID: Audio Source")  != NULL ||
                        strstr(out, "UUID: Headset")        != NULL ||
                        strstr(out, "UUID: A/V Remote")     != NULL ||
                        strstr(out, "Icon: audio-")          != NULL ||
                        strstr(out, "Icon: headset")          != NULL) ? 1 : 0;

    /* Extract human-readable name (overrides MAC-as-name from device list) */
    const char *np = strstr(out, "\tName: ");
    if (!np) np = strstr(out, "\tAlias: ");
    if (np)
    {
        np += (np[1] == 'N') ? 7 : 8; /* skip "\tName: " (7) or "\tAlias: " (8) */
        char name_buf[96] = {0};
        size_t i = 0;
        while (np[i] && np[i] != '\n' && np[i] != '\r' && i < sizeof(name_buf) - 1)
            name_buf[i] = np[i], i++;
        name_buf[i] = '\0';
        /* Only use if it's a real name, not another MAC address */
        if (name_buf[0] && !mac_is_safe(name_buf))
            strncpy(d->name, name_buf, sizeof(d->name) - 1);
    }
}

/* ── Parse "bluetoothctl devices" output into s_devices ─────────────── */

static void populate_from_output(const char *out)
{
    char buf[16384];
    strncpy(buf, out, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    strip_ansi(buf);

    char *save = NULL;
    char *line = strtok_r(buf, "\n", &save);
    while (line && s_count < BT_MAX_DEVICES)
    {
        /* Handle both plain "Device MAC Name" and interactive
         * "[bluetooth]# Device MAC Name" with or without ANSI codes. */
        char *p = strstr(line, "Device ");
        if (p)
        {
            p += 7;
            char mac[24] = {0};
            char name[96] = {0};
            sscanf(p, "%23s %95[^\n\r]", mac, name);

            if (mac_is_safe(mac))
            {
                BtDevice d;
                memset(&d, 0, sizeof(d));
                snprintf(d.mac, sizeof(d.mac), "%s", mac);
                snprintf(d.name, sizeof(d.name), "%s",
                         name[0] ? name : mac);
                /* Skip per-device bluetoothctl info during scan — querying
                 * each device adds 3 s × N devices of delay. Status is
                 * fetched on demand when the user opens the device detail. */
                s_devices[s_count++] = d;
            }
        }
        line = strtok_r(NULL, "\n", &save);
    }
}

/* ── Scan helper (shared by sync and async paths) ───────────────────── */

static void do_scan_and_list(void)
{
    /* Load already-paired devices first so they appear even if not nearby */
    s_count = 0;
    char paired_out[16384];
    read_command("bluetoothctl paired-devices 2>/dev/null", paired_out, sizeof(paired_out));
    populate_from_output(paired_out);
    /* Mark all pre-loaded entries as paired */
    for (size_t i = 0; i < s_count; i++)
        s_devices[i].paired = 1;

    /* Run interactive scan and parse [NEW]/[CHG] Name events in real time.
     * 10 seconds covers at least one full BR/EDR inquiry cycle (10.24 s)
     * so Classic BT headsets and phones in pairing mode are reliably seen.
     * LE devices appear within the first second, so the extra time is free. */
    FILE *sf = popen(
        "(echo 'scan on'; sleep 10; echo 'scan off'; sleep 0.5) | bluetoothctl 2>/dev/null",
        "r");
    if (sf)
    {
        char buf[512];
        while (fgets(buf, sizeof(buf), sf))
        {
            strip_ansi(buf);
            char mac[24]     = {0};
            char name_buf[96] = {0};

            /* ── [NEW] Device MAC RealName ──────────────────────────────
             * Already-known devices (paired/seen before) appear here with
             * their stored name embedded directly in the NEW event. */
            char *p_new = strstr(buf, "[NEW] Device ");
            if (p_new)
            {
                char raw_name[96] = {0};
                p_new += 13;
                if (sscanf(p_new, "%23s %95[^\n\r]", mac, raw_name) == 2
                    && mac_is_safe(mac)
                    && raw_name[0]
                    && !mac_is_safe(raw_name)
                    && !name_is_dash_mac(raw_name))
                {
                    strncpy(name_buf, raw_name, sizeof(name_buf) - 1);
                }
                else { mac[0] = '\0'; }
            }

            /* ── [CHG] Device MAC Name: RealName  (or Alias: RealName) ────
             * Name resolves from the advertising packet or remote name req.
             * Some devices (e.g. Samsung monitors) emit Alias: but not Name:.
             * Prefer Name: over Alias: when both appear in the same cycle. */
            char *p_chg = strstr(buf, "[CHG] Device ");
            if (p_chg && !mac[0])
            {
                char rest[256] = {0};
                p_chg += 13;
                if (sscanf(p_chg, "%23s %255[^\n\r]", mac, rest) == 2
                    && mac_is_safe(mac)
                    && (strncmp(rest, "Name: ",  6) == 0 ||
                        strncmp(rest, "Alias: ", 7) == 0))
                {
                    int skip = (rest[0] == 'N') ? 6 : 7; /* "Name: " or "Alias: " */
                    strncpy(name_buf, rest + skip, sizeof(name_buf) - 1);
                    /* strip trailing whitespace */
                    size_t nlen = strlen(name_buf);
                    while (nlen > 0 && (name_buf[nlen-1] == '\r' ||
                                        name_buf[nlen-1] == '\n' ||
                                        name_buf[nlen-1] == ' '))
                        name_buf[--nlen] = '\0';
                    if (!name_buf[0] || mac_is_safe(name_buf))
                        mac[0] = '\0';
                }
                else { mac[0] = '\0'; }
            }

            if (!mac[0] || !name_buf[0]) continue;

            /* Find existing entry or add new one */
            size_t idx = s_count;
            for (size_t i = 0; i < s_count; i++)
                if (strcmp(s_devices[i].mac, mac) == 0) { idx = i; break; }

            if (idx == s_count && s_count < BT_MAX_DEVICES)
            {
                memset(&s_devices[s_count], 0, sizeof(BtDevice));
                snprintf(s_devices[s_count].mac, sizeof(s_devices[s_count].mac), "%s", mac);
                s_count++;
            }
            if (idx < s_count)
                snprintf(s_devices[idx].name, sizeof(s_devices[idx].name), "%s", name_buf);
        }
        pclose(sf);
    }

}

/* ── Load paired devices without scanning ───────────────────────────── */

/* Detect any already-connected devices and populate s_connected_mac.
 * Uses "bluetoothctl devices Connected" which is fast (no scanning). */
static void detect_connected_devices(void)
{
    char raw[4096];
    read_command("bluetoothctl devices Connected 2>/dev/null", raw, sizeof(raw));
    char buf[4096];
    strncpy(buf, raw, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    strip_ansi(buf);

    char *save = NULL;
    char *line = strtok_r(buf, "\n", &save);
    while (line)
    {
        char *p = strstr(line, "Device ");
        if (p)
        {
            char mac[24] = {0};
            sscanf(p + 7, "%23s", mac);
            if (mac_is_safe(mac))
            {
                for (size_t i = 0; i < s_count; i++)
                {
                    if (strcmp(s_devices[i].mac, mac) == 0)
                    {
                        s_devices[i].connected = 1;
                        /* First connected device wins for ALSA routing */
                        if (!s_connected_mac[0])
                        {
                            set_alsa_bt_sink(mac);
                            strncpy(s_connected_mac, mac, sizeof(s_connected_mac) - 1);
                            s_connected_mac[sizeof(s_connected_mac) - 1] = '\0';
                        }
                        break;
                    }
                }
            }
        }
        line = strtok_r(NULL, "\n", &save);
    }
}

void bluetooth_service_load_paired(void)
{
    if (!s_available) return;
    s_count = 0;
    char out[16384];
    read_command("bluetoothctl paired-devices 2>/dev/null", out, sizeof(out));
    populate_from_output(out);
    /* Mark all as paired and filter out MAC-only name entries */
    size_t j = 0;
    for (size_t i = 0; i < s_count; i++)
    {
        if (s_devices[i].name[0] && !mac_is_safe(s_devices[i].name))
        {
            s_devices[i].paired = 1;
            s_devices[j++] = s_devices[i];
        }
    }
    s_count = j;

    /* Populate s_connected_mac for any headset that auto-reconnected at boot */
    detect_connected_devices();
}

/* ── Background scan thread ──────────────────────────────────────────── */

static void *scan_thread_fn(void *arg)
{
    (void)arg;
    do_scan_and_list();
    s_scan_running = 0;
    return NULL;
}

/* ── Public API ──────────────────────────────────────────────────────── */

void bluetooth_service_init(void)
{
    s_count = 0;
    s_available = (access("/usr/bin/bluetoothctl", X_OK) == 0 ||
                   access("/bin/bluetoothctl",     X_OK) == 0) ? 1 : 0;
}

void bluetooth_service_shutdown(void) {}

int bluetooth_service_is_available(void)
{
    return s_available;
}

int bluetooth_service_set_power(int on)
{
    if (!s_available) return -1;
    if (on)
    {
        if (system("bluetoothctl --timeout 4 power on >/dev/null 2>&1") != 0)
            return -1;
    }
    else
    {
        if (system("bluetoothctl --timeout 4 power off >/dev/null 2>&1") != 0)
            return -1;
    }
    return 0;
}

int bluetooth_service_get_power(void)
{
    if (!s_available) return 0;
    char out[4096];
    read_command("bluetoothctl --timeout 3 show 2>/dev/null", out, sizeof(out));
    return (strstr(out, "Powered: yes") != NULL) ? 1 : 0;
}

int bluetooth_service_refresh_devices(void)
{
    if (!s_available) return -1;
    do_scan_and_list();
    return 0;
}

int bluetooth_service_scan_start(void)
{
    if (!s_available)    return -1;
    if (s_scan_running)  return 0;
    s_scan_stop_requested = 0;
    s_scan_running = 1;
    if (pthread_create(&s_scan_thread, NULL, scan_thread_fn, NULL) != 0)
    {
        s_scan_running = 0;
        return -1;
    }
    pthread_detach(s_scan_thread);
    return 0;
}

void bluetooth_service_scan_stop(void)
{
    s_scan_stop_requested = 1;
    /* Stop the radio immediately — don't wait for the thread's pipeline to finish */
    if (s_available)
        system("bluetoothctl --timeout 2 scan off >/dev/null 2>&1");
}

int bluetooth_service_scan_is_running(void)
{
    return s_scan_running;
}

size_t bluetooth_service_device_count(void)
{
    return s_count;
}

const BtDevice *bluetooth_service_device_at(size_t index)
{
    if (index >= s_count) return NULL;
    return &s_devices[index];
}

int bluetooth_service_refresh_device(const char *mac)
{
    if (!s_available || !mac_is_safe(mac)) return -1;
    for (size_t i = 0; i < s_count; i++)
        if (strcmp(s_devices[i].mac, mac) == 0)
        {
            update_device_state(&s_devices[i]);
            if (s_devices[i].connected)
            {
                set_alsa_bt_sink(mac);
                strncpy(s_connected_mac, mac, sizeof(s_connected_mac) - 1);
                s_connected_mac[sizeof(s_connected_mac) - 1] = '\0';
            }
            else if (strcmp(s_connected_mac, mac) == 0)
            {
                s_connected_mac[0] = '\0';
            }
            return s_devices[i].connected;
        }
    return -1;
}

static void *connect_thread_fn(void *arg)
{
    (void)arg;

    /* Skip trust+pair for devices we already know are paired.
     * Re-pairing a bonded device can confuse some headsets. */
    int already_paired = 0;
    for (size_t i = 0; i < s_count; i++)
        if (strcmp(s_devices[i].mac, s_connect_mac) == 0)
        {
            already_paired = s_devices[i].paired;
            break;
        }

    char cmd[512];
    if (already_paired)
    {
        snprintf(cmd, sizeof(cmd),
                 "bluetoothctl --timeout 8 connect %s >/dev/null 2>&1",
                 s_connect_mac);
    }
    else
    {
        snprintf(cmd, sizeof(cmd),
                 "bluetoothctl --timeout 3 trust %s >/dev/null 2>&1;"
                 "bluetoothctl --timeout 10 pair %s >/dev/null 2>&1;"
                 "bluetoothctl --timeout 8  connect %s >/dev/null 2>&1",
                 s_connect_mac, s_connect_mac, s_connect_mac);
    }
    system(cmd);

    /* Give BlueALSA time to negotiate the A2DP codec and open the stream
     * before we route ALSA to it. Without this sleep the bluealsa PCM may
     * not be ready yet and the first open() call will fail. */
    sleep(2);

    for (size_t i = 0; i < s_count; i++)
        if (strcmp(s_devices[i].mac, s_connect_mac) == 0)
        {
            update_device_state(&s_devices[i]);
            if (s_devices[i].connected)
            {
                set_alsa_bt_sink(s_connect_mac);
                strncpy(s_connected_mac, s_connect_mac, sizeof(s_connected_mac) - 1);
                s_connected_mac[sizeof(s_connected_mac) - 1] = '\0';
            }
            else
            {
                s_connected_mac[0] = '\0';
            }
            break;
        }
    s_connect_running = 0;
    return NULL;
}

int bluetooth_service_connect_async(const char *mac)
{
    if (!s_available || !mac_is_safe(mac)) return -1;
    if (s_connect_running) return 0;
    strncpy(s_connect_mac, mac, sizeof(s_connect_mac) - 1);
    s_connect_mac[sizeof(s_connect_mac) - 1] = '\0';
    s_connect_running = 1;
    if (pthread_create(&s_connect_thread, NULL, connect_thread_fn, NULL) != 0)
    {
        s_connect_running = 0;
        return -1;
    }
    pthread_detach(s_connect_thread);
    return 0;
}

int bluetooth_service_connect_is_running(void)
{
    return s_connect_running;
}

int bluetooth_service_connect(const char *mac)
{
    if (!s_available || !mac_is_safe(mac)) return -1;
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
             "bluetoothctl --timeout 3 trust %s >/dev/null 2>&1;"
             "bluetoothctl --timeout 10 pair %s >/dev/null 2>&1;"
             "bluetoothctl --timeout 6  connect %s >/dev/null 2>&1",
             mac, mac, mac);
    run_command(cmd);

    for (size_t i = 0; i < s_count; i++)
        if (strcmp(s_devices[i].mac, mac) == 0)
        {
            update_device_state(&s_devices[i]);
            break;
        }
    return 0;
}

int bluetooth_service_disconnect(const char *mac)
{
    if (!s_available || !mac_is_safe(mac)) return -1;
    char cmd[256];
    snprintf(cmd, sizeof(cmd),
             "bluetoothctl --timeout 3 disconnect %s >/dev/null 2>&1", mac);
    run_command(cmd);
    if (strcmp(s_connected_mac, mac) == 0)
        s_connected_mac[0] = '\0';
    restore_alsa_default();

    for (size_t i = 0; i < s_count; i++)
        if (strcmp(s_devices[i].mac, mac) == 0)
        {
            update_device_state(&s_devices[i]);
            break;
        }
    return 0;
}

int bluetooth_service_remove(const char *mac)
{
    if (!s_available || !mac_is_safe(mac)) return -1;
    char cmd[256];
    snprintf(cmd, sizeof(cmd),
             "bluetoothctl --timeout 3 remove %s >/dev/null 2>&1", mac);
    run_command(cmd);
    if (strcmp(s_connected_mac, mac) == 0)
        s_connected_mac[0] = '\0';
    restore_alsa_default();

    for (size_t i = 0; i < s_count; i++)
        if (strcmp(s_devices[i].mac, mac) == 0)
        {
            if (i + 1 < s_count)
                memmove(&s_devices[i], &s_devices[i + 1],
                        (s_count - i - 1) * sizeof(BtDevice));
            s_count--;
            break;
        }
    return 0;
}

void bluetooth_service_get_connected_mac(char *out, size_t sz)
{
    if (!out || sz == 0) return;
    strncpy(out, s_connected_mac, sz - 1);
    out[sz - 1] = '\0';
}
