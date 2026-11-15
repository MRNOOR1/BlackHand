#include "ui-ipcs/audio_ipc.h"
#include "mp3_service.h"
#include <stdatomic.h>
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
static atomic_int      s_scan_running         = 0;
static atomic_int      s_scan_stop_requested  = 0;
static pthread_t         s_scan_thread;
static atomic_int      s_connect_running = 0;
static pthread_t         s_connect_thread;
static char              s_connect_mac[24]   = {0};
/* MAC of the currently connected BT headset; empty when none is connected.
 * Read by voice_memo_service to choose between SCO (BT mic) and ALSA default. */
static char              s_connected_mac[24] = {0};

/* Guards s_devices[], s_count and s_connected_mac, which are written by the
 * detached scan/connect/refresh threads and read by the render thread.
 * Never hold this across system()/popen()/bluetoothctl calls — those block
 * for seconds and would stall whichever thread is waiting on the lock. */
static pthread_mutex_t   s_devices_mutex = PTHREAD_MUTEX_INITIALIZER;

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
    return pclose(p) == 0 ? 0 : -1;
}

static void run_command(const char *cmd)
{
    if (cmd) (void)system(cmd);
}

/* Fire-and-forget via a detached thread. */
static void *bg_cmd_thread(void *arg)
{
    char *cmd = (char *)arg;
    (void)system(cmd);
    free(cmd);
    return NULL;
}

static void run_command_bg(const char *cmd)
{
    if (!cmd) return;
    char *copy = strdup(cmd);
    if (!copy) return;
    pthread_t t;
    if (pthread_create(&t, NULL, bg_cmd_thread, copy) != 0)
    {
        free(copy);
        return;
    }
    pthread_detach(t);
}


/* ── ALSA routing ────────────────────────────────────────────────────── */

static void set_alsa_bt_sink(const char *mac)
{
    mp3_service_stop();
    if (audio_ipc_route(mac) != 0) fprintf(stderr, "bluetooth: audio route failed\n");
}
static void restore_alsa_default(void)
{
    mp3_service_stop();
    if (audio_ipc_route("") != 0) fprintf(stderr, "bluetooth: audio fallback failed\n");
}

/* Disconnect then restore ALSA — runs in a background thread so ALSA is only
 * rerouted to local AFTER the BT link has closed, not before. */
static void *disconnect_and_restore_thread_fn(void *arg)
{
    char *cmd = (char *)arg;
    (void)system(cmd);
    free(cmd);
    restore_alsa_default();
    return NULL;
}

