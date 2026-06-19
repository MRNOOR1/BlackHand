/*
 * audio_alsa.c — single-owner audio engine for blackhand-audio.
 *
 * Owns: the playback PCM, the capture PCM, the playback thread, the
 * capture thread, the elapsed clock, all routing decisions. The UI
 * never sees ALSA, never sees a PID, never tracks position itself.
 *
 * Engines are symmetric state machines:
 *
 *   PB_IDLE ──play_start──▶ PB_PLAYING ──pause──▶ PB_PAUSED
 *      ▲                        │ stop                │
 *      └────────────────────────┴──────── resume ─────┘
 *
 *   RC_IDLE ──record_start──▶ RC_RECORDING ──pause──▶ RC_PAUSED
 *      ▲                          │ stop                 │
 *      └──────────────────────────┴──────── resume ──────┘
 *
 * Pause is a condvar wait inside the I/O thread — no PCM close, no
 * file close, no restart-from-zero. Resume re-prepares the PCM and
 * keeps going from the current byte offset.
 */

#include "audio_alsa.h"
#include "bt_route.h"

#include <alsa/asoundlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <stdint.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdarg.h>

/* ───────────── debug log ─────────────
   Writes timestamped lines to /boot/logs/audio.log. /boot is FAT32 and
   auto-mounts on macOS / Windows when the SD card is plugged in — no
   ext4 driver needed. Each line is flushed immediately so a hard
   power-off doesn't eat the last few events.

   Also mirrored to stderr, which the service manager redirects into
   /boot/logs/last-boot.log via the rcS exec redirect. */
static FILE *g_log = NULL;
static pthread_mutex_t g_log_lock = PTHREAD_MUTEX_INITIALIZER;

static void dlog(const char *fmt, ...) {
    pthread_mutex_lock(&g_log_lock);
    if (!g_log) {
        /* Make sure /boot/logs exists even if rcS didn't create it
           (e.g. someone ran the daemon manually for testing). */
        mkdir("/boot/logs", 0755);
        g_log = fopen("/boot/logs/audio.log", "a");
    }
    if (g_log) {
        time_t now = time(NULL);
        struct tm tmv;
        localtime_r(&now, &tmv);
        char ts[32];
        strftime(ts, sizeof(ts), "%H:%M:%S", &tmv);
        fprintf(g_log, "[%s] ", ts);
        va_list ap; va_start(ap, fmt);
        vfprintf(g_log, fmt, ap);
        va_end(ap);
        fputc('\n', g_log);
        fflush(g_log);
    }
    pthread_mutex_unlock(&g_log_lock);

    va_list ap2; va_start(ap2, fmt);
    vfprintf(stderr, fmt, ap2);
    va_end(ap2);
    fputc('\n', stderr);
}

/* ───────────────────────── card discovery ───────────────────────── */

static int find_usb_audio_card(void) {
    for (int n = 1; n <= 7; n++) {
        char path[64];
        snprintf(path, sizeof(path), "/proc/asound/card%d/usbbus", n);
        if (access(path, F_OK) == 0) return n;
    }
    return 1;
}

static int load_card_num(void) {
    FILE *f = fopen("/tmp/bh-audio-card", "r");
    if (f) {
        int n = 1;
        if (fscanf(f, "%d", &n) == 1 && n >= 1 && n <= 7) {
            fclose(f);
            return n;
        }
        fclose(f);
    }
    return find_usb_audio_card();
}

static void card_to_pcm(int card_num, char *buf, size_t sz) {
    snprintf(buf, sz, "plughw:%d,0", card_num);
}

/* ───────────────────────── mixer / volume ───────────────────────── */

static snd_mixer_t      *mixer_handle = NULL;
static snd_mixer_elem_t *master_elem  = NULL;
static int current_volume = 50;

#define VOLUME_FLOOR_MB (-3000)

