#include "audio_alsa.h"
#include <alsa/asoundlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>

/* ── dynamic card discovery ──────────────────────────────────────────────
   Card 0 is always bcm2835 (HDMI/headphone).
   Card 1+ is the first USB audio device (wired USB-C earphones or BT dongle).
   rcS writes the detected card number to /tmp/bh-audio-card at boot.
   We also scan /proc/asound here as a fallback.
   ─────────────────────────────────────────────────────────────────────── */

/* Returns the first card > 0 that has a usbbus node (i.e. is a USB device). */
static int find_usb_audio_card(void) {
    for (int n = 1; n <= 7; n++) {
        char path[64];
        snprintf(path, sizeof(path), "/proc/asound/card%d/usbbus", n);
        if (access(path, F_OK) == 0) return n;
    }
    return 1; /* default fallback */
}

/* Read card number written by rcS (/tmp/bh-audio-card) or detect live. */
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

/* Build a plughw device string from a card number. */
static void card_to_pcm(int card_num, char *buf, size_t sz) {
    snprintf(buf, sz, "plughw:%d,0", card_num);
}

/* ── mixer ── */
static snd_mixer_t      *mixer_handle = NULL;
static snd_mixer_elem_t *master_elem  = NULL;
static int current_volume = 50;

#define VOLUME_FLOOR_MB (-3000)

static void apply_volume(void) {
    if (!master_elem) return;

    long min_val, max_val;

    /* ── Try dB first (logarithmic — feels natural on hardware with dB support) */
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

    /* ── Fall back to raw integer range ──────────────────────────────────────
       Most USB audio class devices (Apple USB-C EarPods, generic adapters)
       only expose integer steps, not dB.  This is the common path. */
    if (snd_mixer_selem_get_playback_volume_range(master_elem, &min_val, &max_val) == 0
        && max_val > min_val) {
        long raw = min_val + (long)current_volume * (max_val - min_val) / 100;
        snd_mixer_selem_set_playback_volume_all(master_elem, raw);
    }
}

/* Try to attach the mixer to the USB audio card.
   Called lazily from audio_set_volume() so volume works even when
   earphones are plugged in AFTER the service starts. */
static int mixer_try_attach(void) {
    if (master_elem) return 0; /* already have volume control */

    /* Close stale handle (attached but no elem found previously) */
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

    /* Walk all elements, log them, and pick the best volume control.
       Priority: Master > Speaker > Headphone > PCM > Front > (first found) */
    static const char * const PREFERRED[] = {
        "Master", "Speaker", "Headphone", "PCM", "Front", NULL
    };

    snd_mixer_elem_t *elem = snd_mixer_first_elem(mixer_handle);
    while (elem) {
        const char *name = snd_mixer_selem_get_name(elem);
        int has_vol = snd_mixer_selem_has_playback_volume(elem);
        fprintf(stderr, "audio: mixer elem '%s' playback_vol=%d\n", name, has_vol);

        if (has_vol) {
            if (!master_elem) master_elem = elem; /* take first available */
            for (int i = 0; PREFERRED[i]; i++) {
                if (strcmp(name, PREFERRED[i]) == 0) {
                    master_elem = elem; /* upgrade to preferred name */
                    /* Keep going only if we haven't reached highest priority */
                    if (i == 0) goto found; /* "Master" — stop immediately */
                }
            }
        }
        elem = snd_mixer_elem_next(elem);
    }

found:
    if (master_elem) {
        fprintf(stderr, "audio: using mixer control '%s' on %s\n",
                snd_mixer_selem_get_name(master_elem), mixer_card);
        apply_volume(); /* push current_volume immediately */
        return 0;
    }

    fprintf(stderr, "audio: no volume control on %s\n", mixer_card);
    return -1;
}

/* ── playback ── */
static pthread_t        play_thread_id  = 0;
static int              playback_active = 0;
static pid_t            child_pid       = -1;
static volatile int     play_stop       = 0;
static pthread_mutex_t  play_mutex      = PTHREAD_MUTEX_INITIALIZER;

/* Owns a NULL-terminated argv array (each element strdup'd). Forks the
   command, waits for it, then frees argv and clears playback_active. */
