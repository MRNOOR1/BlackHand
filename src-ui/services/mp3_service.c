#include "mp3_service.h"
#include "audio_ipc.h"

#include <dirent.h>
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
static char g_audio_root[512] = {0};

/* ── Playback state (tracked locally — audio runs in blackhand-audio) ── */
static mp3_playback_state state = MP3_STOPPED;
static size_t cur_cat = 0;
static size_t cur_col = 0;
static size_t cur_track = 0;
static int cur_is_direct = 0;
static time_t start_time = 0;
static unsigned pause_offset = 0;
static int volume_percent = 80;

/* ── Helper: title from filename ─────────────────────────────────────── */
static char *title_from_filename(const char *filename)
{
    char *copy = strdup(filename);
    if (!copy)
        return strdup("Unknown");
    char *dot = strrchr(copy, '.');
    if (dot)
        *dot = '\0';
    for (char *p = copy; *p; p++)
    {
        if (*p == '_')
            *p = ' ';
    }
    return copy;
}

/* ── Library helpers ─────────────────────────────────────────────────── */
static int ensure_category_capacity(void)
{
    if (category_count < category_capacity)
        return 0;
    size_t new_cap = (category_capacity == 0) ? 8 : category_capacity * 2;
    if (new_cap > MP3_MAX_CATEGORIES)
        new_cap = MP3_MAX_CATEGORIES;
    Mp3Category *newp = realloc(categories, new_cap * sizeof(Mp3Category));
    if (!newp)
        return -1;
    categories = newp;
    category_capacity = new_cap;
    return 0;
}

static int ensure_collection_capacity(Mp3Category *cat)
{
    if (cat->collection_count < cat->collection_capacity)
        return 0;
    size_t new_cap = (cat->collection_capacity == 0) ? 8 : cat->collection_capacity * 2;
    if (new_cap > MP3_MAX_COLLECTIONS)
        new_cap = MP3_MAX_COLLECTIONS;
    Mp3Collection *newp = realloc(cat->collections, new_cap * sizeof(Mp3Collection));
    if (!newp)
        return -1;
    cat->collections = newp;
    cat->collection_capacity = new_cap;
    return 0;
}

static int ensure_track_capacity(Mp3Track **tracks, size_t *count, size_t *capacity)
{
    if (*count < *capacity)
        return 0;
    size_t new_cap = (*capacity == 0) ? 16 : *capacity * 2;
    if (new_cap > MP3_MAX_TRACKS)
        new_cap = MP3_MAX_TRACKS;
    Mp3Track *newp = realloc(*tracks, new_cap * sizeof(Mp3Track));
    if (!newp)
        return -1;
    *tracks = newp;
    *capacity = new_cap;
    return 0;
}

static void add_track(Mp3Track **tracks, size_t *count, size_t *capacity,
                      const char *path, const char *filename)
{
    if (ensure_track_capacity(tracks, count, capacity) != 0)
        return;
    Mp3Track *t = &(*tracks)[*count];
    t->path = strdup(path);
    t->title = title_from_filename(filename);
    t->duration = 0;
    if (!t->path || !t->title)
    {
        free(t->path);
        free(t->title);
        return;
    }
    (*count)++;
}

/* ── Scan a collection folder for .mp3 files ─────────────────────────── */
static void scan_collection_dir(Mp3Collection *col, const char *col_path)
{
    DIR *dir = opendir(col_path);
    if (!dir)
        return;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
    {
        if (entry->d_name[0] == '.')
            continue;
        const char *ext = strrchr(entry->d_name, '.');
        if (!ext || strcmp(ext, ".mp3") != 0)
            continue;

        char full_path[1024];
        snprintf(full_path, sizeof(full_path), "%s/%s", col_path, entry->d_name);

        struct stat st;
        if (stat(full_path, &st) != 0 || !S_ISREG(st.st_mode))
            continue;

        add_track(&col->tracks, &col->track_count, &col->track_capacity,
                  full_path, entry->d_name);
    }
    closedir(dir);
}