static void apply_volume(void) {
    if (!master_elem) return;
    long min_val, max_val;

    if (snd_mixer_selem_get_playback_dB_range(master_elem, &min_val, &max_val) == 0
        && max_val > min_val) {
        long db_value;
        if (current_volume == 0) {
            db_value = min_val;
        } else {
            long floor = (min_val > VOLUME_FLOOR_MB) ? min_val : VOLUME_FLOOR_MB;
            db_value = floor + (long)current_volume * (max_val - floor) / 100;
        }
        snd_mixer_selem_set_playback_dB_all(master_elem, db_value, 0);
        return;
    }
    if (snd_mixer_selem_get_playback_volume_range(master_elem, &min_val, &max_val) == 0
        && max_val > min_val) {
        long raw = min_val + (long)current_volume * (max_val - min_val) / 100;
        snd_mixer_selem_set_playback_volume_all(master_elem, raw);
    }
}

static int mixer_try_attach(void) {
    if (master_elem) return 0;
    if (mixer_handle) {
        snd_mixer_close(mixer_handle);
        mixer_handle = NULL;
    }

    int card_num = load_card_num();
    char mixer_card[32];
    snprintf(mixer_card, sizeof(mixer_card), "hw:%d", card_num);

    int err = snd_mixer_open(&mixer_handle, 0);
    if (err < 0) { mixer_handle = NULL; return -1; }

    err = snd_mixer_attach(mixer_handle, mixer_card);
    if (err < 0) {
        snd_mixer_close(mixer_handle);
        mixer_handle = NULL;
        return -1;
    }

    snd_mixer_selem_register(mixer_handle, NULL, NULL);
    snd_mixer_load(mixer_handle);

    static const char * const PREFERRED[] = {
        "Master", "Speaker", "Headphone", "PCM", "Front", NULL
    };

    snd_mixer_elem_t *elem = snd_mixer_first_elem(mixer_handle);
    while (elem) {
        const char *name = snd_mixer_selem_get_name(elem);
        int has_vol = snd_mixer_selem_has_playback_volume(elem);
        fprintf(stderr, "audio: mixer elem '%s' playback_vol=%d\n", name, has_vol);

        if (has_vol) {
            if (!master_elem) master_elem = elem;
            for (int i = 0; PREFERRED[i]; i++) {
                if (strcmp(name, PREFERRED[i]) == 0) {
                    master_elem = elem;
                    if (i == 0) goto found;
                }
            }
        }
        elem = snd_mixer_elem_next(elem);
    }

found:
    if (master_elem) {
        fprintf(stderr, "audio: using mixer control '%s' on %s\n",
                snd_mixer_selem_get_name(master_elem), mixer_card);
        apply_volume();
        return 0;
    }
    fprintf(stderr, "audio: no volume control on %s\n", mixer_card);
    return -1;
}

void audio_set_volume(int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    current_volume = percent;
    mixer_try_attach();
    if (master_elem) apply_volume();
}

int audio_get_volume(void) {
    if (!master_elem) return current_volume;
    snd_mixer_handle_events(mixer_handle);

    long min_val, max_val;
    if (snd_mixer_selem_get_playback_dB_range(master_elem, &min_val, &max_val) == 0
        && max_val > min_val) {
        long cur_db;
        if (snd_mixer_selem_get_playback_dB(master_elem, SND_MIXER_SCHN_MONO, &cur_db) < 0)
            return current_volume;
        long floor = (min_val > VOLUME_FLOOR_MB) ? min_val : VOLUME_FLOOR_MB;
        if (cur_db <= floor)       current_volume = 0;
        else if (cur_db >= max_val) current_volume = 100;
        else current_volume = (int)((cur_db - floor) * 100 / (max_val - floor));
        return current_volume;
    }
    if (snd_mixer_selem_get_playback_volume_range(master_elem, &min_val, &max_val) == 0
        && max_val > min_val) {
        long cur_raw;
        if (snd_mixer_selem_get_playback_volume(master_elem, SND_MIXER_SCHN_MONO, &cur_raw) < 0)
            return current_volume;
        current_volume = (int)((cur_raw - min_val) * 100 / (max_val - min_val));
    }
    return current_volume;
}

/* ───────────────────────── monotonic clock ───────────────────────── */

static int64_t mono_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

/* ───────────────────────── playback engine ───────────────────────── */

typedef enum { PB_IDLE, PB_PLAYING, PB_PAUSED } pb_state_t;

static pthread_mutex_t pb_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  pb_resume_cv = PTHREAD_COND_INITIALIZER;
static pb_state_t      pb_state = PB_IDLE;

static pthread_t       pb_thread_id = 0;
static int             pb_stop_request = 0;