static void run_disconnect_bg(const char *cmd)
{
    if (!cmd) return;
    char *copy = strdup(cmd);
    if (!copy) {
        restore_alsa_default();
        return;
    }
    pthread_t t;
    if (pthread_create(&t, NULL, disconnect_and_restore_thread_fn, copy) != 0)
    {
        free(copy);
        restore_alsa_default();
        return;
    }
    pthread_detach(t);
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

/* Returns the index of `mac` in s_devices, or (size_t)-1 if absent.
 * Caller must hold s_devices_mutex. */
static size_t find_device_locked(const char *mac)
{
    for (size_t i = 0; i < s_count; i++)
        if (strcmp(s_devices[i].mac, mac) == 0) return i;
    return (size_t)-1;
}

/* Takes a MAC rather than a BtDevice* on purpose: bluetoothctl info blocks
 * for up to 2 s, and s_devices can be reshaped (bluetooth_service_remove's
 * memmove) while we wait. Re-resolving by MAC under the lock afterward
 * avoids writing through a pointer/index that may no longer be valid. */
static void update_device_state(const char *mac)
{
    if (!mac_is_safe(mac)) return;
    char cmd[256];
    char out[8192];
    snprintf(cmd, sizeof(cmd),
             "bluetoothctl --timeout 2 info %s 2>/dev/null", mac);
    if (read_command(cmd, out, sizeof(out)) != 0) return;

    int connected     = (strstr(out, "Connected: yes")  != NULL) ? 1 : 0;
    int paired        = (strstr(out, "Paired: yes")     != NULL) ? 1 : 0;
    int trusted       = (strstr(out, "Trusted: yes")    != NULL) ? 1 : 0;
    int audio_capable = (strstr(out, "UUID: Audio Sink")   != NULL ||
                         strstr(out, "UUID: Audio Source")  != NULL ||
                         strstr(out, "UUID: Headset")        != NULL ||
                         strstr(out, "UUID: A/V Remote")     != NULL ||
                         strstr(out, "Icon: audio-")          != NULL ||
                         strstr(out, "Icon: headset")          != NULL) ? 1 : 0;

    /* Extract human-readable name (overrides MAC-as-name from device list) */
    char name_buf[96] = {0};
    const char *np = strstr(out, "\tName: ");
    if (!np) np = strstr(out, "\tAlias: ");
    if (np)
    {
        np += (np[1] == 'N') ? 7 : 8; /* skip "\tName: " (7) or "\tAlias: " (8) */
        size_t i = 0;
        while (np[i] && np[i] != '\n' && np[i] != '\r' && i < sizeof(name_buf) - 1)
            name_buf[i] = np[i], i++;
        name_buf[i] = '\0';
    }

    pthread_mutex_lock(&s_devices_mutex);
    size_t idx = find_device_locked(mac);
    if (idx != (size_t)-1)
    {
        s_devices[idx].connected     = connected;
        s_devices[idx].paired        = paired;
        s_devices[idx].trusted       = trusted;
        s_devices[idx].audio_capable = audio_capable;
        /* Only use if it's a real name, not another MAC address */
        if (name_buf[0] && !mac_is_safe(name_buf))
            strncpy(s_devices[idx].name, name_buf, sizeof(s_devices[idx].name) - 1);
    }
    pthread_mutex_unlock(&s_devices_mutex);
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
    pthread_mutex_lock(&s_devices_mutex);
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
    pthread_mutex_unlock(&s_devices_mutex);
}

/* ── Scan helper (shared by sync and async paths) ───────────────────── */

static void detect_connected_devices(void); /* forward declaration */

static void do_scan_and_list(void)
{
    /* Load already-paired devices first so they appear even if not nearby */
    pthread_mutex_lock(&s_devices_mutex);
    s_count = 0;
    pthread_mutex_unlock(&s_devices_mutex);
    char paired_out[16384];
    read_command("bluetoothctl paired-devices 2>/dev/null", paired_out, sizeof(paired_out));
    populate_from_output(paired_out);
    /* Mark all pre-loaded entries as paired */
    pthread_mutex_lock(&s_devices_mutex);
    for (size_t i = 0; i < s_count; i++)
        s_devices[i].paired = 1;
    pthread_mutex_unlock(&s_devices_mutex);

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
        time_t scan_start = time(NULL);
        while (fgets(buf, sizeof(buf), sf) && !s_scan_stop_requested)
        {
            /* Check for stop request periodically to exit scan early */
            time_t now = time(NULL);
            if (s_scan_stop_requested && (now - scan_start > 2))
            {
                /* Drain remaining output without processing to clean up popen */
                char drain[512];
                while (fgets(drain, sizeof(drain), sf)) {}
                break;
            }
            
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
            pthread_mutex_lock(&s_devices_mutex);
            size_t idx = find_device_locked(mac);
            if (idx == (size_t)-1 && s_count < BT_MAX_DEVICES)
            {
                idx = s_count;
                memset(&s_devices[idx], 0, sizeof(BtDevice));
                snprintf(s_devices[idx].mac, sizeof(s_devices[idx].mac), "%s", mac);
                s_count++;
            }
            if (idx < s_count)
                snprintf(s_devices[idx].name, sizeof(s_devices[idx].name), "%s", name_buf);
            pthread_mutex_unlock(&s_devices_mutex);
        }
        pclose(sf);
    }

    /* Update connected flags now that the full device list is known */
    detect_connected_devices();
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
                pthread_mutex_lock(&s_devices_mutex);
                size_t idx = find_device_locked(mac);
                /* audio_capable is only populated by update_device_state()
                 * (connect/refresh flows); a device that auto-reconnected
                 * at boot or via background scan never went through those,
                 * so query it here or routing to a real headset silently
                 * never happens. */
                int need_state = (idx != (size_t)-1) && !s_devices[idx].audio_capable;
                pthread_mutex_unlock(&s_devices_mutex);

                if (idx != (size_t)-1)
                {
                    if (need_state) update_device_state(mac);

                    pthread_mutex_lock(&s_devices_mutex);
                    idx = find_device_locked(mac); /* array may have been reshaped */
                    int route = 0;
                    if (idx != (size_t)-1)
                    {
                        s_devices[idx].connected = 1;
                        /* First connected device wins for ALSA routing */
                        route = !s_connected_mac[0] && s_devices[idx].audio_capable;
                    }
                    pthread_mutex_unlock(&s_devices_mutex);

                    if (route)
                    {
                        set_alsa_bt_sink(mac);
                        pthread_mutex_lock(&s_devices_mutex);
                        strncpy(s_connected_mac, mac, sizeof(s_connected_mac) - 1);
                        s_connected_mac[sizeof(s_connected_mac) - 1] = '\0';
                        pthread_mutex_unlock(&s_devices_mutex);
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
    pthread_mutex_lock(&s_devices_mutex);
    s_count = 0;
    pthread_mutex_unlock(&s_devices_mutex);
    char out[16384];
    read_command("bluetoothctl paired-devices 2>/dev/null", out, sizeof(out));
    populate_from_output(out);
    /* Mark all as paired and filter out MAC-only name entries */
    pthread_mutex_lock(&s_devices_mutex);
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
    pthread_mutex_unlock(&s_devices_mutex);

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
    pthread_mutex_lock(&s_devices_mutex);
    s_count = 0;
    pthread_mutex_unlock(&s_devices_mutex);
    s_available = (access("/usr/bin/bluetoothctl", X_OK) == 0 ||
                   access("/bin/bluetoothctl",     X_OK) == 0) ? 1 : 0;
    /* Pre-populate paired devices at startup so they're visible before the
     * first scan completes. Also detects any headset that auto-reconnected. */
    bluetooth_service_load_paired();
}

/* The scan thread can run up to ~10.5 s and the connect thread up to ~23 s
 * (pair 10 s + trust 3 s + connect 8 s + a 2 s post-connect settle), both
 * blocked inside system()/popen() with no cancellable point. init.c grants
 * only a 5 s shutdown grace, so these detached threads can never be joined
 * unconditionally here. Request the scan to stop and wait a short, bounded
 * window for both to finish; if they haven't, return anyway — they keep
 * running detached and exit on their own, or are torn down with the process. */
void bluetooth_service_shutdown(void)
{
    bluetooth_service_scan_stop();

    const int poll_ms   = 100;
    const int max_ms     = 1500; /* well inside the 5 s shutdown grace */
    for (int waited = 0; waited < max_ms; waited += poll_ms)
    {
        if (!s_scan_running && !s_connect_running) break;
        usleep(poll_ms * 1000);
    }
}

int bluetooth_service_is_available(void)
{
    return s_available;
}

int bluetooth_service_set_power(int on)
{
    if (!s_available) return -1;
    /* Do not race an active pairing command with adapter shutdown. */
    if (s_connect_running) return -1;
    if (!on) bluetooth_service_scan_stop();
    int rc = system(on ? "bluetoothctl --timeout 4 power on >/dev/null 2>&1"
                       : "bluetoothctl --timeout 4 power off >/dev/null 2>&1");
    if (rc != 0 || bluetooth_service_get_power() != !!on) return -1;
    if (!on)
    {
        pthread_mutex_lock(&s_devices_mutex);
        int had_connected = s_connected_mac[0] != '\0';
        pthread_mutex_unlock(&s_devices_mutex);

        if (had_connected)
        {
            restore_alsa_default();
            pthread_mutex_lock(&s_devices_mutex);
            s_connected_mac[0] = '\0';
            for (size_t i = 0; i < s_count; i++) s_devices[i].connected = 0;
            pthread_mutex_unlock(&s_devices_mutex);
        }
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
    /* Fire and forget — the radio stops in the background; the UI returns instantly */
    if (s_available)
        run_command_bg("bluetoothctl --timeout 2 scan off >/dev/null 2>&1");
}

int bluetooth_service_scan_is_running(void)
{
    return s_scan_running;
}

size_t bluetooth_service_device_count(void)
{
    pthread_mutex_lock(&s_devices_mutex);
    size_t n = s_count;
    pthread_mutex_unlock(&s_devices_mutex);
    return n;
}

/* Copies the device at `index` into *out rather than returning a pointer
 * into s_devices — a pointer handed to another thread could be dereferenced
 * after a concurrent bluetooth_service_remove() memmove has invalidated it.
 * Returns 1 and fills *out on success, 0 (leaving *out untouched) otherwise. */
int bluetooth_service_device_at(size_t index, BtDevice *out)
{
    if (!out) return 0;
    pthread_mutex_lock(&s_devices_mutex);
    int found = index < s_count;
    if (found) *out = s_devices[index];
    pthread_mutex_unlock(&s_devices_mutex);
    return found;
}

int bluetooth_service_refresh_device(const char *mac)
{
    if (!s_available || !mac_is_safe(mac)) return -1;

    pthread_mutex_lock(&s_devices_mutex);
    int exists = find_device_locked(mac) != (size_t)-1;
    pthread_mutex_unlock(&s_devices_mutex);
    if (!exists) return -1;

    update_device_state(mac);

    pthread_mutex_lock(&s_devices_mutex);
    size_t idx = find_device_locked(mac);
    int connected = (idx != (size_t)-1) ? s_devices[idx].connected : 0;
    pthread_mutex_unlock(&s_devices_mutex);
    if (idx == (size_t)-1) return -1; /* removed while we were querying it */

    if (connected)
    {
        set_alsa_bt_sink(mac);
        pthread_mutex_lock(&s_devices_mutex);
        strncpy(s_connected_mac, mac, sizeof(s_connected_mac) - 1);
        s_connected_mac[sizeof(s_connected_mac) - 1] = '\0';
        pthread_mutex_unlock(&s_devices_mutex);
    }
    else
    {
        pthread_mutex_lock(&s_devices_mutex);
        if (strcmp(s_connected_mac, mac) == 0) s_connected_mac[0] = '\0';
        pthread_mutex_unlock(&s_devices_mutex);
    }
    return connected;
}

static void *connect_thread_fn(void *arg)
{
    (void)arg;

    /* Skip trust+pair for devices we already know are paired.
     * Re-pairing a bonded device can confuse some headsets. */
    int already_paired = 0;
    pthread_mutex_lock(&s_devices_mutex);
    size_t paired_idx = find_device_locked(s_connect_mac);
    if (paired_idx != (size_t)-1) already_paired = s_devices[paired_idx].paired;
    pthread_mutex_unlock(&s_devices_mutex);

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
                 "bluetoothctl --timeout 10 pair %s >/dev/null 2>&1 && "
                 "bluetoothctl --timeout 3 trust %s >/dev/null 2>&1 && "
                 "bluetoothctl --timeout 8  connect %s >/dev/null 2>&1",
                 s_connect_mac, s_connect_mac, s_connect_mac);
    }
    (void)system(cmd);

    /* Give BlueALSA time to negotiate the A2DP codec and open the stream
     * before we route ALSA to it. Without this sleep the bluealsa PCM may
     * not be ready yet and the first open() call will fail. */
    sleep(2);

    pthread_mutex_lock(&s_devices_mutex);
    int connect_target_exists = find_device_locked(s_connect_mac) != (size_t)-1;
    pthread_mutex_unlock(&s_devices_mutex);

    if (connect_target_exists)
    {
        update_device_state(s_connect_mac);

        pthread_mutex_lock(&s_devices_mutex);
        size_t idx = find_device_locked(s_connect_mac);
        int connected = (idx != (size_t)-1) ? s_devices[idx].connected : 0;
        pthread_mutex_unlock(&s_devices_mutex);

        if (idx != (size_t)-1)
        {
            if (connected)
            {
                set_alsa_bt_sink(s_connect_mac);
                pthread_mutex_lock(&s_devices_mutex);
                strncpy(s_connected_mac, s_connect_mac, sizeof(s_connected_mac) - 1);
                s_connected_mac[sizeof(s_connected_mac) - 1] = '\0';
                pthread_mutex_unlock(&s_devices_mutex);
            }
            else
            {
                pthread_mutex_lock(&s_devices_mutex);
                s_connected_mac[0] = '\0';
                pthread_mutex_unlock(&s_devices_mutex);
            }
        }
    }
    s_connect_running = 0;
    return NULL;
}

/* ── Async device state refresh ──────────────────────────────────────────── */

static char              s_refresh_mac[24]   = {0};
static atomic_int      s_refresh_running = 0;
static pthread_t         s_refresh_thread;

static void *refresh_device_thread_fn(void *arg)
{
    (void)arg;

    pthread_mutex_lock(&s_devices_mutex);
    int exists = find_device_locked(s_refresh_mac) != (size_t)-1;
    pthread_mutex_unlock(&s_devices_mutex);

    if (exists) update_device_state(s_refresh_mac);

    s_refresh_running = 0;
    return NULL;
}

int bluetooth_service_refresh_device_async(const char *mac)
{
    if (!s_available || !mac_is_safe(mac)) return -1;
    if (s_refresh_running) return 0;  /* Already refreshing */
    
    strncpy(s_refresh_mac, mac, sizeof(s_refresh_mac) - 1);
    s_refresh_mac[sizeof(s_refresh_mac) - 1] = '\0';
    s_refresh_running = 1;
    
    if (pthread_create(&s_refresh_thread, NULL, refresh_device_thread_fn, NULL) != 0)
    {
        s_refresh_running = 0;
        return -1;
    }
    pthread_detach(s_refresh_thread);
    return 0;
}

int bluetooth_service_connect_async(const char *mac)
{
    if (!s_available || !mac_is_safe(mac)) return -1;
    if (s_connect_running) return -1;
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

    pthread_mutex_lock(&s_devices_mutex);
    int exists = find_device_locked(mac) != (size_t)-1;
    pthread_mutex_unlock(&s_devices_mutex);
    if (exists) update_device_state(mac);
    return 0;
}

int bluetooth_service_disconnect(const char *mac)
{
    if (!s_available || !mac_is_safe(mac)) return -1;
    char cmd[256];
    snprintf(cmd, sizeof(cmd),
             "bluetoothctl --timeout 3 disconnect %s >/dev/null 2>&1", mac);

    pthread_mutex_lock(&s_devices_mutex);
    int was_connected_mac = strcmp(s_connected_mac, mac) == 0;
    if (was_connected_mac) s_connected_mac[0] = '\0';
    /* Optimistic update — the UI reflects disconnected immediately */
    size_t idx = find_device_locked(mac);
    if (idx != (size_t)-1) s_devices[idx].connected = 0;
    pthread_mutex_unlock(&s_devices_mutex);

    if (was_connected_mac) {
        run_disconnect_bg(cmd);
    } else {
        run_command_bg(cmd);
    }
    return 0;
}

int bluetooth_service_remove(const char *mac)
{
    if (!s_available || !mac_is_safe(mac)) return -1;
    char cmd[256];
    snprintf(cmd, sizeof(cmd),
             "bluetoothctl --timeout 3 remove %s >/dev/null 2>&1", mac);
    if (s_connect_running) return -1;
    if (system(cmd) != 0) return -1;

    pthread_mutex_lock(&s_devices_mutex);
    int was_connected_mac = strcmp(s_connected_mac, mac) == 0;
    if (was_connected_mac) s_connected_mac[0] = '\0';
    pthread_mutex_unlock(&s_devices_mutex);
    if (was_connected_mac) restore_alsa_default();

    pthread_mutex_lock(&s_devices_mutex);
    size_t idx = find_device_locked(mac);
    if (idx != (size_t)-1)
    {
        if (idx + 1 < s_count)
            memmove(&s_devices[idx], &s_devices[idx + 1],
                    (s_count - idx - 1) * sizeof(BtDevice));
        s_count--;
    }
    pthread_mutex_unlock(&s_devices_mutex);
    return 0;
}

void bluetooth_service_get_connected_mac(char *out, size_t sz)
{
    if (!out || sz == 0) return;
    pthread_mutex_lock(&s_devices_mutex);
    strncpy(out, s_connected_mac, sz - 1);
    pthread_mutex_unlock(&s_devices_mutex);
    out[sz - 1] = '\0';
}