static void *subprocess_playback_thread(void *arg) {
    char **argv = (char **)arg;

    pid_t pid = fork();
    if (pid == 0) {
        execvp(argv[0], argv);
        _exit(1);
    } else if (pid > 0) {
        pthread_mutex_lock(&play_mutex);
        child_pid = pid;
        pthread_mutex_unlock(&play_mutex);
        int status;
        waitpid(pid, &status, 0);
    }

    for (int i = 0; argv[i]; i++) free(argv[i]);
    free(argv);

    pthread_mutex_lock(&play_mutex);
    child_pid = -1;
    playback_active = 0;
    pthread_mutex_unlock(&play_mutex);
    return NULL;
}

static int ends_with_ci(const char *s, const char *suffix) {
    size_t sl = strlen(s), xl = strlen(suffix);
    return sl >= xl && strcasecmp(s + sl - xl, suffix) == 0;
}

/* ── native WAV playback thread ──
   Parses the WAV header, opens ALSA directly via plughw (no aplay needed),
   streams PCM frames, and stops cleanly when play_stop is set. */
static void *wav_playback_thread(void *arg) {
    char *path = (char *)arg;
    snd_pcm_t *pcm = NULL;
    FILE *f = NULL;
    uint8_t *buf = NULL;

    f = fopen(path, "rb");
    free(path);
    if (!f) goto cleanup;

    /* Read RIFF/WAVE header */
    uint8_t riff[12];
    if (fread(riff, 1, 12, f) < 12) goto cleanup;
    if (memcmp(riff, "RIFF", 4) != 0 || memcmp(riff + 8, "WAVE", 4) != 0) goto cleanup;

    uint16_t channels = 1, bits = 16;
    uint32_t sample_rate = 48000;
    int found_fmt = 0, found_data = 0;

    /* Walk chunks until we find fmt and data */
    while (!found_data) {
        uint8_t id[4], sz_buf[4];
        if (fread(id, 1, 4, f) < 4 || fread(sz_buf, 1, 4, f) < 4) break;
        uint32_t sz = (uint32_t)sz_buf[0] | ((uint32_t)sz_buf[1] << 8) |
                      ((uint32_t)sz_buf[2] << 16) | ((uint32_t)sz_buf[3] << 24);

        if (memcmp(id, "fmt ", 4) == 0 && sz >= 16) {
            uint8_t fb[16];
            if (fread(fb, 1, 16, f) < 16) goto cleanup;
            uint16_t afmt = (uint16_t)(fb[0] | (fb[1] << 8));
            if (afmt != 1) goto cleanup; /* PCM only */
            channels    = (uint16_t)(fb[2]  | (fb[3]  << 8));
            sample_rate = (uint32_t)(fb[4]  | (fb[5]  << 8) | (fb[6] << 16) | (fb[7] << 24));
            bits        = (uint16_t)(fb[14] | (fb[15] << 8));
            if (sz > 16) fseek(f, (long)(sz - 16), SEEK_CUR);
            found_fmt = 1;
        } else if (memcmp(id, "data", 4) == 0) {
            found_data = 1; /* audio data starts at current position */
        } else {
            fseek(f, (long)sz, SEEK_CUR);
        }
    }
    if (!found_fmt || !found_data) {
        fprintf(stderr, "wav_playback: invalid WAV file\n");
        goto cleanup;
    }

    /* Open ALSA PCM — plughw handles format/rate/channel conversion */
    char pcm_dev[32];
    card_to_pcm(load_card_num(), pcm_dev, sizeof(pcm_dev));
    int err = snd_pcm_open(&pcm, pcm_dev, SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) {
        fprintf(stderr, "wav_playback: pcm_open: %s\n", snd_strerror(err));
        goto cleanup;
    }

    snd_pcm_format_t fmt_alsa = (bits == 8)  ? SND_PCM_FORMAT_U8 :
                                 (bits == 24) ? SND_PCM_FORMAT_S24_3LE :
                                                SND_PCM_FORMAT_S16_LE;

    err = snd_pcm_set_params(pcm, fmt_alsa,
                              SND_PCM_ACCESS_RW_INTERLEAVED,
                              channels, sample_rate, 1, 500000);
    if (err < 0) {
        fprintf(stderr, "wav_playback: set_params: %s\n", snd_strerror(err));
        goto cleanup;
    }

    size_t frame_bytes   = (size_t)(channels * (bits / 8));
    size_t period_frames = 1024;
    buf = malloc(period_frames * frame_bytes);
    if (!buf) goto cleanup;

    while (!play_stop) {
        size_t got = fread(buf, frame_bytes, period_frames, f);
        if (got == 0) break;
        snd_pcm_sframes_t w = snd_pcm_writei(pcm, buf, (snd_pcm_uframes_t)got);
        if (w < 0) {
            w = snd_pcm_recover(pcm, (int)w, 0);
            if (w < 0) { fprintf(stderr, "wav_playback: writei: %s\n", snd_strerror((int)w)); break; }
        }
    }

    if (!play_stop) snd_pcm_drain(pcm);

cleanup:
    free(buf);
    if (pcm) { snd_pcm_drop(pcm); snd_pcm_close(pcm); }
    if (f) fclose(f);
    pthread_mutex_lock(&play_mutex);
    child_pid = -1;
    playback_active = 0;
    pthread_mutex_unlock(&play_mutex);
    return NULL;
}