static snd_pcm_t      *pb_pcm = NULL;
static FILE           *pb_file = NULL;
static off_t           pb_data_start = 0;
static uint32_t        pb_data_size  = 0;
static uint32_t        pb_byte_rate  = 1;

/* Elapsed clock: position_ms is the authoritative position; it gets
   bumped by (now - play_start_mono_ms) whenever we read or transition. */
static int64_t         pb_position_ms = 0;
static int64_t         pb_play_start_mono_ms = 0;
static int             pb_seek_request_ms = -1;

/* Compute the live position. Caller must hold pb_lock. */
static int64_t pb_live_position_ms_locked(void) {
    if (pb_state == PB_PLAYING) {
        return pb_position_ms + (mono_ms() - pb_play_start_mono_ms);
    }
    return pb_position_ms;
}

/* Open the right PCM device for playback. Routing decision lives here.
   Tries BT A2DP first if a sink is connected; falls back to USB if the
   BT open fails (stale bluealsa registration, headset just dropped, …).
   Returns 0 on success. */
static int pb_open_pcm(void) {
    char dev[64];
    char mac[24] = {0};

    if (bt_route_find_a2dp_sink(mac, sizeof(mac))) {
        snprintf(dev, sizeof(dev), "bluealsa:DEV=%s,PROFILE=a2dp", mac);
        dlog("play routing → BT A2DP (%s)", mac);
        int err = snd_pcm_open(&pb_pcm, dev, SND_PCM_STREAM_PLAYBACK, 0);
        if (err == 0) return 0;
        dlog("play: bluealsa open failed (%s) — falling back to USB", snd_strerror(err));
        pb_pcm = NULL;
    }

    card_to_pcm(load_card_num(), dev, sizeof(dev));
    dlog("play routing → USB (%s)", dev);
    int err = snd_pcm_open(&pb_pcm, dev, SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) {
        dlog("play: pcm_open(%s): %s", dev, snd_strerror(err));
        pb_pcm = NULL;
        return -1;
    }
    return 0;
}

/* Parse the WAV header of pb_file. Fills in pb_data_start, pb_data_size,
   pb_byte_rate, and out_channels/out_sample_rate/out_bits. Returns 0 on
   success. On return, pb_file is positioned at the start of audio data. */
static int pb_parse_wav(uint16_t *out_channels, uint32_t *out_sample_rate,
                        uint16_t *out_bits) {
    uint8_t riff[12];
    if (fread(riff, 1, 12, pb_file) < 12) return -1;
    if (memcmp(riff, "RIFF", 4) != 0 || memcmp(riff + 8, "WAVE", 4) != 0)
        return -1;

    uint16_t channels = 1, bits = 16;
    uint32_t sample_rate = 48000;
    int found_fmt = 0, found_data = 0;

    while (!found_data) {
        uint8_t id[4], sz_buf[4];
        if (fread(id, 1, 4, pb_file) < 4 || fread(sz_buf, 1, 4, pb_file) < 4) break;
        uint32_t sz = (uint32_t)sz_buf[0] | ((uint32_t)sz_buf[1] << 8) |
                      ((uint32_t)sz_buf[2] << 16) | ((uint32_t)sz_buf[3] << 24);

        if (memcmp(id, "fmt ", 4) == 0 && sz >= 16) {
            uint8_t fb[16];
            if (fread(fb, 1, 16, pb_file) < 16) return -1;
            uint16_t afmt = (uint16_t)(fb[0] | (fb[1] << 8));
            if (afmt != 1) return -1; /* PCM only */
            channels    = (uint16_t)(fb[2]  | (fb[3]  << 8));
            sample_rate = (uint32_t)(fb[4]  | (fb[5]  << 8) | (fb[6] << 16) | (fb[7] << 24));
            bits        = (uint16_t)(fb[14] | (fb[15] << 8));
            if (sz > 16) fseek(pb_file, (long)(sz - 16), SEEK_CUR);
            found_fmt = 1;
        } else if (memcmp(id, "data", 4) == 0) {
            pb_data_start = ftello(pb_file);
            pb_data_size  = sz;
            found_data = 1;
        } else {
            fseek(pb_file, (long)sz, SEEK_CUR);
        }
    }
    if (!found_fmt || !found_data) return -1;

    pb_byte_rate = sample_rate * channels * (bits / 8);
    if (pb_byte_rate == 0) return -1;
    *out_channels = channels;
    *out_sample_rate = sample_rate;
    *out_bits = bits;
    return 0;
}

