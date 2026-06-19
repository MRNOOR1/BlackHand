/*
 * voice_memo_service.c — UI-side memo metadata + thin daemon facade.
 *
 * The daemon (blackhand-audio) owns all audio I/O, routing, the
 * recording PID, and the playback elapsed clock. This service owns:
 *   • The filesystem index of memos (scan, list, rename, delete)
 *   • The user-facing state machine (VM_IDLE / RECORDING / PLAYING / …)
 *   • Filename generation
 *   • Play mode (normal / repeat-one / repeat-all / shuffle) and
 *     next/prev navigation
 *
 * No forks, no ALSA, no monotonic timers, no MAC addresses. All those
 * concerns moved into the daemon.
 */

#include "voice_memo_service.h"
#include "../config.h"

#include <dirent.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <stdint.h>
#include <unistd.h>
#include <pthread.h>

#include "../ui-ipcs/audio_ipc.h"

#define INITIAL_MEMO_CAPACITY 16

static VoiceMemo **memos_index = NULL;
static size_t memos_count = 0;
static size_t memos_capacity = 0;

static VMState   current_state = VM_IDLE;
static VoiceMemo *current_memo = NULL;
static unsigned  memo_serial = 0;
static VMPlayMode play_mode = VM_PLAYMODE_NORMAL;

/* Last elapsed values polled from the daemon, cached so the UI can
   render them without re-issuing an IPC call inside the draw loop. */
static int cached_play_elapsed_ms = 0;
static int cached_record_elapsed_ms = 0;
static int64_t record_start_wallclock_ms = 0;

/* Filename of the recording currently in flight. We need this in
   record_stop so we can find the file we just wrote. */
static char active_record_filename[160] = {0};

/* Protects all of the above. */
static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;

static const char *VOICE_MEMO_PATH = APP_PATH_VOICE_MEMOS_DIR;

/* ───────────────────────── helpers ───────────────────────── */

static int64_t monotonic_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

static int ensure_capacity(void) {
    if (memos_count < memos_capacity) return 0;
    size_t new_capacity = (memos_capacity == 0) ? INITIAL_MEMO_CAPACITY : memos_capacity * 2;
    VoiceMemo **new_array = realloc(memos_index, new_capacity * sizeof(VoiceMemo *));
    if (!new_array) return -1;
    memos_index = new_array;
    memos_capacity = new_capacity;
    return 0;
}

static void insert_at_front(VoiceMemo *memo) {
    for (size_t i = memos_count; i > 0; i--) memos_index[i] = memos_index[i - 1];
    memos_index[0] = memo;
    memos_count++;
}

static char *make_timestamp_filename(void) {
    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    char name[64];
    strftime(name, sizeof(name), "memo_%Y%m%d_%H%M%S", &tmv);
    char final_name[80];
    snprintf(final_name, sizeof(final_name), "%s_%u.wav", name, memo_serial++);
    return strdup(final_name);
}

