#ifndef MP3_SERVICE_H
#define MP3_SERVICE_H
#include <stddef.h>

/*
 * mp3_service.h
 *
 * Hierarchical music library service.
 *
 * Structure: ./Music/<category>/<collection>/<track.mp3>
 *
 * - Categories are top-level folders (genres, moods, etc.)
 * - Collections are folders inside categories (artists, albums, etc.)
 * - Tracks are .mp3 files inside collections
 * - If a category folder contains .mp3 files directly, they appear as
 *   tracks without needing a collection layer
 *
 * Limits:
 *   max categories: 50
 *   max collections per category: 200
 *   max tracks per collection: 1000
 */

#define MP3_MAX_CATEGORIES       50
#define MP3_MAX_COLLECTIONS     200
#define MP3_MAX_TRACKS         1000
#define MP3_VIZ_BINS             20

typedef struct {
	char *path;      /* Full file path */
	char *title;     /* Display name (filename without .mp3, _ replaced with spaces) */
	unsigned duration; /* Duration in seconds (0 if unknown) */
} Mp3Track;

typedef struct {
	char *name;       /* Collection folder name */
	Mp3Track *tracks;
	size_t track_count;
	size_t track_capacity;
} Mp3Collection;

typedef struct {
	char *name;               /* Category folder name */
	Mp3Collection *collections;
	size_t collection_count;
	size_t collection_capacity;
	/* Direct tracks in category (no collection subfolder) */
	Mp3Track *direct_tracks;
	size_t direct_track_count;
	size_t direct_track_capacity;
} Mp3Category;

typedef enum {
	MP3_STOPPED,
	MP3_PLAYING,
	MP3_PAUSED
} mp3_playback_state;

/* Initialization and shutdown */
int  mp3_service_init(const char *audio_root);
void mp3_service_shutdown(void);
int  mp3_service_rescan(const char *audio_root);

/* Library queries */
size_t              mp3_service_category_count(void);
const Mp3Category  *mp3_service_get_category(size_t index);
size_t              mp3_service_collection_count(size_t category_index);
const Mp3Collection *mp3_service_get_collection(size_t category_index, size_t collection_index);
size_t              mp3_service_track_count(size_t category_index, size_t collection_index);
const Mp3Track     *mp3_service_get_track(size_t category_index, size_t collection_index, size_t track_index);

/* Direct tracks in a category (no collection) */
size_t              mp3_service_direct_track_count(size_t category_index);
const Mp3Track     *mp3_service_get_direct_track(size_t category_index, size_t track_index);

/* Playback control */
int  mp3_service_play(size_t cat_idx, size_t col_idx, size_t track_idx, int is_direct);
void mp3_service_pause(void);
void mp3_service_resume(void);
void mp3_service_stop(void);
int  mp3_service_next(void);
int  mp3_service_prev(void);

/* Playback state */
mp3_playback_state mp3_service_get_state(void);
int                mp3_service_get_current_index(void);  /* track index in current collection */
unsigned           mp3_service_get_elapsed(void);
unsigned           mp3_service_get_total_duration(void);
size_t             mp3_service_get_visualizer(unsigned char *out_levels, size_t max_levels);
void               mp3_service_update(void);

/* Current track info */
const char *mp3_service_current_track_name(void);
const char *mp3_service_current_collection_name(void);
const char *mp3_service_current_category_name(void);

/* Volume control */
int  mp3_service_get_volume(void);
void mp3_service_set_volume(int percent); /* 0-100 */

/* Backward compatibility - flat track count */
size_t mp3_service_count(void);

#endif
