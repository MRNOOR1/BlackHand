#include "mp3_service.h"

#include <dirent.h>
#include <math.h>
#include <mpg123.h>
#include <out123.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* ── Library storage ─────────────────────────────────────────────────── */
static Mp3Category *categories = NULL;
static size_t category_count = 0;
static size_t category_capacity = 0;

/* ── Playback state ──────────────────────────────────────────────────── */
static mp3_playback_state state = MP3_STOPPED;
static size_t cur_cat = 0;
static size_t cur_col = 0;
static size_t cur_track = 0;
static int    cur_is_direct = 0; /* 1 if playing direct tracks in a category */
static time_t start_time = 0;
static unsigned pause_offset = 0;
static unsigned total_duration = 0;
static int volume_percent = 80;
static int finished_naturally = 0;
static char g_audio_root[512] = {0};

/* ── Thread state ────────────────────────────────────────────────────── */
static pthread_t player_thread;
static int thread_running = 0;
static int stop_requested = 0;
static pthread_mutex_t mp3_lock = PTHREAD_MUTEX_INITIALIZER;

static float viz_levels[MP3_VIZ_BINS] = {0};

/* ── Helper: title from filename ─────────────────────────────────────── */
static char *title_from_filename(const char *filename) {
    char *copy = strdup(filename);
    if (!copy) return strdup("Unknown");
    char *dot = strrchr(copy, '.');
    if (dot) *dot = '\0';
    for (char *p = copy; *p; p++) {
        if (*p == '_') *p = ' ';
    }
    return copy;
}

static char *title_from_metadata_or_filename(const char *path, const char *filename) {
    int err = 0;
    mpg123_handle *mh = mpg123_new(NULL, &err);
    if (!mh) return title_from_filename(filename);

    char *out = NULL;
    if (mpg123_open(mh, path) == MPG123_OK) {
        mpg123_id3v1 *v1 = NULL;
        mpg123_id3v2 *v2 = NULL;
        mpg123_id3(mh, &v1, &v2);
        if (v2 && v2->title && v2->title->p && v2->title->fill > 0) {
            out = strdup(v2->title->p);
        } else if (v1 && v1->title[0] != '\0') {
            char tbuf[64];
            snprintf(tbuf, sizeof(tbuf), "%s", v1->title);
            out = strdup(tbuf);
        }
    }
    mpg123_close(mh);
    mpg123_delete(mh);

    if (!out || out[0] == '\0') {
        free(out);
        return title_from_filename(filename);
    }
    return out;
}

static unsigned duration_from_mp3(const char *path) {
    if (!path) return 0;
    int err = 0;
    mpg123_handle *mh = mpg123_new(NULL, &err);
    if (!mh) return 0;
    unsigned out = 0;
    if (mpg123_open(mh, path) == MPG123_OK) {
        long rate = 0;
        int channels = 0, encoding = 0;
        if (mpg123_getformat(mh, &rate, &channels, &encoding) == MPG123_OK && rate > 0) {
            off_t samples = mpg123_length(mh);
            if (samples > 0) {
                out = (unsigned)(samples / rate);
            }
        }
    }
    mpg123_close(mh);
    mpg123_delete(mh);
    return out;
}

/* ── Visualizer ──────────────────────────────────────────────────────── */
static void clear_visualizer(void) {
    for (int i = 0; i < MP3_VIZ_BINS; i++) viz_levels[i] = 0.0f;
}

static void update_visualizer_from_pcm16(const int16_t *samples, size_t sample_count, int channels) {
    if (!samples || sample_count == 0) return;
    if (channels < 1) channels = 1;
    size_t frames = sample_count / (size_t)channels;
    if (frames == 0) return;
    size_t frames_per_bin = frames / MP3_VIZ_BINS;
    if (frames_per_bin == 0) frames_per_bin = 1;

    for (int b = 0; b < MP3_VIZ_BINS; b++) {
        size_t start = (size_t)b * frames_per_bin;
        if (start >= frames) { viz_levels[b] *= 0.85f; continue; }
        size_t end = start + frames_per_bin;
        if (end > frames) end = frames;
        double sum_sq = 0.0;
        size_t count = 0;
        for (size_t f = start; f < end; f++) {
            int16_t s = samples[f * (size_t)channels];
            double n = (double)s / 32768.0;
            sum_sq += n * n;
            count++;
        }
        float rms = 0.0f;
        if (count > 0) rms = (float)sqrt(sum_sq / (double)count);
        float smoothed = (viz_levels[b] * 0.7f) + (rms * 0.3f);
        if (smoothed > 1.0f) smoothed = 1.0f;
        viz_levels[b] = smoothed;
    }
}