/* ── Scan a category folder ──────────────────────────────────────────── */
static void scan_category_dir(Mp3Category *cat, const char *cat_path)
{
    DIR *dir = opendir(cat_path);
    if (!dir)
        return;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
    {
        if (entry->d_name[0] == '.')
            continue;

        char full_path[1024];
        snprintf(full_path, sizeof(full_path), "%s/%s", cat_path, entry->d_name);

        struct stat st;
        if (stat(full_path, &st) != 0)
            continue;

        if (S_ISDIR(st.st_mode))
        {
            if (ensure_collection_capacity(cat) != 0)
                continue;
            Mp3Collection *col = &cat->collections[cat->collection_count];
            memset(col, 0, sizeof(Mp3Collection));
            col->name = strdup(entry->d_name);
            if (!col->name)
                continue;

            scan_collection_dir(col, full_path);

            if (col->track_count > 0)
            {
                cat->collection_count++;
            }
            else
            {
                free(col->name);
                free(col->tracks);
            }
        }
        else if (S_ISREG(st.st_mode))
        {
            const char *ext = strrchr(entry->d_name, '.');
            if (ext && strcmp(ext, ".mp3") == 0)
            {
                add_track(&cat->direct_tracks, &cat->direct_track_count,
                          &cat->direct_track_capacity, full_path, entry->d_name);
            }
        }
    }
    closedir(dir);
}

/* ── Public API ──────────────────────────────────────────────────────── */

int mp3_service_init(const char *audio_root)
{
    if (!audio_root || audio_root[0] == '\0')
        return -1;
    snprintf(g_audio_root, sizeof(g_audio_root), "%s", audio_root);

    categories = NULL;
    category_count = 0;
    category_capacity = 0;

    struct stat st = {0};
    if (stat(audio_root, &st) == -1)
    {
        if (mkdir(audio_root, 0755) != 0)
            return -1;
    }
    else if (!S_ISDIR(st.st_mode))
    {
        return -1;
    }

    DIR *root_dir = opendir(audio_root);
    if (!root_dir)
        return -1;

    struct dirent *entry;
    while ((entry = readdir(root_dir)) != NULL)
    {
        if (entry->d_name[0] == '.')
            continue;

        char cat_path[1024];
        snprintf(cat_path, sizeof(cat_path), "%s/%s", audio_root, entry->d_name);

        struct stat cat_st;
        if (stat(cat_path, &cat_st) != 0 || !S_ISDIR(cat_st.st_mode))
            continue;

        if (ensure_category_capacity() != 0)
            continue;

        Mp3Category *cat = &categories[category_count];
        memset(cat, 0, sizeof(Mp3Category));
        cat->name = strdup(entry->d_name);
        if (!cat->name)
            continue;

        scan_category_dir(cat, cat_path);

        if (cat->collection_count > 0 || cat->direct_track_count > 0)
        {
            category_count++;
        }
        else
        {
            free(cat->name);
        }
    }
    closedir(root_dir);
    return 0;
}

int mp3_service_rescan(const char *audio_root)
{
    if (state != MP3_STOPPED)
        return 0;
    const char *root = (audio_root && audio_root[0]) ? audio_root : g_audio_root;
    if (!root || !root[0])
        return -1;
    mp3_service_shutdown();
    return mp3_service_init(root);
}

/* ── Library queries ─────────────────────────────────────────────────── */

size_t mp3_service_category_count(void) { return category_count; }

const Mp3Category *mp3_service_get_category(size_t index)
{
    if (index >= category_count)
        return NULL;
    return &categories[index];
}

size_t mp3_service_collection_count(size_t category_index)
{
    if (category_index >= category_count)
        return 0;
    return categories[category_index].collection_count;
}

const Mp3Collection *mp3_service_get_collection(size_t category_index, size_t collection_index)
{
    if (category_index >= category_count)
        return NULL;
    Mp3Category *cat = &categories[category_index];
    if (collection_index >= cat->collection_count)
        return NULL;
    return &cat->collections[collection_index];
}