static void *pb_thread_main(void *arg) {
    (void)arg;

    uint16_t channels = 1, bits = 16;
    uint32_t sample_rate = 48000;
    uint8_t *buf = NULL;
    size_t   frame_bytes = 0;
    const size_t period_frames = 1024;

    pthread_mutex_lock(&pb_lock);
    /* pb_file and pb_pcm were opened by play_start under lock before the
       thread was created — they're guaranteed to be valid here. */
    pthread_mutex_unlock(&pb_lock);

    if (pb_parse_wav(&channels, &sample_rate, &bits) != 0) {
        dlog("play: bad WAV header");
        goto cleanup;
    }
    dlog("play: WAV %uHz %uch %ubit, data_size=%u",
         sample_rate, (unsigned)channels, (unsigned)bits, pb_data_size);

    snd_pcm_format_t fmt = (bits == 8)  ? SND_PCM_FORMAT_U8 :
                           (bits == 24) ? SND_PCM_FORMAT_S24_3LE :
                                          SND_PCM_FORMAT_S16_LE;

    int err = snd_pcm_set_params(pb_pcm, fmt, SND_PCM_ACCESS_RW_INTERLEAVED,
                                  channels, sample_rate, 1, 500000);
    if (err < 0) {
        dlog("play: set_params(%uHz %uch): %s",
             sample_rate, (unsigned)channels, snd_strerror(err));
        goto cleanup;
    }

    frame_bytes = (size_t)(channels * (bits / 8));
    if (frame_bytes == 0) { dlog("play: zero frame_bytes"); goto cleanup; }
    buf = malloc(period_frames * frame_bytes);
    if (!buf) goto cleanup;

    while (1) {
        pthread_mutex_lock(&pb_lock);

        /* Block here while paused. */
        while (pb_state == PB_PAUSED && !pb_stop_request) {
            pthread_cond_wait(&pb_resume_cv, &pb_lock);
        }
        if (pb_stop_request) {
            pthread_mutex_unlock(&pb_lock);
            break;
        }

        int seek_ms = pb_seek_request_ms;
        pb_seek_request_ms = -1;

        pthread_mutex_unlock(&pb_lock);

        if (seek_ms >= 0) {
            off_t target = pb_data_start + ((off_t)seek_ms * pb_byte_rate / 1000);
            off_t end    = pb_data_start + (off_t)pb_data_size;
            if (target < pb_data_start) target = pb_data_start;
            if (target > end)           target = end;
            /* Frame-align to avoid garbled output on 16/24/32-bit formats. */
            off_t off = target - pb_data_start;
            off -= (off % (off_t)frame_bytes);
            target = pb_data_start + off;

            fseeko(pb_file, target, SEEK_SET);
            snd_pcm_drop(pb_pcm);
            snd_pcm_prepare(pb_pcm);
        }

        size_t got = fread(buf, frame_bytes, period_frames, pb_file);
        if (got == 0) { dlog("play: EOF reached"); break; }
        snd_pcm_sframes_t w = snd_pcm_writei(pb_pcm, buf, (snd_pcm_uframes_t)got);
        if (w < 0) {
            w = snd_pcm_recover(pb_pcm, (int)w, 0);
            if (w < 0) {
                dlog("play: writei: %s", snd_strerror((int)w));
                break;
            }
        }
    }

    /* Let buffered audio finish unless we were explicitly stopped. */
    pthread_mutex_lock(&pb_lock);
    int stopped = pb_stop_request;
    pthread_mutex_unlock(&pb_lock);
    if (!stopped && pb_pcm) snd_pcm_drain(pb_pcm);

cleanup:
    free(buf);

    pthread_mutex_lock(&pb_lock);
    if (pb_state == PB_PLAYING) {
        /* Natural end-of-track: freeze position at the file's full length. */
        pb_position_ms += (mono_ms() - pb_play_start_mono_ms);
    }
    if (pb_pcm)  { snd_pcm_drop(pb_pcm); snd_pcm_close(pb_pcm); pb_pcm = NULL; }
    if (pb_file) { fclose(pb_file); pb_file = NULL; }
    pb_state = PB_IDLE;
    pb_stop_request = 0;
    pb_play_start_mono_ms = 0;
    pthread_mutex_unlock(&pb_lock);
    return NULL;
}

