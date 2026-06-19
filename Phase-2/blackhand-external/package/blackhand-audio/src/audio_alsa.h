#ifndef AUDIO_ALSA_H
#define AUDIO_ALSA_H

/*
 * audio_alsa.h — daemon-owned audio engine.
 *
 * blackhand-audio is the sole owner of ALSA, the recording PID, the
 * playback thread, and the elapsed clock. The UI process never opens
 * a PCM device, never forks arecord/aplay/mpg123, and never tracks
 * playback position with its own timer — it just sends commands and
 * reads back what the daemon reports.
 *
 * Pause is REAL pause: the playback thread waits on a condvar, the
 * capture thread waits on a condvar. No restart-from-zero.
 *
 * Routing is decided at play_start / record_start by querying bluez
 * for a connected A2DP sink / HFP headset. The UI does not pass MACs.
 */

int audio_init(void);

/* ── playback ── */

/* Start playing a WAV file. Decides BT vs USB routing at call time.
   Stops any current playback first. Returns 0 on success, -1 on error. */
int  audio_play_start(const char *path);

/* SIGSTOP-style pause: thread parks on a condvar, PCM buffer is dropped
   so audio stops immediately (not after the buffer drains). Returns 0
   on success, -1 if nothing is playing. */
int  audio_play_pause(void);

/* Resume from the byte offset where pause held. PCM is re-prepared
   so frames flow again. Returns 0 on success, -1 if not paused. */
int  audio_play_resume(void);

/* Stop playback entirely. Joins the thread, closes the PCM. */
int  audio_play_stop(void);

/* Seek to absolute position in the current track. Honored by the WAV
   thread on its next iteration; for MP3 paths this returns -1 (not
   supported). Clamped to [0, duration]. */
int  audio_play_seek(int position_ms);

/* Status accessors — UI polls these via the status verb. */
int  audio_is_playing(void);
int  audio_is_paused(void);
int  audio_play_elapsed_ms(void);

/* ── recording ── */

/* Start recording. Decides BT (SCO) vs USB mic routing at call time.
   Returns 0 on success, -1 if already recording or capture device
   unavailable. */
int  audio_record_start(const char *path);

/* Real pause for recording — capture thread parks on a condvar, no
   bytes hit the file while paused. */
int  audio_record_pause(void);
int  audio_record_resume(void);

/* Stop recording and finalise the WAV header. */
int  audio_record_stop(void);

int  audio_is_recording(void);

/* ── volume ── */
void audio_set_volume(int percent);
int  audio_get_volume(void);

#endif