size_t mp3_service_track_count(size_t category_index, size_t collection_index)
{
    const Mp3Collection *col = mp3_service_get_collection(category_index, collection_index);
    if (!col)
        return 0;
    return col->track_count;
}

const Mp3Track *mp3_service_get_track(size_t category_index, size_t collection_index, size_t track_index)
{
    const Mp3Collection *col = mp3_service_get_collection(category_index, collection_index);
    if (!col || track_index >= col->track_count)
        return NULL;
    return &col->tracks[track_index];
}

size_t mp3_service_direct_track_count(size_t category_index)
{
    if (category_index >= category_count)
        return 0;
    return categories[category_index].direct_track_count;
}

const Mp3Track *mp3_service_get_direct_track(size_t category_index, size_t track_index)
{
    if (category_index >= category_count)
        return NULL;
    Mp3Category *cat = &categories[category_index];
    if (track_index >= cat->direct_track_count)
        return NULL;
    return &cat->direct_tracks[track_index];
}

/* ── Internal helpers ────────────────────────────────────────────────── */

static const char *get_current_track_path(void)
{
    if (cur_is_direct)
    {
        const Mp3Track *t = mp3_service_get_direct_track(cur_cat, cur_track);
        return t ? t->path : NULL;
    }
    const Mp3Track *t = mp3_service_get_track(cur_cat, cur_col, cur_track);
    return t ? t->path : NULL;
}

static size_t get_current_queue_size(void)
{
    if (cur_is_direct)
        return mp3_service_direct_track_count(cur_cat);
    return mp3_service_track_count(cur_cat, cur_col);
}

/* ── Playback control ────────────────────────────────────────────────── */

int mp3_service_play(size_t cat_idx, size_t col_idx, size_t track_idx, int is_direct)
{
    const char *path = NULL;
    if (is_direct)
    {
        const Mp3Track *t = mp3_service_get_direct_track(cat_idx, track_idx);
        if (!t)
            return -1;
        path = t->path;
    }
    else
    {
        const Mp3Track *t = mp3_service_get_track(cat_idx, col_idx, track_idx);
        if (!t)
            return -1;
        path = t->path;
    }
    if (!path)
        return -1;

    if (audio_ipc_play(path) != 0)
        return -1;

    cur_cat = cat_idx;
    cur_col = col_idx;
    cur_track = track_idx;
    cur_is_direct = is_direct;
    state = MP3_PLAYING;
    start_time = time(NULL);
    pause_offset = 0;
    return 0;
}

void mp3_service_pause(void)
{
    if (state == MP3_PLAYING)
    {
        audio_ipc_pause();
        pause_offset += (unsigned)(time(NULL) - start_time);
        state = MP3_PAUSED;
    }
}

void mp3_service_resume(void)
{
    if (state == MP3_PAUSED)
    {
        const char *path = get_current_track_path();
        if (path && audio_ipc_play(path) == 0)
        {
            start_time = time(NULL);
            state = MP3_PLAYING;
        }
    }
}

void mp3_service_stop(void)
{
    if (state != MP3_STOPPED)
    {
        audio_ipc_stop();
        state = MP3_STOPPED;
        start_time = 0;
        pause_offset = 0;
    }
}

int mp3_service_next(void)
{
    size_t queue_size = get_current_queue_size();
    if (cur_track + 1 >= queue_size)
        return -1;

    cur_track++;
    const char *path = get_current_track_path();
    if (!path)
        return -1;
    if (audio_ipc_play(path) != 0)
        return -1;

    state = MP3_PLAYING;
    start_time = time(NULL);
    pause_offset = 0;
    return 0;
}

int mp3_service_prev(void)
{
    if (cur_track > 0)
        cur_track--;

    const char *path = get_current_track_path();
    if (!path)
        return -1;
    if (audio_ipc_play(path) != 0)
        return -1;

    state = MP3_PLAYING;
    start_time = time(NULL);
    pause_offset = 0;
    return 0;
}