/* Stop helper that joins the playback thread. Must be called WITHOUT
   pb_lock held — the thread acquires it during cleanup. The snd_pcm_drop
   call has to happen under the lock so it can't race with the thread's
   final snd_pcm_close (which sets pb_pcm = NULL). */
static void pb_join_thread(void) {
    pthread_mutex_lock(&pb_lock);
    pb_stop_request = 1;
    pthread_cond_broadcast(&pb_resume_cv); /* wake up if paused */
    if (pb_pcm) snd_pcm_drop(pb_pcm);      /* unblock any writei in flight */
    pthread_t t = pb_thread_id;
    pb_thread_id = 0;
    pthread_mutex_unlock(&pb_lock);

    if (t != 0) pthread_join(t, NULL);
}

int audio_play_start(const char *path) {
    if (!path) return -1;

    /* Stop any existing playback first. Releases pb_pcm and pb_file. */
    pthread_mutex_lock(&pb_lock);
    if (pb_state != PB_IDLE) {
        pthread_mutex_unlock(&pb_lock);
        pb_join_thread();
        pthread_mutex_lock(&pb_lock);
    }

    dlog("play_start: %s", path);
    pb_file = fopen(path, "rb");
    if (!pb_file) {
        dlog("play_start: cannot open %s: %s", path, strerror(errno));
        pthread_mutex_unlock(&pb_lock);
        return -1;
    }
    if (pb_open_pcm() != 0) {
        fclose(pb_file); pb_file = NULL;
        pthread_mutex_unlock(&pb_lock);
        return -1;
    }

    mixer_try_attach();

    pb_position_ms = 0;
    pb_play_start_mono_ms = mono_ms();
    pb_seek_request_ms = -1;
    pb_stop_request = 0;
    pb_state = PB_PLAYING;
    pthread_mutex_unlock(&pb_lock);

    if (pthread_create(&pb_thread_id, NULL, pb_thread_main, NULL) != 0) {
        pthread_mutex_lock(&pb_lock);
        if (pb_pcm)  { snd_pcm_close(pb_pcm); pb_pcm = NULL; }
        if (pb_file) { fclose(pb_file); pb_file = NULL; }
        pb_state = PB_IDLE;
        pthread_mutex_unlock(&pb_lock);
        return -1;
    }
    return 0;
}

int audio_play_pause(void) {
    pthread_mutex_lock(&pb_lock);
    if (pb_state != PB_PLAYING) {
        pthread_mutex_unlock(&pb_lock);
        return -1;
    }
    pb_position_ms += (mono_ms() - pb_play_start_mono_ms);
    pb_state = PB_PAUSED;
    /* Drop under the lock — the thread's cleanup also holds this lock
       when it closes pb_pcm, so we can't race with that. The drop kills
       any in-flight writei so audio stops immediately rather than after
       the kernel buffer drains. */
    if (pb_pcm) snd_pcm_drop(pb_pcm);
    pthread_mutex_unlock(&pb_lock);
    return 0;
}

int audio_play_resume(void) {
    pthread_mutex_lock(&pb_lock);
    if (pb_state != PB_PAUSED) {
        pthread_mutex_unlock(&pb_lock);
        return -1;
    }
    if (pb_pcm) snd_pcm_prepare(pb_pcm);
    pb_play_start_mono_ms = mono_ms();
    pb_state = PB_PLAYING;
    pthread_cond_broadcast(&pb_resume_cv);
    pthread_mutex_unlock(&pb_lock);
    return 0;
}

int audio_play_stop(void) {
    pthread_mutex_lock(&pb_lock);
    if (pb_state == PB_IDLE) {
        pthread_mutex_unlock(&pb_lock);
        return 0;
    }
    pthread_mutex_unlock(&pb_lock);
    pb_join_thread();
    return 0;
}

