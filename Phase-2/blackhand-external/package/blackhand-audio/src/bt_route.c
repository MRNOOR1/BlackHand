/*
 * bt_route.c — query bluez for connected audio devices.
 *
 * Shells out to `bluetoothctl` instead of linking libdbus directly.
 * The architectural property the daemon needed was "daemon decides
 * routing, not the UI" — that's preserved either way. bluetoothctl
 * is ~50 lines of code vs ~150 of libdbus boilerplate, already
 * present on the image, and runs in ~30 ms. If we ever need lower
 * latency we can swap the implementation without touching callers.
 *
 * Two-pass approach per query:
 *   1. `bluetoothctl devices Connected` → list of connected MACs
 *   2. `bluetoothctl info <MAC>` → check UUIDs for A2DP-Sink / Handsfree
 */

#include "bt_route.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

/* UUID prefixes we care about. Bluez prints UUIDs in 128-bit form,
   but the short form (0000XXXX-0000-1000-8000-00805f9b34fb) is what
   bluetoothctl info shows. Match on the human-readable name instead —
   bluez prints "Audio Sink" / "Handsfree" alongside the UUID line. */
#define UUID_LABEL_A2DP_SINK  "Audio Sink"
#define UUID_LABEL_HFP_HF     "Handsfree"
#define UUID_LABEL_HFP_HS     "Headset"
#define UUID_LABEL_HSP        "Headset Audio Gateway"

static int run_and_capture(const char *cmd, char *buf, size_t buf_size) {
    FILE *fp = popen(cmd, "r");
    if (!fp) return -1;
    size_t total = 0;
    while (total + 1 < buf_size) {
        size_t n = fread(buf + total, 1, buf_size - 1 - total, fp);
        if (n == 0) break;
        total += n;
    }
    buf[total] = '\0';
    pclose(fp);
    return 0;
}

static int is_mac_char(char c) {
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || c == ':';
}

/* Walks the `bluetoothctl devices Connected` output. Each line looks like:
     "Device AA:BB:CC:DD:EE:FF Device Name Here"
   Calls visit(mac) for each. visit returns non-zero to stop iteration. */
typedef int (*device_visit_fn)(const char *mac, void *ctx);

static void for_each_connected(device_visit_fn visit, void *ctx) {
    char out[4096];
    /* --timeout bounds the shell so a wedged bluetoothd can't stall the
       single-threaded daemon dispatch on every play/record start. */
    if (run_and_capture("bluetoothctl --timeout 2 devices Connected 2>/dev/null",
                        out, sizeof(out)) != 0) {
        return;
    }
    char *line = out;
    while (*line) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';

        char mac[18] = {0};
        const char *p = strstr(line, "Device ");
        if (p) {
            p += strlen("Device ");
            int j = 0;
            while (*p && j < 17 && is_mac_char(*p)) {
                mac[j++] = *p++;
            }
            mac[j] = '\0';
            if (j == 17) {
                if (visit(mac, ctx)) {
                    return;
                }
            }
        }

        if (!nl) break;
        line = nl + 1;
    }
}

struct find_ctx {
    const char *uuid_label_a;
    const char *uuid_label_b;  /* optional second match */
    char       *out_mac;
    size_t      out_size;
    int         found;
};

static int find_visit(const char *mac, void *vctx) {
    struct find_ctx *ctx = (struct find_ctx *)vctx;

    char cmd[64];
    snprintf(cmd, sizeof(cmd), "bluetoothctl --timeout 2 info %s 2>/dev/null", mac);

    char info[8192];
    if (run_and_capture(cmd, info, sizeof(info)) != 0) return 0;

    /* bluetoothctl info prints "UUID: Audio Sink ..." lines. */
    int hit = (strstr(info, ctx->uuid_label_a) != NULL) ||
              (ctx->uuid_label_b && strstr(info, ctx->uuid_label_b) != NULL);
    if (hit) {
        snprintf(ctx->out_mac, ctx->out_size, "%s", mac);
        ctx->found = 1;
        return 1; /* stop iteration */
    }
    return 0;
}

int bt_route_find_a2dp_sink(char *out_mac, size_t mac_size) {
    if (!out_mac || mac_size < 18) return 0;
    out_mac[0] = '\0';

    struct find_ctx ctx = {
        .uuid_label_a = UUID_LABEL_A2DP_SINK,
        .uuid_label_b = NULL,
        .out_mac      = out_mac,
        .out_size     = mac_size,
        .found        = 0,
    };
    for_each_connected(find_visit, &ctx);
    return ctx.found;
}

int bt_route_find_hfp_mic(char *out_mac, size_t mac_size) {
    if (!out_mac || mac_size < 18) return 0;
    out_mac[0] = '\0';

    struct find_ctx ctx = {
        .uuid_label_a = UUID_LABEL_HFP_HF,
        .uuid_label_b = UUID_LABEL_HFP_HS,
        .out_mac      = out_mac,
        .out_size     = mac_size,
        .found        = 0,
    };
    for_each_connected(find_visit, &ctx);
    return ctx.found;
}

