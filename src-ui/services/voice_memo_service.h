#ifndef VOICE_MEMO_SERVICE_H
#define VOICE_MEMO_SERVICE_H

#include <stddef.h>

/*
 * voice_memo_service.h
 *
 * This service owns voice memo domain logic.
 * Filesystem is the source of truth.
 * Each memo is identified by its filename.
 *
 * Mock stage:
 * - No real audio recording yet
 * - Recording/playback are simulated using timers
 * - One file per memo in ./VoiceMemos
 */

typedef enum
{
	VM_IDLE,
	VM_RECORDING,
	VM_PLAYING,
	VM_PAUSED
} VMState;

typedef struct
{
	char *filename;
	int duration_ms;
} VoiceMemo;

void voice_memo_service_init(void);

VMState voice_memo_service_state(void);
const VoiceMemo *voice_memo_service_current(void);

const VoiceMemo **voice_memo_service_list_all(size_t *out_count);
const VoiceMemo *voice_memo_service_get_by_filename(const char *filename);

int voice_memo_service_record_start(void);
int voice_memo_service_record_stop(const char *title_optional);

int voice_memo_service_play_start(const char *filename);
int voice_memo_service_play_pause(void);
int voice_memo_service_play_resume(void);
int voice_memo_service_play_stop(void);

int voice_memo_service_delete(const char *filename);
int voice_memo_service_rename(const char *old_filename, const char *new_title);

int voice_memo_service_tick(void);
int voice_memo_service_elapsed_ms(void);
int voice_memo_service_total_ms(void);

void voice_memo_service_shutdown(void);

#endif
