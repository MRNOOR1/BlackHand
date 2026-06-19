#include "headphone_input.h"

#include "../services/mp3_service.h"
#include "../services/voice_memo_service.h"
#include "../ui-ipcs/audio_ipc.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <unistd.h>

#ifndef KEY_MEDIA
#define KEY_MEDIA 226
#endif

#define INPUT_DIR    "/dev/input"
#define VOLUME_STEP  5       /* % per button press              */
#define RETRY_MS     2000    /* ms between device re-scan tries */

static pthread_t     g_thread;
static int           g_thread_started = 0;
static volatile int  g_stop  = 0;
static int           g_vol   = 80; /* shadow volume for +/- */

/* ── helpers ──────────────────────────────────────────────────────────── */

static int has_bit(const uint8_t *bits, int bit) {
    return (bits[bit / 8] >> (bit % 8)) & 1;
}

/* Sync our shadow volume from the audio service. */
static void sync_volume(void) {
    int playing = 0, vol = 0;
    if (audio_ipc_status(&playing, &vol) == 0 && vol >= 0 && vol <= 100)
        g_vol = vol;
}

static void set_volume(int v) {
    if (v < 0)   v = 0;
    if (v > 100) v = 100;
    g_vol = v;
    audio_ipc_volume(v);
}

/* ── device discovery ─────────────────────────────────────────────────── */

/* Return an open (blocking) fd for the first input device that has
   KEY_VOLUMEUP or KEY_PLAYPAUSE, or -1 if none found. */
static int find_headphone_device(void) {
    DIR *d = opendir(INPUT_DIR);
    if (!d) return -1;

    struct dirent *ent;
    int found_fd = -1;

    while (!g_stop && (ent = readdir(d)) != NULL) {
        if (strncmp(ent->d_name, "event", 5) != 0) continue;

        char path[64];
        snprintf(path, sizeof(path), "%s/%s", INPUT_DIR, ent->d_name);

        /* Open non-blocking just for capability probing */
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;

        uint8_t ev_bits[EV_MAX / 8 + 1]   = {0};
        uint8_t key_bits[KEY_MAX / 8 + 1] = {0};

        if (ioctl(fd, EVIOCGBIT(0, sizeof(ev_bits)), ev_bits) < 0
            || !has_bit(ev_bits, EV_KEY)) {
            close(fd);
            continue;
        }

        if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits) < 0) {
            close(fd);
            continue;
        }

        int want = has_bit(key_bits, KEY_VOLUMEUP)  ||
                   has_bit(key_bits, KEY_VOLUMEDOWN) ||
                   has_bit(key_bits, KEY_PLAYPAUSE)  ||
                   has_bit(key_bits, KEY_MEDIA);

        close(fd); /* close probe fd */

        if (want) {
            /* Re-open in blocking mode for the read loop */
            fd = open(path, O_RDONLY);
            if (fd >= 0) {
                char name[128] = "unknown";
                ioctl(fd, EVIOCGNAME(sizeof(name)), name);
                found_fd = fd;
                break;
            }
        }
    }

    closedir(d);
    return found_fd;
}

/* ── key dispatch ─────────────────────────────────────────────────────── */

static void dispatch(int code) {
    switch (code) {

    case KEY_VOLUMEUP:
        /* Always fetch the live volume before applying a delta.
         * This keeps headphone buttons, the UI keypad, and future BT
         * controls all in sync — blackhand-audio is the single owner. */
        sync_volume();
        set_volume(g_vol + VOLUME_STEP);
        break;

    case KEY_VOLUMEDOWN:
        sync_volume();
        set_volume(g_vol - VOLUME_STEP);
        break;

    case KEY_PLAYPAUSE:
    case KEY_MEDIA: {
        /* Voice memo takes priority if it's active */
        VMState vm = voice_memo_service_state();
        if (vm == VM_PLAYING) {
            voice_memo_service_play_pause();
            break;
        }
        if (vm == VM_PAUSED) {
            voice_memo_service_play_resume();
            break;
        }

        /* Otherwise control mp3 playback */
        mp3_playback_state ms = mp3_service_get_state();
        if (ms == MP3_PLAYING)      mp3_service_pause();
        else if (ms == MP3_PAUSED)  mp3_service_resume();
        break;
    }

    case KEY_NEXTSONG:
        mp3_service_next();
        break;

    case KEY_PREVIOUSSONG:
        mp3_service_prev();
        break;

    default:
        break;
    }
}

/* ── event-loop thread ────────────────────────────────────────────────── */

static void *thread_fn(void *arg) {
    (void)arg;

    int fd = -1;
    sync_volume();

    while (!g_stop) {
        /* (Re-)discover device when we don't have one */
        if (fd < 0) {
            fd = find_headphone_device();
            if (fd < 0) {
                /* Wait RETRY_MS in small slices so g_stop is noticed quickly */
                for (int i = 0; i < RETRY_MS / 100 && !g_stop; i++)
                    usleep(100000);
                continue;
            }
            sync_volume(); /* resync after plug-in */
        }

        /* Block up to 500ms waiting for an event */
        struct timeval tv = {0, 500000};
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);

        int sel = select(fd + 1, &rfds, NULL, NULL, &tv);
        if (sel < 0) {
            if (errno == EINTR) continue;
            fprintf(stderr, "headphone_input: select error — rescanning\n");
            close(fd); fd = -1;
            continue;
        }
        if (sel == 0) continue; /* timeout — check g_stop and loop */

        struct input_event ev;
        ssize_t n = read(fd, &ev, sizeof(ev));
        if (n != (ssize_t)sizeof(ev)) {
            /* Device disconnected or read error */
            fprintf(stderr, "headphone_input: device lost — rescanning\n");
            close(fd); fd = -1;
            continue;
        }

        /* Only handle key-down events (value == 1).
           value == 0 is key-up; value == 2 is auto-repeat. */
        if (ev.type == EV_KEY && ev.value == 1)
            dispatch(ev.code);
    }

    if (fd >= 0) close(fd);
    return NULL;
}

/* ── public API ───────────────────────────────────────────────────────── */

void headphone_input_init(void) {
    g_stop = 0;
    g_thread_started = 0;
    if (pthread_create(&g_thread, NULL, thread_fn, NULL) == 0)
        g_thread_started = 1;
    else
        fprintf(stderr, "headphone_input: failed to start thread\n");
}

void headphone_input_shutdown(void) {
    g_stop = 1;
    if (g_thread_started) {
        pthread_join(g_thread, NULL);
        g_thread_started = 0;
    }
}