#include <stdlib.h>
#include <unistd.h>
#ifndef BH_ASOUND_CONF
#define BH_ASOUND_CONF "/etc/asound.conf"
#endif
static int commit_route(FILE *f, const char *temp) {
    int ok = !ferror(f);
    if (fflush(f) != 0) ok = 0;
    if (ok && fsync(fileno(f)) != 0) ok = 0;
    if (fclose(f) != 0) ok = 0;
    if (!ok || rename(temp, BH_ASOUND_CONF) != 0) { unlink(temp); return -1; }
    return 0;
}
/* Read the physical card number that rcS chose at boot */
static int read_boot_card(void)
{
    int card = 0;
    FILE *cf = fopen("/tmp/bh-audio-card", "r");
    if (cf) { (void)fscanf(cf, "%d", &card); fclose(cf); }
    return card;
}

/* Route ALSA default PCM through BlueALSA for the connected headset.
 * BlueALSA creates a virtual PCM (type bluealsa) once the A2DP codec
 * is negotiated.  Setting asound.conf here routes all ALSA playback
 * through the headset and maps the default ctl to AVRCP volume. */
static int set_alsa_bt_sink(const char *mac)
{
    char temp[128] = BH_ASOUND_CONF ".tmpXXXXXX";
    int fd = mkstemp(temp);
    if (fd < 0) return -1;
    FILE *f = fdopen(fd, "w");
    if (!f) { close(fd); unlink(temp); return -1; }
    /* Named pcm.bt first, then alias default to it via plug for
     * automatic format/rate conversion.  Inline slave.pcm {} blocks
     * cause parse errors on older ALSA library versions. */
    /* asym: A2DP is a playback-only sink, so capture MUST NOT be routed to it
     * or every record open fails with -EINVAL while a headset is connected.
     * Playback -> headset, capture -> the physical card (a USB headset's mic).
     * A BT headset's own mic is HFP/SCO and is opened explicitly by the audio
     * daemon, so it does not depend on this default. */
    int card = read_boot_card();
    fprintf(f,
        "pcm.bt {\n"
        "    type bluealsa\n"
        "    device \"%s\"\n"
        "    profile \"a2dp\"\n"
        "}\n"
        "pcm.bh_play {\n"
        "    type plug\n"
        "    slave.pcm \"bt\"\n"
        "}\n"
        "pcm.bh_cap {\n"
        "    type plug\n"
        "    slave.pcm \"hw:%d,0\"\n"
        "}\n"
        "pcm.!default {\n"
        "    type asym\n"
        "    playback.pcm \"bh_play\"\n"
        "    capture.pcm \"bh_cap\"\n"
        "}\n"
        "ctl.!default {\n"
        "    type bluealsa\n"
        "    device \"%s\"\n"
        "}\n",
        mac, card, mac);
    return commit_route(f, temp);
}

/* Restore ALSA to the physical card chosen at boot */
static int restore_alsa_default(void)
{
    int card = read_boot_card();
    char temp[128] = BH_ASOUND_CONF ".tmpXXXXXX";
    int fd = mkstemp(temp);
    if (fd < 0) return -1;
    FILE *f = fdopen(fd, "w");
    if (!f) { close(fd); unlink(temp); return -1; }
    /* Mirror the boot-time rcS config exactly (see rcS's write_asound_conf):
     * no dmix — verified on hardware that dmix HANGS on this card (hw_ptr
     * frozen at 0); the card itself mixes concurrent hardware playback
     * streams fine (8 substreams), so bh_play goes straight at hw:$card,0. */
    fprintf(f,
        "pcm.bh_play {\n"
        "    type plug\n"
        "    slave.pcm \"hw:%d,0\"\n"
        "}\n"
        "pcm.bh_cap {\n"
        "    type plug\n"
        "    slave.pcm \"hw:%d,0\"\n"
        "}\n"
        "pcm.!default {\n"
        "    type asym\n"
        "    playback.pcm \"bh_play\"\n"
        "    capture.pcm \"bh_cap\"\n"
        "}\n"
        "ctl.!default {\n"
        "    type hw\n"
        "    card %d\n"
        "}\n",
        card, card, card);
    return commit_route(f, temp);
}


int bt_route_set_default(const char *mac) {
    if (!mac || !mac[0]) return restore_alsa_default();
    if (strlen(mac) != 17) return -1;
    for (int i = 0; i < 17; i++) {
        if (i % 3 == 2 ? mac[i] != ':' : !isxdigit((unsigned char)mac[i])) return -1;
    }
    char cmd[128], info[8192];
    snprintf(cmd, sizeof(cmd), "bluetoothctl --timeout 3 info %s 2>/dev/null", mac);
    if (run_and_capture(cmd, info, sizeof(info)) != 0 || !strstr(info, "Connected: yes") ||
        !strstr(info, "Audio Sink")) return -1;
    return set_alsa_bt_sink(mac);
}