int audio_play_seek(int position_ms) {
    if (position_ms < 0) position_ms = 0;
    pthread_mutex_lock(&pb_lock);
    if (pb_state == PB_IDLE) {
        pthread_mutex_unlock(&pb_lock);
        return -1;
    }
    pb_seek_request_ms = position_ms;
    pb_position_ms = position_ms;
    if (pb_state == PB_PLAYING) {
        pb_play_start_mono_ms = mono_ms();
    }
    /* If paused, wake the thread so it picks up the seek immediately —
       no, leave it parked, the seek will apply on resume. */
    pthread_mutex_unlock(&pb_lock);
    return 0;
}

int audio_is_playing(void) {
    pthread_mutex_lock(&pb_lock);
    int r = (pb_state == PB_PLAYING);
    pthread_mutex_unlock(&pb_lock);
    return r;
}

int audio_is_paused(void) {
    pthread_mutex_lock(&pb_lock);
    int r = (pb_state == PB_PAUSED);
    pthread_mutex_unlock(&pb_lock);
    return r;
}

int audio_play_elapsed_ms(void) {
    pthread_mutex_lock(&pb_lock);
    int64_t e = pb_live_position_ms_locked();
    pthread_mutex_unlock(&pb_lock);
    if (e < 0) e = 0;
    return (int)e;
}

/* ───────────────────────── recording engine ───────────────────────── */

#define REC_CHANNELS_DEFAULT  1
#define REC_FORMAT            SND_PCM_FORMAT_S16_LE
#define REC_BITS              16
#define REC_PERIOD            1024

typedef enum { RC_IDLE, RC_RECORDING, RC_PAUSED } rc_state_t;

static pthread_mutex_t rc_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  rc_resume_cv = PTHREAD_COND_INITIALIZER;
static rc_state_t      rc_state = RC_IDLE;

static pthread_t       rc_thread_id = 0;
static int             rc_stop_request = 0;

static snd_pcm_t      *rc_pcm = NULL;
static FILE           *rc_file = NULL;
static uint32_t        rc_data_bytes = 0;
static uint32_t        rc_sample_rate = 48000;
static uint16_t        rc_channels = REC_CHANNELS_DEFAULT;

/* Elapsed clock — same model as playback. */
static int64_t         rc_position_ms = 0;
static int64_t         rc_record_start_mono_ms = 0;

static void rc_write_wav_header(FILE *f, uint32_t data_size,
                                uint16_t channels, uint32_t sample_rate) {
    uint16_t audio_fmt   = 1;
    uint16_t bits        = REC_BITS;
    uint32_t byte_rate   = sample_rate * channels * (bits / 8);
    uint16_t block_align = (uint16_t)(channels * (bits / 8));
    uint32_t fmt_size    = 16;
    uint32_t chunk_size  = 36 + data_size;

    fseek(f, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, f);   fwrite(&chunk_size,  4, 1, f);
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);   fwrite(&fmt_size,    4, 1, f);
    fwrite(&audio_fmt,   2, 1, f);
    fwrite(&channels,    2, 1, f);
    fwrite(&sample_rate, 4, 1, f);
    fwrite(&byte_rate,   4, 1, f);
    fwrite(&block_align, 2, 1, f);
    fwrite(&bits,        2, 1, f);
    fwrite("data", 1, 4, f);   fwrite(&data_size,   4, 1, f);
}

/* Open the right capture PCM device. Tries BT SCO first; falls back to
   USB capture if that fails. Returns 0 on success, fills *sample_rate_out
   with the rate we agreed with the device. */