/* ── Library helpers ─────────────────────────────────────────────────── */
static int ensure_category_capacity(void) {
    if (category_count < category_capacity) return 0;
    size_t new_cap = (category_capacity == 0) ? 8 : category_capacity * 2;
    if (new_cap > MP3_MAX_CATEGORIES) new_cap = MP3_MAX_CATEGORIES;
    Mp3Category *newp = realloc(categories, new_cap * sizeof(Mp3Category));
    if (!newp) return -1;
    categories = newp;
    category_capacity = new_cap;
    return 0;
}

static int ensure_collection_capacity(Mp3Category *cat) {
    if (cat->collection_count < cat->collection_capacity) return 0;
    size_t new_cap = (cat->collection_capacity == 0) ? 8 : cat->collection_capacity * 2;
    if (new_cap > MP3_MAX_COLLECTIONS) new_cap = MP3_MAX_COLLECTIONS;
    Mp3Collection *newp = realloc(cat->collections, new_cap * sizeof(Mp3Collection));
    if (!newp) return -1;
    cat->collections = newp;
    cat->collection_capacity = new_cap;
    return 0;
}

static int ensure_track_capacity(Mp3Track **tracks, size_t *count, size_t *capacity) {
    if (*count < *capacity) return 0;
    size_t new_cap = (*capacity == 0) ? 16 : *capacity * 2;
    if (new_cap > MP3_MAX_TRACKS) new_cap = MP3_MAX_TRACKS;
    Mp3Track *newp = realloc(*tracks, new_cap * sizeof(Mp3Track));
    if (!newp) return -1;
    *tracks = newp;
    *capacity = new_cap;
    return 0;
}

static void add_track(Mp3Track **tracks, size_t *count, size_t *capacity,
                      const char *path, const char *filename) {
    if (ensure_track_capacity(tracks, count, capacity) != 0) return;
    Mp3Track *t = &(*tracks)[*count];
    t->path = strdup(path);
    t->title = title_from_metadata_or_filename(path, filename);
    t->duration = duration_from_mp3(path);
    if (!t->path || !t->title) {
        free(t->path);
        free(t->title);
        return;
    }
    (*count)++;
}

/* ── Scan a collection folder for .mp3 files ─────────────────────────── */
static void scan_collection_dir(Mp3Collection *col, const char *col_path) {
    DIR *dir = opendir(col_path);
    if (!dir) return;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        char full_path[1024];
        snprintf(full_path, sizeof(full_path), "%s/%s", col_path, entry->d_name);

        struct stat st;
        if (stat(full_path, &st) != 0) continue;

        if (S_ISREG(st.st_mode)) {
            const char *ext = strrchr(entry->d_name, '.');
            if (!ext || strcmp(ext, ".mp3") != 0) continue;
            add_track(&col->tracks, &col->track_count, &col->track_capacity,
                      full_path, entry->d_name);
        } else if (S_ISDIR(st.st_mode)) {
            DIR *sub = opendir(full_path);
            if (!sub) continue;
            struct dirent *sube;
            while ((sube = readdir(sub)) != NULL) {
                if (sube->d_name[0] == '.') continue;
                const char *ext = strrchr(sube->d_name, '.');
                if (!ext || strcmp(ext, ".mp3") != 0) continue;
                char nested[1024];
                snprintf(nested, sizeof(nested), "%s/%s", full_path, sube->d_name);
                struct stat nst;
                if (stat(nested, &nst) != 0 || !S_ISREG(nst.st_mode)) continue;
                add_track(&col->tracks, &col->track_count, &col->track_capacity,
                          nested, sube->d_name);
            }
            closedir(sub);
        }
    }
    closedir(dir);
}