/* ── recording ── */
#define REC_RATE         48000
#define REC_CHANNELS     1
#define REC_FORMAT       SND_PCM_FORMAT_S16_LE
#define REC_PERIOD       1024

static snd_pcm_t       *capture_handle    = NULL;
static pthread_t        rec_thread_id     = 0;
static int              recording_active  = 0;
static volatile int     rec_stop          = 0;
static pthread_mutex_t  rec_mutex         = PTHREAD_MUTEX_INITIALIZER;

static void write_wav_header(FILE *f, uint32_t data_size) {
    uint16_t audio_fmt   = 1;
    uint16_t channels    = REC_CHANNELS;
    uint32_t sample_rate = REC_RATE;
    uint32_t byte_rate   = REC_RATE * REC_CHANNELS * 2;
    uint16_t block_align = REC_CHANNELS * 2;
    uint16_t bits        = 16;
    uint32_t fmt_size    = 16;
    uint32_t chunk_size  = 36 + data_size;

    fwrite("RIFF", 1, 4, f);  fwrite(&chunk_size,  4, 1, f);
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);  fwrite(&fmt_size,    4, 1, f);
    fwrite(&audio_fmt,   2, 1, f);
    fwrite(&channels,    2, 1, f);
    fwrite(&sample_rate, 4, 1, f);
    fwrite(&byte_rate,   4, 1, f);
    fwrite(&block_align, 2, 1, f);
    fwrite(&bits,        2, 1, f);
    fwrite("data", 1, 4, f);  fwrite(&data_size,   4, 1, f);
}

static void *capture_thread(void *arg) {
    char *path = (char *)arg;

    FILE *wav = fopen(path, "wb");
    free(path);
    if (!wav) {
        pthread_mutex_lock(&rec_mutex);
        recording_active = 0;
        pthread_mutex_unlock(&rec_mutex);
        return NULL;
    }

    write_wav_header(wav, 0);  /* placeholder — fixed at end */

    char buf[REC_PERIOD * REC_CHANNELS * 2];
    uint32_t data_size = 0;

    while (!rec_stop) {
        snd_pcm_sframes_t n = snd_pcm_readi(capture_handle, buf, REC_PERIOD);
        if (n < 0) {
            if (n == -EPIPE) { snd_pcm_prepare(capture_handle); continue; }
            break;
        }
        size_t bytes = (size_t)n * REC_CHANNELS * 2;
        fwrite(buf, 1, bytes, wav);
        data_size += bytes;
    }

    /* Patch the WAV header with the real sizes now that we know them. */
    rewind(wav);
    write_wav_header(wav, data_size);
    fclose(wav);

    snd_pcm_drop(capture_handle);
    snd_pcm_close(capture_handle);
    capture_handle = NULL;
    rec_stop = 0;

    pthread_mutex_lock(&rec_mutex);
    recording_active = 0;
    pthread_mutex_unlock(&rec_mutex);
    return NULL;
}

/* ── public API ── */

int audio_init(void) {
    /* Try to attach mixer now. Non-fatal if USB audio not present yet —
       mixer_try_attach() will be called lazily from audio_set_volume(). */
    mixer_try_attach();
    return 0; /* always succeeds — service must start regardless */
}