static int rc_open_pcm(uint32_t *sample_rate_out, uint16_t *channels_out) {
    char dev[64];
    char mac[24] = {0};
    uint16_t want_channels = REC_CHANNELS_DEFAULT;
    uint32_t want_rate;

    if (bt_route_find_hfp_mic(mac, sizeof(mac))) {
        snprintf(dev, sizeof(dev), "bluealsa:DEV=%s,PROFILE=sco", mac);
        want_rate = 16000; /* HFP SCO mSBC max */
        dlog("rec routing → BT SCO (%s)", mac);
        int err = snd_pcm_open(&rc_pcm, dev, SND_PCM_STREAM_CAPTURE, 0);
        if (err == 0) {
            err = snd_pcm_set_params(rc_pcm, REC_FORMAT, SND_PCM_ACCESS_RW_INTERLEAVED,
                                      want_channels, want_rate, 1, 500000);
            if (err == 0) {
                *sample_rate_out = want_rate;
                *channels_out    = want_channels;
                return 0;
            }
            dlog("rec: SCO set_params failed (%s) — falling back to USB", snd_strerror(err));
            snd_pcm_close(rc_pcm);
        } else {
            dlog("rec: SCO open failed (%s) — falling back to USB", snd_strerror(err));
        }
        rc_pcm = NULL;
    }

    card_to_pcm(load_card_num(), dev, sizeof(dev));
    want_rate = 48000;
    dlog("rec routing → USB (%s)", dev);
    int err = snd_pcm_open(&rc_pcm, dev, SND_PCM_STREAM_CAPTURE, 0);
    if (err < 0) {
        dlog("rec: capture pcm_open(%s): %s", dev, snd_strerror(err));
        rc_pcm = NULL;
        return -1;
    }
    err = snd_pcm_set_params(rc_pcm, REC_FORMAT, SND_PCM_ACCESS_RW_INTERLEAVED,
                              want_channels, want_rate, 1, 500000);
    if (err < 0) {
        dlog("rec: capture set_params: %s", snd_strerror(err));
        snd_pcm_close(rc_pcm);
        rc_pcm = NULL;
        return -1;
    }
    *sample_rate_out = want_rate;
    *channels_out    = want_channels;
    return 0;
}

static void *rc_thread_main(void *arg) {
    (void)arg;
    uint8_t buf[REC_PERIOD * 4]; /* up to 2 channels * 16-bit */
    int first_bytes_logged = 0;

    /* Explicitly start the capture stream. snd_pcm_readi will auto-start
       in most cases, but on some bluealsa setups and on a freshly-opened
       USB capture, the first readi blocks indefinitely until start() runs.
       Cheap insurance. */
    int err = snd_pcm_start(rc_pcm);
    if (err < 0 && err != -EBADFD) {
        dlog("rec: snd_pcm_start: %s", snd_strerror(err));
    }

    while (1) {
        pthread_mutex_lock(&rc_lock);
        while (rc_state == RC_PAUSED && !rc_stop_request) {
            pthread_cond_wait(&rc_resume_cv, &rc_lock);
        }
        if (rc_stop_request) {
            pthread_mutex_unlock(&rc_lock);
            break;
        }
        pthread_mutex_unlock(&rc_lock);

        snd_pcm_sframes_t n = snd_pcm_readi(rc_pcm, buf, REC_PERIOD);
        if (n < 0) {
            /* snd_pcm_recover handles EPIPE (overrun), ESTRPIPE (suspend),
               and EINTR. Anything else is fatal. */
            int rec = snd_pcm_recover(rc_pcm, (int)n, 0);
            if (rec < 0) {
                dlog("rec: readi: %s (unrecoverable)", snd_strerror((int)n));
                break;
            }
            dlog("rec: readi recovered from %s", snd_strerror((int)n));
            continue;
        }
        if (n == 0) {
            /* Shouldn't happen on a healthy capture device, but log it
               loudly if it does — this is what an output-only USB device
               would look like. */
            dlog("rec: readi returned 0 frames (no mic? muted?)");
            continue;
        }

        size_t bytes = (size_t)n * rc_channels * (REC_BITS / 8);
        size_t w = fwrite(buf, 1, bytes, rc_file);
        if (w != bytes) {
            dlog("rec: short write: %zu/%zu (disk full?)", w, bytes);
        }
        rc_data_bytes += (uint32_t)bytes;

        if (!first_bytes_logged) {
            dlog("rec: first %zu bytes captured (mic is live)", bytes);
            first_bytes_logged = 1;
        }
    }

    pthread_mutex_lock(&rc_lock);
    if (rc_state == RC_RECORDING) {
        rc_position_ms += (mono_ms() - rc_record_start_mono_ms);
    }
    if (rc_file) {
        dlog("rec: stopping, total captured = %u bytes", rc_data_bytes);
        rc_write_wav_header(rc_file, rc_data_bytes, rc_channels, rc_sample_rate);
        fclose(rc_file);
        rc_file = NULL;
    }
    if (rc_pcm) { snd_pcm_drop(rc_pcm); snd_pcm_close(rc_pcm); rc_pcm = NULL; }
    rc_state = RC_IDLE;
    rc_stop_request = 0;
    rc_record_start_mono_ms = 0;
    pthread_mutex_unlock(&rc_lock);
    return NULL;
}