/* ── Scan a category folder ──────────────────────────────────────────── */
static void scan_category_dir(Mp3Category *cat, const char *cat_path) {
    DIR *dir = opendir(cat_path);
    if (!dir) return;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;

        char full_path[1024];
        snprintf(full_path, sizeof(full_path), "%s/%s", cat_path, entry->d_name);

        struct stat st;
        if (stat(full_path, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            /* This is a collection subfolder */
            if (ensure_collection_capacity(cat) != 0) continue;
            Mp3Collection *col = &cat->collections[cat->collection_count];
            memset(col, 0, sizeof(Mp3Collection));
            col->name = strdup(entry->d_name);
            if (!col->name) continue;

            scan_collection_dir(col, full_path);

            /* Only add if it has tracks */
            if (col->track_count > 0) {
                cat->collection_count++;
            } else {
                free(col->name);
                free(col->tracks);
            }
        } else if (S_ISREG(st.st_mode)) {
            /* Direct mp3 file in category */
            const char *ext = strrchr(entry->d_name, '.');
            if (ext && strcmp(ext, ".mp3") == 0) {
                add_track(&cat->direct_tracks, &cat->direct_track_count,
                          &cat->direct_track_capacity, full_path, entry->d_name);
            }
        }
    }
    closedir(dir);
}

/* ── Player thread ───────────────────────────────────────────────────── */
typedef struct {
    char *path;
} player_args_t;

static void *player_thread_fn(void *arg) {
    player_args_t *args = (player_args_t *)arg;
    if (!args || !args->path) { free(args); return NULL; }

    mpg123_handle *mh = NULL;
    out123_handle *ao = NULL;
    unsigned char *buffer = NULL;
    size_t outblock = 0;
    long rate = 0;
    int channels = 0, encoding = 0;

    int err = 0;
    mh = mpg123_new(NULL, &err);
    if (!mh) goto cleanup;
    if (mpg123_open(mh, args->path) != MPG123_OK) goto cleanup;
    if (mpg123_getformat(mh, &rate, &channels, &encoding) != MPG123_OK) goto cleanup;

    ao = out123_new();
    if (!ao) goto cleanup;
    if (out123_open(ao, NULL, NULL) != 0) goto cleanup;
    if (out123_start(ao, rate, channels, encoding) != 0) goto cleanup;

    outblock = mpg123_outblock(mh);
    if (outblock == 0) goto cleanup;
    buffer = malloc(outblock);
    if (!buffer) goto cleanup;

    int paused_locally = 0;
    while (1) {
        pthread_mutex_lock(&mp3_lock);
        int should_stop = stop_requested;
        mp3_playback_state st = state;
        pthread_mutex_unlock(&mp3_lock);

        if (should_stop) break;

        if (st == MP3_PAUSED) {
            if (!paused_locally) { out123_pause(ao); paused_locally = 1; }
            usleep(20000);
            continue;
        }

        if (paused_locally) { out123_continue(ao); paused_locally = 0; }

        size_t done = 0;
        int r = mpg123_read(mh, buffer, outblock, &done);
        if (r == MPG123_DONE) break;
        if (r != MPG123_OK && r != MPG123_NEW_FORMAT) break;

        if (r == MPG123_NEW_FORMAT) {
            if (mpg123_getformat(mh, &rate, &channels, &encoding) != MPG123_OK) break;
            out123_stop(ao);
            if (out123_start(ao, rate, channels, encoding) != 0) break;
            continue;
        }

        if (done > 0) {
            out123_play(ao, buffer, done);
            pthread_mutex_lock(&mp3_lock);
            if (out123_encsize(encoding) == 2) {
                update_visualizer_from_pcm16((const int16_t *)buffer, done / sizeof(int16_t), channels);
            }
            pthread_mutex_unlock(&mp3_lock);
        }
    }

cleanup:
    if (ao) { out123_drop(ao); out123_close(ao); out123_del(ao); }
    if (mh) { mpg123_close(mh); mpg123_delete(mh); }
    free(buffer);

    pthread_mutex_lock(&mp3_lock);
    thread_running = 0;
    if (!stop_requested) {
        state = MP3_STOPPED;
        start_time = 0;
        pause_offset = 0;
        finished_naturally = 1;
        clear_visualizer();
    }
    pthread_mutex_unlock(&mp3_lock);

    free(args->path);
    free(args);
    return NULL;
}