/* ── Playback state ──────────────────────────────────────────────────── */

mp3_playback_state mp3_service_get_state(void) { return state; }

int mp3_service_get_current_index(void)
{
    return (state != MP3_STOPPED) ? (int)cur_track : -1;
}

unsigned mp3_service_get_elapsed(void)
{
    if (state == MP3_PLAYING)
        return pause_offset + (unsigned)(time(NULL) - start_time);
    if (state == MP3_PAUSED)
        return pause_offset;
    return 0;
}

unsigned mp3_service_get_total_duration(void)
{
    const Mp3Track *t = NULL;
    if (cur_is_direct)
        t = mp3_service_get_direct_track(cur_cat, cur_track);
    else
        t = mp3_service_get_track(cur_cat, cur_col, cur_track);
    return t ? t->duration : 0;
}

size_t mp3_service_get_visualizer(unsigned char *out_levels, size_t max_levels)
{
    if (!out_levels || max_levels == 0)
        return 0;
    size_t count = (max_levels < MP3_VIZ_BINS) ? max_levels : MP3_VIZ_BINS;
    for (size_t i = 0; i < count; i++)
        out_levels[i] = 0;
    return count;
}

void mp3_service_update(void)
{
    if (state != MP3_PLAYING)
        return;

    /* Poll audio service at most once per second to detect natural track end. */
    static time_t last_poll = 0;
    time_t now = time(NULL);
    if (now - last_poll < 1)
        return;
    last_poll = now;

    int playing = 0, vol = 0;
    if (audio_ipc_status(&playing, &vol) != 0)
        return;

    if (!playing)
    {
        size_t queue_size = get_current_queue_size();
        if (cur_track + 1 < queue_size)
        {
            mp3_service_next();
        }
        else
        {
            state = MP3_STOPPED;
            start_time = 0;
            pause_offset = 0;
        }
    }
}

/* ── Current track info ──────────────────────────────────────────────── */

const char *mp3_service_current_track_name(void)
{
    if (cur_is_direct)
    {
        const Mp3Track *t = mp3_service_get_direct_track(cur_cat, cur_track);
        return t ? t->title : "Unknown";
    }
    const Mp3Track *t = mp3_service_get_track(cur_cat, cur_col, cur_track);
    return t ? t->title : "Unknown";
}

const char *mp3_service_current_collection_name(void)
{
    if (cur_is_direct)
        return "";
    const Mp3Collection *c = mp3_service_get_collection(cur_cat, cur_col);
    return c ? c->name : "Unknown";
}

const char *mp3_service_current_category_name(void)
{
    const Mp3Category *c = mp3_service_get_category(cur_cat);
    return c ? c->name : "Unknown";
}

/* ── Volume ──────────────────────────────────────────────────────────── */

int mp3_service_get_volume(void) { return volume_percent; }

void mp3_service_set_volume(int percent)
{
    if (percent < 0)
        percent = 0;
    if (percent > 100)
        percent = 100;
    volume_percent = percent;
    audio_ipc_volume(percent);
}

/* ── Misc ────────────────────────────────────────────────────────────── */

size_t mp3_service_count(void)
{
    size_t total = 0;
    for (size_t c = 0; c < category_count; c++)
    {
        total += categories[c].direct_track_count;
        for (size_t col = 0; col < categories[c].collection_count; col++)
            total += categories[c].collections[col].track_count;
    }
    return total;
}

void mp3_service_shutdown(void)
{
    mp3_service_stop();

    if (!categories)
        return;
    for (size_t c = 0; c < category_count; c++)
    {
        Mp3Category *cat = &categories[c];

        for (size_t t = 0; t < cat->direct_track_count; t++)
        {
            free(cat->direct_tracks[t].path);
            free(cat->direct_tracks[t].title);
        }
        free(cat->direct_tracks);

        for (size_t col = 0; col < cat->collection_count; col++)
        {
            Mp3Collection *collection = &cat->collections[col];
            for (size_t t = 0; t < collection->track_count; t++)
            {
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
}