void audio_play(const char *path) {
    pthread_mutex_lock(&play_mutex);
    if (playback_active) {
        pthread_mutex_unlock(&play_mutex);
        audio_stop();
        pthread_mutex_lock(&play_mutex);
    }
    playback_active = 1;
    pthread_mutex_unlock(&play_mutex);

    play_stop = 0;
    mixer_try_attach(); /* ensure volume is applied when playback starts */

    if (ends_with_ci(path, ".mp3")) {
        /* MP3 — subprocess via mpg123, targeting dynamic USB card */
        char pcm_dev[32];
        card_to_pcm(load_card_num(), pcm_dev, sizeof(pcm_dev));
        char **argv = malloc(6 * sizeof(char *));
        if (!argv) goto oom;
        argv[0] = strdup("mpg123");
        argv[1] = strdup("-q");
        argv[2] = strdup("-a");
        argv[3] = strdup(pcm_dev);
        argv[4] = strdup(path);
        argv[5] = NULL;
        pthread_create(&play_thread_id, NULL, subprocess_playback_thread, argv);
    } else {
        /* WAV (and anything else) — native ALSA thread, no aplay needed */
        char *path_copy = strdup(path);
        if (!path_copy) goto oom;
        pthread_create(&play_thread_id, NULL, wav_playback_thread, path_copy);
    }
    return;

oom:
    pthread_mutex_lock(&play_mutex);
    playback_active = 0;
    pthread_mutex_unlock(&play_mutex);
}

void audio_stop(void) {
    /* Signal WAV playback thread to stop */
    play_stop = 1;

    /* Kill mpg123 subprocess if running */
    pthread_mutex_lock(&play_mutex);
    if (child_pid > 0) kill(child_pid, SIGTERM);
    pthread_mutex_unlock(&play_mutex);

    if (play_thread_id != 0) {
        pthread_join(play_thread_id, NULL);
        play_thread_id = 0;
    }
    play_stop = 0;
}

void audio_set_volume(int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    current_volume = percent;
    mixer_try_attach(); /* no-op if already attached; retries if device just plugged in */
    if (master_elem) apply_volume();
}

int audio_get_volume(void) {
    if (!master_elem) return current_volume;

    /* Refresh mixer state so hardware changes (e.g. headset volume wheel)
       are picked up before we read back the current level. */
    snd_mixer_handle_events(mixer_handle);

    /* Mirror the dB vs raw-integer logic from apply_volume() in reverse. */
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
        return current_volume;
    }

    return current_volume;
}

int audio_is_playing(void) {
    int ret;
    pthread_mutex_lock(&play_mutex);
    ret = playback_active;
    pthread_mutex_unlock(&play_mutex);
    return ret;
}

int audio_record_start(const char *path) {
    pthread_mutex_lock(&rec_mutex);
    if (recording_active) { pthread_mutex_unlock(&rec_mutex); return -1; }

    char pcm_dev[32];
    card_to_pcm(load_card_num(), pcm_dev, sizeof(pcm_dev));
    int err = snd_pcm_open(&capture_handle, pcm_dev, SND_PCM_STREAM_CAPTURE, 0);
    if (err < 0) {
        fprintf(stderr, "audio_record_start: pcm_open: %s\n", snd_strerror(err));
        pthread_mutex_unlock(&rec_mutex);
        return -1;
    }

    err = snd_pcm_set_params(capture_handle, REC_FORMAT,
                              SND_PCM_ACCESS_RW_INTERLEAVED,
                              REC_CHANNELS, REC_RATE, 1, 500000);
    if (err < 0) {
        fprintf(stderr, "audio_record_start: set_params: %s\n", snd_strerror(err));
        snd_pcm_close(capture_handle);
        capture_handle = NULL;
        pthread_mutex_unlock(&rec_mutex);
        return -1;
    }

    char *path_copy = strdup(path);
    if (!path_copy) {
        snd_pcm_close(capture_handle);
        capture_handle = NULL;
        pthread_mutex_unlock(&rec_mutex);
        return -1;
    }

    rec_stop = 0;
    recording_active = 1;
    pthread_mutex_unlock(&rec_mutex);

    pthread_create(&rec_thread_id, NULL, capture_thread, path_copy);
    return 0;
}

void audio_record_stop(void) {
    pthread_mutex_lock(&rec_mutex);
    rec_stop = 1;
    pthread_mutex_unlock(&rec_mutex);

    if (rec_thread_id != 0) {
        pthread_join(rec_thread_id, NULL);
        rec_thread_id = 0;
    }
}

int audio_is_recording(void) {
    int ret;
    pthread_mutex_lock(&rec_mutex);
    ret = recording_active;
    pthread_mutex_unlock(&rec_mutex);
    return ret;
}