static int read_wav_duration_ms(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    unsigned char hdr[44];
    if (fread(hdr, 1, sizeof(hdr), f) != sizeof(hdr)) { fclose(f); return 0; }
    fclose(f);
    if (memcmp(hdr + 0, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0) return 0;
    uint32_t byte_rate = (uint32_t)hdr[28] | ((uint32_t)hdr[29] << 8) |
                        ((uint32_t)hdr[30] << 16) | ((uint32_t)hdr[31] << 24);
    uint32_t data_bytes = (uint32_t)hdr[40] | ((uint32_t)hdr[41] << 8) |
                         ((uint32_t)hdr[42] << 16) | ((uint32_t)hdr[43] << 24);
    if (byte_rate == 0) return 0;
    return (int)((data_bytes * 1000U) / byte_rate);
}

static int make_filename_from_title(const char *title, char *out, size_t out_size) {
    if (!title || !out || out_size < 8) return -1;
    size_t j = 0;
    int last_underscore = 0;
    for (size_t i = 0; title[i] != '\0' && j + 5 < out_size; i++) {
        unsigned char ch = (unsigned char)title[i];
        if (isalnum(ch)) {
            out[j++] = (char)tolower(ch);
            last_underscore = 0;
        } else if (ch == ' ' || ch == '-' || ch == '_') {
            if (!last_underscore && j > 0) { out[j++] = '_'; last_underscore = 1; }
        }
    }
    while (j > 0 && out[j - 1] == '_') j--;
    if (j == 0) return -1;
    out[j] = '\0';
    strncat(out, ".wav", out_size - strlen(out) - 1);
    return 0;
}

static int path_exists(const char *p) { return access(p, F_OK) == 0; }

/* ───────────────────────── init / shutdown ───────────────────────── */

void voice_memo_service_init(void) {
    memos_index = malloc(INITIAL_MEMO_CAPACITY * sizeof(VoiceMemo *));
    if (!memos_index) return;
    memos_capacity = INITIAL_MEMO_CAPACITY;
    memos_count = 0;
    current_state = VM_IDLE;
    current_memo = NULL;
    play_mode = VM_PLAYMODE_NORMAL;
    cached_play_elapsed_ms = 0;
    cached_record_elapsed_ms = 0;
    record_start_wallclock_ms = 0;
    active_record_filename[0] = '\0';

    struct stat st = {0};
    if (stat(VOICE_MEMO_PATH, &st) == -1) {
        if (mkdir(VOICE_MEMO_PATH, 0755) != 0) {
            free(memos_index); memos_index = NULL;
            memos_capacity = 0; memos_count = 0;
            return;
        }
    }

    DIR *dir = opendir(VOICE_MEMO_PATH);
    if (!dir) {
        free(memos_index); memos_index = NULL;
        memos_capacity = 0; memos_count = 0;
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        const char *ext = strrchr(entry->d_name, '.');
        if (!ext || strcmp(ext, ".wav") != 0) continue;
        if (ensure_capacity() != 0) continue;

        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", VOICE_MEMO_PATH, entry->d_name);
        VoiceMemo *memo = malloc(sizeof(VoiceMemo));
        if (!memo) continue;
        memo->filename = strdup(entry->d_name);
        memo->duration_ms = read_wav_duration_ms(path);
        if (!memo->filename) { free(memo); continue; }
        insert_at_front(memo);
    }
    closedir(dir);
}

void voice_memo_service_shutdown(void) {
    audio_ipc_play_stop();
    audio_ipc_record_stop();
    if (!memos_index) return;
    for (size_t i = 0; i < memos_count; i++) {
        free(memos_index[i]->filename);
        free(memos_index[i]);
    }
    free(memos_index);
    memos_index = NULL;
    memos_capacity = 0;
    memos_count = 0;
    current_state = VM_IDLE;
    current_memo = NULL;
}

/* ───────────────────────── accessors ───────────────────────── */

VMState voice_memo_service_state(void) {
    pthread_mutex_lock(&s_lock);
    VMState s = current_state;
    pthread_mutex_unlock(&s_lock);
    return s;
}

const VoiceMemo *voice_memo_service_current(void) { return current_memo; }

const VoiceMemo **voice_memo_service_list_all(size_t *out_count) {
    if (out_count) *out_count = memos_count;
    return (const VoiceMemo **)memos_index;
}

const VoiceMemo *voice_memo_service_get_by_filename(const char *filename) {
    if (!filename) return NULL;
    for (size_t i = 0; i < memos_count; i++) {
        if (strcmp(memos_index[i]->filename, filename) == 0) return memos_index[i];
    }
    return NULL;
}

/* ───────────────────────── recording ───────────────────────── */

int voice_memo_service_record_start(void) {
    pthread_mutex_lock(&s_lock);
    if (current_state != VM_IDLE) { pthread_mutex_unlock(&s_lock); return -1; }
    pthread_mutex_unlock(&s_lock);

    char *fname = make_timestamp_filename();
    if (!fname) return -1;
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", VOICE_MEMO_PATH, fname);

    if (audio_ipc_record_start(path) != 0) {
        free(fname);
        return -1;
    }

    pthread_mutex_lock(&s_lock);
    snprintf(active_record_filename, sizeof(active_record_filename), "%s", fname);
    free(fname);
    current_state = VM_RECORDING;
    current_memo = NULL;
    cached_record_elapsed_ms = 0;
    record_start_wallclock_ms = monotonic_ms();
    pthread_mutex_unlock(&s_lock);
    return 0;
}

int voice_memo_service_record_pause(void) {
    pthread_mutex_lock(&s_lock);
    if (current_state != VM_RECORDING) { pthread_mutex_unlock(&s_lock); return -1; }
    pthread_mutex_unlock(&s_lock);

    if (audio_ipc_record_pause() != 0) return -1;

    pthread_mutex_lock(&s_lock);
    cached_record_elapsed_ms += (int)(monotonic_ms() - record_start_wallclock_ms);
    current_state = VM_RECORDING_PAUSED;
    pthread_mutex_unlock(&s_lock);
    return 0;
}

int voice_memo_service_record_resume(void) {
    pthread_mutex_lock(&s_lock);
    if (current_state != VM_RECORDING_PAUSED) { pthread_mutex_unlock(&s_lock); return -1; }
    pthread_mutex_unlock(&s_lock);

    if (audio_ipc_record_resume() != 0) return -1;

    pthread_mutex_lock(&s_lock);
    record_start_wallclock_ms = monotonic_ms();
    current_state = VM_RECORDING;
    pthread_mutex_unlock(&s_lock);
    return 0;
}

int voice_memo_service_record_cancel(void) {
    pthread_mutex_lock(&s_lock);
    if (current_state != VM_RECORDING && current_state != VM_RECORDING_PAUSED) {
        pthread_mutex_unlock(&s_lock);
        return -1;
    }
    char path_to_remove[1024] = {0};
    if (active_record_filename[0] != '\0')
        snprintf(path_to_remove, sizeof(path_to_remove), "%s/%s",
                 VOICE_MEMO_PATH, active_record_filename);
    current_state = VM_IDLE;
    current_memo = NULL;
    cached_record_elapsed_ms = 0;
    record_start_wallclock_ms = 0;
    active_record_filename[0] = '\0';
    pthread_mutex_unlock(&s_lock);

    audio_ipc_record_stop();
    if (path_to_remove[0]) remove(path_to_remove);
    return 0;
}

int voice_memo_service_record_stop(const char *title_optional) {
    pthread_mutex_lock(&s_lock);
    if (current_state != VM_RECORDING && current_state != VM_RECORDING_PAUSED) {
        pthread_mutex_unlock(&s_lock);
        return -1;
    }
    if (ensure_capacity() != 0) { pthread_mutex_unlock(&s_lock); return -1; }

    int duration_ms = cached_record_elapsed_ms;
    if (current_state == VM_RECORDING) {
        duration_ms += (int)(monotonic_ms() - record_start_wallclock_ms);
    }
    char fname_copy[160] = {0};
    strncpy(fname_copy, active_record_filename, sizeof(fname_copy) - 1);
    current_state = VM_IDLE;
    current_memo = NULL;
    cached_record_elapsed_ms = 0;
    record_start_wallclock_ms = 0;
    active_record_filename[0] = '\0';
    pthread_mutex_unlock(&s_lock);

    /* Daemon flushes WAV header and closes the file. */
    audio_ipc_record_stop();

    VoiceMemo *memo = malloc(sizeof(VoiceMemo));
    if (!memo) return -1;
    memo->filename = (fname_copy[0] != '\0') ? strdup(fname_copy) : make_timestamp_filename();
    memo->duration_ms = duration_ms;
    if (!memo->filename) { free(memo); return -1; }

    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", VOICE_MEMO_PATH, memo->filename);

    /* If the file isn't on disk, the recording failed at the daemon —
       drop the memo rather than fabricating a silent placeholder. */
    if (!path_exists(path)) {
        free(memo->filename);
        free(memo);
        return -1;
    }

    int hdr_duration = read_wav_duration_ms(path);
    if (hdr_duration > 0) memo->duration_ms = hdr_duration;

    if (title_optional && title_optional[0] != '\0') {
        char base_name[128] = {0};
        if (make_filename_from_title(title_optional, base_name, sizeof(base_name)) == 0) {
            char final_name[160] = {0};
            char new_path[1024] = {0};
            int suffix = 0;
            while (1) {
                if (suffix == 0) {
                    snprintf(final_name, sizeof(final_name), "%s", base_name);
                } else {
                    const char *dot = strrchr(base_name, '.');
                    if (!dot) break;
                    int stem_len = (int)(dot - base_name);
                    snprintf(final_name, sizeof(final_name), "%.*s_%d.wav", stem_len, base_name, suffix);
                }
                snprintf(new_path, sizeof(new_path), "%s/%s", VOICE_MEMO_PATH, final_name);
                if (!path_exists(new_path)) break;
                suffix++;
            }
            if (final_name[0] != '\0' && strcmp(final_name, memo->filename) != 0) {
                if (rename(path, new_path) == 0) {
                    free(memo->filename);
                    memo->filename = strdup(final_name);
                }
            }
        }
    }

    pthread_mutex_lock(&s_lock);
    insert_at_front(memo);
    pthread_mutex_unlock(&s_lock);
    return 0;
}

/* ───────────────────────── playback ───────────────────────── */

int voice_memo_service_play_start(const char *filename) {
    if (!filename) return -1;

    pthread_mutex_lock(&s_lock);
    if (current_state == VM_RECORDING || current_state == VM_RECORDING_PAUSED) {
        pthread_mutex_unlock(&s_lock);
        return -1;
    }
    pthread_mutex_unlock(&s_lock);

    const VoiceMemo *found = voice_memo_service_get_by_filename(filename);
    if (!found) return -1;

    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", VOICE_MEMO_PATH, found->filename);

    if (audio_ipc_play_start(path) != 0) return -1;

    pthread_mutex_lock(&s_lock);
    current_memo = (VoiceMemo *)found;
    current_state = VM_PLAYING;
    cached_play_elapsed_ms = 0;
    pthread_mutex_unlock(&s_lock);
    return 0;
}

int voice_memo_service_play_pause(void) {
    pthread_mutex_lock(&s_lock);
    if (current_state != VM_PLAYING) { pthread_mutex_unlock(&s_lock); return -1; }
    pthread_mutex_unlock(&s_lock);

    if (audio_ipc_play_pause() != 0) return -1;

    pthread_mutex_lock(&s_lock);
    current_state = VM_PAUSED;
    pthread_mutex_unlock(&s_lock);
    return 0;
}

int voice_memo_service_play_resume(void) {
    pthread_mutex_lock(&s_lock);
    if (current_state != VM_PAUSED) { pthread_mutex_unlock(&s_lock); return -1; }
    pthread_mutex_unlock(&s_lock);

    if (audio_ipc_play_resume() != 0) return -1;

    pthread_mutex_lock(&s_lock);
    current_state = VM_PLAYING;
    pthread_mutex_unlock(&s_lock);
    return 0;
}

int voice_memo_service_play_stop(void) {
    pthread_mutex_lock(&s_lock);
    if (current_state != VM_PLAYING && current_state != VM_PAUSED) {
        pthread_mutex_unlock(&s_lock);
        return -1;
    }
    current_state = VM_IDLE;
    current_memo = NULL;
    cached_play_elapsed_ms = 0;
    pthread_mutex_unlock(&s_lock);

    audio_ipc_play_stop();
    return 0;
}

int voice_memo_service_seek_relative(int delta_ms) {
    pthread_mutex_lock(&s_lock);
    if (current_state != VM_PLAYING && current_state != VM_PAUSED) {
        pthread_mutex_unlock(&s_lock);
        return -1;
    }
    if (!current_memo) { pthread_mutex_unlock(&s_lock); return -1; }
    int total = current_memo->duration_ms;
    int current = cached_play_elapsed_ms;
    pthread_mutex_unlock(&s_lock);

    if (total <= 0) return -1;
    int next = current + delta_ms;
    if (next < 0) next = 0;
    if (next > total) next = total;

    if (audio_ipc_play_seek(next) != 0) return -1;

    pthread_mutex_lock(&s_lock);
    cached_play_elapsed_ms = next;
    pthread_mutex_unlock(&s_lock);
    return 0;
}

/* ───────────────────────── metadata ops (filesystem) ───────────────────────── */

int voice_memo_service_delete(const char *filename) {
    if (!filename) return -1;

    for (size_t i = 0; i < memos_count; i++) {
        VoiceMemo *memo = memos_index[i];
        if (strcmp(memo->filename, filename) != 0) continue;

        pthread_mutex_lock(&s_lock);
        int is_current = (current_memo == memo);
        if (is_current) {
            current_state = VM_IDLE;
            current_memo = NULL;
            cached_play_elapsed_ms = 0;
        }
        pthread_mutex_unlock(&s_lock);

        if (is_current) audio_ipc_play_stop();

        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", VOICE_MEMO_PATH, memo->filename);
        remove(path);

        free(memo->filename);
        free(memo);
        for (size_t j = i; j + 1 < memos_count; j++) memos_index[j] = memos_index[j + 1];
        memos_count--;
        return 0;
    }
    return -1;
}

int voice_memo_service_rename(const char *old_filename, const char *new_title) {
    if (!old_filename || !new_title) return -1;

    VoiceMemo *target = NULL;
    for (size_t i = 0; i < memos_count; i++) {
        if (strcmp(memos_index[i]->filename, old_filename) == 0) {
            target = memos_index[i];
            break;
        }
    }
    if (!target) return -1;

    char base_name[128] = {0};
    if (make_filename_from_title(new_title, base_name, sizeof(base_name)) != 0) return -1;

    char final_name[160] = {0};
    char old_path[1024];
    char new_path[1024];
    int suffix = 0;
    while (1) {
        if (suffix == 0) {
            snprintf(final_name, sizeof(final_name), "%s", base_name);
        } else {
            const char *dot = strrchr(base_name, '.');
            if (!dot) return -1;
            int stem_len = (int)(dot - base_name);
            if (stem_len < 1) return -1;
            snprintf(final_name, sizeof(final_name), "%.*s_%d.wav", stem_len, base_name, suffix);
        }
        snprintf(old_path, sizeof(old_path), "%s/%s", VOICE_MEMO_PATH, old_filename);
        snprintf(new_path, sizeof(new_path), "%s/%s", VOICE_MEMO_PATH, final_name);
        if (!path_exists(new_path) || strcmp(old_filename, final_name) == 0) break;
        suffix++;
    }

    if (strcmp(old_filename, final_name) == 0) return 0;
    if (rename(old_path, new_path) != 0) return -1;

    char *new_copy = strdup(final_name);
    if (!new_copy) return -1;
    free(target->filename);
    target->filename = new_copy;
    return 0;
}

/* ───────────────────────── tick / status ───────────────────────── */

int voice_memo_service_tick(void) {
    /* Poll the daemon once per tick. This is the single point of truth
       for "is anything still happening?" — no local timer math.
       Daemon reports playing=true synchronously after play_start, so
       there's no startup race and no grace window needed. */
    int playing = 0, paused = 0, recording = 0, elapsed = 0;
    int ipc_ok = (audio_ipc_status_full(&playing, &paused, &recording, &elapsed, NULL) == 0);

    int do_next = 0;
    int do_repeat = 0;
    const char *repeat_filename = NULL;

    pthread_mutex_lock(&s_lock);
    if (ipc_ok) {
        if (current_state == VM_PLAYING || current_state == VM_PAUSED) {
            cached_play_elapsed_ms = elapsed;
        }
        if (current_state == VM_PLAYING && !playing && !paused) {
            /* Daemon finished the file (or routing dropped). Advance per
               play mode. The daemon is authoritative — no duration
               cross-check needed. */
            if (play_mode == VM_PLAYMODE_REPEAT_ONE && current_memo) {
                repeat_filename = current_memo->filename;
                do_repeat = 1;
            } else {
                do_next = 1;
            }
        }
    }
    pthread_mutex_unlock(&s_lock);

    if (do_repeat) return voice_memo_service_play_start(repeat_filename);
    if (do_next)   return voice_memo_service_next();
    return 0;
}

int voice_memo_service_elapsed_ms(void) {
    pthread_mutex_lock(&s_lock);
    int ms;
    if (current_state == VM_RECORDING) {
        ms = cached_record_elapsed_ms + (int)(monotonic_ms() - record_start_wallclock_ms);
    } else if (current_state == VM_RECORDING_PAUSED) {
        ms = cached_record_elapsed_ms;
    } else {
        ms = cached_play_elapsed_ms;
    }
    pthread_mutex_unlock(&s_lock);
    return ms;
}

int voice_memo_service_total_ms(void) {
    if (!current_memo) return 0;
    return current_memo->duration_ms;
}

/* ───────────────────────── navigation / mode ───────────────────────── */

int voice_memo_service_next(void) {
    if (memos_count == 0) return -1;
    if (current_memo && play_mode == VM_PLAYMODE_REPEAT_ONE)
        return voice_memo_service_play_start(current_memo->filename);

    int cur = -1;
    if (current_memo) {
        for (size_t i = 0; i < memos_count; i++) {
            if (memos_index[i] == current_memo) { cur = (int)i; break; }
        }
    }

    int next;
    if (play_mode == VM_PLAYMODE_SHUFFLE) {
        next = rand() % (int)memos_count;
        if (memos_count > 1 && next == cur) next = (next + 1) % (int)memos_count;
    } else {
        next = cur + 1;
        if (next >= (int)memos_count) {
            if (play_mode == VM_PLAYMODE_REPEAT_ALL) {
                next = 0;
            } else {
                voice_memo_service_play_stop();
                return 0;
            }
        }
    }
    return voice_memo_service_play_start(memos_index[next]->filename);
}

int voice_memo_service_prev(void) {
    if (memos_count == 0) return -1;
    if (current_memo && play_mode == VM_PLAYMODE_REPEAT_ONE)
        return voice_memo_service_play_start(current_memo->filename);

    int cur = -1;
    if (current_memo) {
        for (size_t i = 0; i < memos_count; i++) {
            if (memos_index[i] == current_memo) { cur = (int)i; break; }
        }
    }

    int prev = (cur < 0) ? 0 : (cur - 1);
    if (play_mode == VM_PLAYMODE_SHUFFLE) {
        prev = rand() % (int)memos_count;
        if (memos_count > 1 && prev == cur) prev = (prev + 1) % (int)memos_count;
    } else if (prev < 0) {
        prev = (play_mode == VM_PLAYMODE_REPEAT_ALL) ? (int)memos_count - 1 : 0;
    }
    return voice_memo_service_play_start(memos_index[prev]->filename);
}

void voice_memo_service_cycle_mode(void) {
    play_mode = (VMPlayMode)(((int)play_mode + 1) % 4);
}

VMPlayMode voice_memo_service_mode(void) { return play_mode; }

const char *voice_memo_service_mode_label(void) {
    switch (play_mode) {
        case VM_PLAYMODE_REPEAT_ONE: return "REPEAT 1";
        case VM_PLAYMODE_REPEAT_ALL: return "REPEAT ALL";
        case VM_PLAYMODE_SHUFFLE:    return "SHUFFLE";
        default:                      return "NORMAL";
    }
}