int audio_record_start(const char *path) {
    if (!path) return -1;

    pthread_mutex_lock(&rc_lock);
    if (rc_state != RC_IDLE) {
        pthread_mutex_unlock(&rc_lock);
        return -1;
    }

    dlog("record_start: %s", path);
    rc_file = fopen(path, "wb");
    if (!rc_file) {
        dlog("record_start: cannot create %s: %s", path, strerror(errno));
        pthread_mutex_unlock(&rc_lock);
        return -1;
    }

    if (rc_open_pcm(&rc_sample_rate, &rc_channels) != 0) {
        fclose(rc_file); rc_file = NULL;
        remove(path); /* don't leave a zero-byte file behind */
        pthread_mutex_unlock(&rc_lock);
        return -1;
    }

    /* Write a placeholder header. We'll patch it with the final size
       when we close the file. */
    rc_write_wav_header(rc_file, 0, rc_channels, rc_sample_rate);
    rc_data_bytes = 0;

    rc_position_ms = 0;
    rc_record_start_mono_ms = mono_ms();
    rc_stop_request = 0;
    rc_state = RC_RECORDING;
    pthread_mutex_unlock(&rc_lock);

    if (pthread_create(&rc_thread_id, NULL, rc_thread_main, NULL) != 0) {
        pthread_mutex_lock(&rc_lock);
        if (rc_pcm)  { snd_pcm_close(rc_pcm); rc_pcm = NULL; }
        if (rc_file) { fclose(rc_file); rc_file = NULL; }
        rc_state = RC_IDLE;
        pthread_mutex_unlock(&rc_lock);
        return -1;
    }
    return 0;
}

int audio_record_pause(void) {
    pthread_mutex_lock(&rc_lock);
    if (rc_state != RC_RECORDING) {
        pthread_mutex_unlock(&rc_lock);
        return -1;
    }
    rc_position_ms += (mono_ms() - rc_record_start_mono_ms);
    rc_state = RC_PAUSED;
    /* Drop under the lock to discard the ~200 ms of audio the kernel
       has buffered. Resume's next readi picks up fresh audio. */
    if (rc_pcm) snd_pcm_drop(rc_pcm);
    pthread_mutex_unlock(&rc_lock);
    return 0;
}

int audio_record_resume(void) {
    pthread_mutex_lock(&rc_lock);
    if (rc_state != RC_PAUSED) {
        pthread_mutex_unlock(&rc_lock);
        return -1;
    }
    if (rc_pcm) snd_pcm_prepare(rc_pcm);
    rc_record_start_mono_ms = mono_ms();
    rc_state = RC_RECORDING;
    pthread_cond_broadcast(&rc_resume_cv);
    pthread_mutex_unlock(&rc_lock);
    return 0;
}

int audio_record_stop(void) {
    pthread_mutex_lock(&rc_lock);
    if (rc_state == RC_IDLE) {
        pthread_mutex_unlock(&rc_lock);
        return 0;
    }
    rc_stop_request = 1;
    pthread_cond_broadcast(&rc_resume_cv);
    /* Drop under the lock so the thread's cleanup can't race with us
       (it also takes rc_lock when it closes rc_pcm). */
    if (rc_pcm) snd_pcm_drop(rc_pcm);
    pthread_t t = rc_thread_id;
    rc_thread_id = 0;
    pthread_mutex_unlock(&rc_lock);

    if (t != 0) pthread_join(t, NULL);
    return 0;
}

int audio_is_recording(void) {
    pthread_mutex_lock(&rc_lock);
    int r = (rc_state == RC_RECORDING);
    pthread_mutex_unlock(&rc_lock);
    return r;
}

/* ───────────────────────── init ───────────────────────── */

int audio_init(void) {
    dlog("─── audio daemon starting ───");
    int card = load_card_num();
    char dev[32];
    card_to_pcm(card, dev, sizeof(dev));
    dlog("audio_init: USB card = %d (%s)", card, dev);
    mixer_try_attach();
    return 0;
}