/* ── Public API ──────────────────────────────────────────────────────── */

int mp3_service_init(const char *audio_root) {
    if (!audio_root || audio_root[0] == '\0') return -1;
    snprintf(g_audio_root, sizeof(g_audio_root), "%s", audio_root);

    categories = NULL;
    category_count = 0;
    category_capacity = 0;

    struct stat st = {0};
    if (stat(audio_root, &st) == -1) {
        if (mkdir(audio_root, 0755) != 0) return -1;
    } else if (!S_ISDIR(st.st_mode)) {
        return -1;
    }

    DIR *root_dir = opendir(audio_root);
    if (!root_dir) return -1;

    struct dirent *entry;
    while ((entry = readdir(root_dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;

        char cat_path[1024];
        snprintf(cat_path, sizeof(cat_path), "%s/%s", audio_root, entry->d_name);

        struct stat cat_st;
        if (stat(cat_path, &cat_st) != 0 || !S_ISDIR(cat_st.st_mode)) continue;

        if (ensure_category_capacity() != 0) continue;

        Mp3Category *cat = &categories[category_count];
        memset(cat, 0, sizeof(Mp3Category));
        cat->name = strdup(entry->d_name);
        if (!cat->name) continue;

        scan_category_dir(cat, cat_path);

        /* Only add if it has content */
        if (cat->collection_count > 0 || cat->direct_track_count > 0) {
            category_count++;
        } else {
            free(cat->name);
        }
    }
    closedir(root_dir);
    return 0;
}

int mp3_service_rescan(const char *audio_root) {
    const char *root = audio_root;
    if (!root || root[0] == '\0') root = g_audio_root;
    if (!root || root[0] == '\0') return -1;

    if (mp3_service_get_state() != MP3_STOPPED) {
        return 0;
    }

    mp3_service_shutdown();
    return mp3_service_init(root);
}

size_t mp3_service_category_count(void) {
    return category_count;
}

const Mp3Category *mp3_service_get_category(size_t index) {
    if (index >= category_count) return NULL;
    return &categories[index];
}

size_t mp3_service_collection_count(size_t category_index) {
    if (category_index >= category_count) return 0;
    return categories[category_index].collection_count;
}

const Mp3Collection *mp3_service_get_collection(size_t category_index, size_t collection_index) {
    if (category_index >= category_count) return NULL;
    Mp3Category *cat = &categories[category_index];
    if (collection_index >= cat->collection_count) return NULL;
    return &cat->collections[collection_index];
}

size_t mp3_service_track_count(size_t category_index, size_t collection_index) {
    const Mp3Collection *col = mp3_service_get_collection(category_index, collection_index);
    if (!col) return 0;
    return col->track_count;
}

const Mp3Track *mp3_service_get_track(size_t category_index, size_t collection_index, size_t track_index) {
    const Mp3Collection *col = mp3_service_get_collection(category_index, collection_index);
    if (!col || track_index >= col->track_count) return NULL;
    return &col->tracks[track_index];
}

size_t mp3_service_direct_track_count(size_t category_index) {
    if (category_index >= category_count) return 0;
    return categories[category_index].direct_track_count;
}

const Mp3Track *mp3_service_get_direct_track(size_t category_index, size_t track_index) {
    if (category_index >= category_count) return NULL;
    Mp3Category *cat = &categories[category_index];
    if (track_index >= cat->direct_track_count) return NULL;
    return &cat->direct_tracks[track_index];
}

/* Get the path for the track to play */
static const char *get_current_track_path(void) {
    if (cur_is_direct) {
        const Mp3Track *t = mp3_service_get_direct_track(cur_cat, cur_track);
        return t ? t->path : NULL;
    } else {
        const Mp3Track *t = mp3_service_get_track(cur_cat, cur_col, cur_track);
        return t ? t->path : NULL;
    }
}

static size_t get_current_queue_size(void) {
    if (cur_is_direct) return mp3_service_direct_track_count(cur_cat);
    return mp3_service_track_count(cur_cat, cur_col);
}

static void stop_thread(void) {
    pthread_t join_thread;
    int should_join = 0;

    pthread_mutex_lock(&mp3_lock);
    if (thread_running) {
        stop_requested = 1;
        join_thread = player_thread;
        should_join = 1;
    }
    state = MP3_STOPPED;
    start_time = 0;
    pause_offset = 0;
    total_duration = 0;
    finished_naturally = 0;
    clear_visualizer();
    pthread_mutex_unlock(&mp3_lock);

    if (should_join) {
        pthread_join(join_thread, NULL);
        pthread_mutex_lock(&mp3_lock);
        stop_requested = 0;
        thread_running = 0;
        pthread_mutex_unlock(&mp3_lock);
    }
}

static int start_playback_thread(const char *path) {
    player_args_t *args = malloc(sizeof(player_args_t));
    if (!args) return -1;
    args->path = strdup(path);
    if (!args->path) { free(args); return -1; }

    pthread_mutex_lock(&mp3_lock);
    state = MP3_PLAYING;
    start_time = time(NULL);
    pause_offset = 0;
    total_duration = duration_from_mp3(path);
    stop_requested = 0;
    finished_naturally = 0;
    clear_visualizer();
    thread_running = 1;
    pthread_mutex_unlock(&mp3_lock);

    if (pthread_create(&player_thread, NULL, player_thread_fn, args) != 0) {
        pthread_mutex_lock(&mp3_lock);
        thread_running = 0;
        state = MP3_STOPPED;
        pthread_mutex_unlock(&mp3_lock);
        free(args->path);
        free(args);
        return -1;
    }
    return 0;
}

int mp3_service_play(size_t cat_idx, size_t col_idx, size_t track_idx, int is_direct) {
    stop_thread();

    const char *path = NULL;
    if (is_direct) {
        const Mp3Track *t = mp3_service_get_direct_track(cat_idx, track_idx);
        if (!t) return -1;
        path = t->path;
    } else {
        const Mp3Track *t = mp3_service_get_track(cat_idx, col_idx, track_idx);
        if (!t) return -1;
        path = t->path;
    }
    if (!path) return -1;

    cur_cat = cat_idx;
    cur_col = col_idx;
    cur_track = track_idx;
    cur_is_direct = is_direct;

    return start_playback_thread(path);
}

void mp3_service_pause(void) {
    pthread_mutex_lock(&mp3_lock);
    if (state == MP3_PLAYING) {
        pause_offset += (unsigned)(time(NULL) - start_time);
        state = MP3_PAUSED;
    }
    pthread_mutex_unlock(&mp3_lock);
}

void mp3_service_resume(void) {
    pthread_mutex_lock(&mp3_lock);
    if (state == MP3_PAUSED) {
        start_time = time(NULL);
        state = MP3_PLAYING;
    }
    pthread_mutex_unlock(&mp3_lock);
}

void mp3_service_stop(void) {
    stop_thread();
}

int mp3_service_next(void) {
    size_t queue_size = get_current_queue_size();
    if (cur_track + 1 >= queue_size) return -1; /* last track, do nothing */

    stop_thread();
    cur_track++;
    const char *path = get_current_track_path();
    if (!path) return -1;
    return start_playback_thread(path);
}

int mp3_service_prev(void) {
    if (cur_track == 0) {
        /* First track: restart it */
        stop_thread();
        const char *path = get_current_track_path();
        if (!path) return -1;
        return start_playback_thread(path);
    }

    stop_thread();
    cur_track--;
    const char *path = get_current_track_path();
    if (!path) return -1;
    return start_playback_thread(path);
}

mp3_playback_state mp3_service_get_state(void) {
    pthread_mutex_lock(&mp3_lock);
    mp3_playback_state s = state;
    pthread_mutex_unlock(&mp3_lock);
    return s;
}

int mp3_service_get_current_index(void) {
    pthread_mutex_lock(&mp3_lock);
    int idx = (state != MP3_STOPPED) ? (int)cur_track : -1;
    pthread_mutex_unlock(&mp3_lock);
    return idx;
}

unsigned mp3_service_get_elapsed(void) {
    pthread_mutex_lock(&mp3_lock);
    unsigned elapsed = 0;
    if (state == MP3_PLAYING) {
        elapsed = pause_offset + (unsigned)(time(NULL) - start_time);
    } else if (state == MP3_PAUSED) {
        elapsed = pause_offset;
    }
    pthread_mutex_unlock(&mp3_lock);
    return elapsed;
}

unsigned mp3_service_get_total_duration(void) {
    pthread_mutex_lock(&mp3_lock);
    unsigned out = total_duration;
    pthread_mutex_unlock(&mp3_lock);
    return out;
}

void mp3_service_update(void) {
    int should_advance = 0;
    pthread_mutex_lock(&mp3_lock);
    if (finished_naturally) {
        finished_naturally = 0;
        should_advance = 1;
    }
    pthread_mutex_unlock(&mp3_lock);

    if (should_advance) {
        if (mp3_service_next() != 0) {
            mp3_service_stop();
        }
    }
}

size_t mp3_service_get_visualizer(unsigned char *out_levels, size_t max_levels) {
    if (!out_levels || max_levels == 0) return 0;
    size_t count = (max_levels < MP3_VIZ_BINS) ? max_levels : MP3_VIZ_BINS;
    pthread_mutex_lock(&mp3_lock);
    for (size_t i = 0; i < count; i++) {
        float v = viz_levels[i];
        if (v < 0.0f) v = 0.0f;
        if (v > 1.0f) v = 1.0f;
        out_levels[i] = (unsigned char)(v * 8.0f);
    }
    pthread_mutex_unlock(&mp3_lock);
    return count;
}

const char *mp3_service_current_track_name(void) {
    if (cur_is_direct) {
        const Mp3Track *t = mp3_service_get_direct_track(cur_cat, cur_track);
        return t ? t->title : "Unknown";
    }
    const Mp3Track *t = mp3_service_get_track(cur_cat, cur_col, cur_track);
    return t ? t->title : "Unknown";
}

const char *mp3_service_current_collection_name(void) {
    if (cur_is_direct) return "";
    const Mp3Collection *c = mp3_service_get_collection(cur_cat, cur_col);
    return c ? c->name : "Unknown";
}

const char *mp3_service_current_category_name(void) {
    const Mp3Category *c = mp3_service_get_category(cur_cat);
    return c ? c->name : "Unknown";
}

int mp3_service_get_volume(void) {
    return volume_percent;
}

void mp3_service_set_volume(int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    volume_percent = percent;
    /* TODO: apply volume to out123 when supported */
}

/* Backward compat: total flat track count */
size_t mp3_service_count(void) {
    size_t total = 0;
    for (size_t c = 0; c < category_count; c++) {
        total += categories[c].direct_track_count;
        for (size_t col = 0; col < categories[c].collection_count; col++) {
            total += categories[c].collections[col].track_count;
        }
    }
    return total;
}

void mp3_service_shutdown(void) {
    stop_thread();

    if (!categories) return;
    for (size_t c = 0; c < category_count; c++) {
        Mp3Category *cat = &categories[c];

        /* Free direct tracks */
        for (size_t t = 0; t < cat->direct_track_count; t++) {
            free(cat->direct_tracks[t].path);
            free(cat->direct_tracks[t].title);
        }
        free(cat->direct_tracks);

        /* Free collections */
        for (size_t col = 0; col < cat->collection_count; col++) {
            Mp3Collection *collection = &cat->collections[col];
            for (size_t t = 0; t < collection->track_count; t++) {
                free(collection->tracks[t].path);
                free(collection->tracks[t].title);
            }
            free(collection->tracks);
            free(collection->name);
        }
        free(cat->collections);
        free(cat->name);
    }
    free(categories);
    categories = NULL;
    category_count = 0;
    category_capacity = 0;
    total_duration = 0;
    finished_naturally = 0;
}
