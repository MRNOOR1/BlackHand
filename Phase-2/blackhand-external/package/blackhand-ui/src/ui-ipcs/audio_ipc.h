#ifndef AUDIO_IPC_H
#define AUDIO_IPC_H

/*
 * audio_ipc.h — client-side wrappers for blackhand-audio.
 *
 * The UI never touches ALSA or forks audio subprocesses. Every audio
 * operation goes through this header. The daemon owns routing,
 * playback position, pause state, and the recording PID.
 */

int audio_ipc_ping(void);

/* ── playback ─────────────────────────────────────────────────────────────── */

/* Start playing a file. Daemon picks the output (BT A2DP if connected,
   else USB card) automatically. Returns 0 on success. */
int audio_ipc_play_start(const char *filepath);

/* Real pause — daemon parks the playback thread, drops the ALSA buffer
   so audio stops immediately. Resume continues from the same byte offset. */
int audio_ipc_play_pause(void);
int audio_ipc_play_resume(void);
int audio_ipc_play_stop(void);

/* Seek to an absolute position within the current file. */
int audio_ipc_play_seek(int position_ms);

/* ── recording ────────────────────────────────────────────────────────────── */

/* Start recording to a file. UI picks the filename, daemon picks the
   capture device (BT SCO if a headset is connected, else USB mic). */
int audio_ipc_record_start(const char *filepath);
int audio_ipc_record_pause(void);
int audio_ipc_record_resume(void);
int audio_ipc_record_stop(void);

/* ── volume ───────────────────────────────────────────────────────────────── */

int audio_ipc_volume(int percent);

/* Convenience: returns 0–100, or -1 on IPC failure. */
int audio_ipc_get_volume(void);

/* ── status ───────────────────────────────────────────────────────────────── */

/* Full status snapshot. Any out_* pointer may be NULL. Returns 0 on success. */
int audio_ipc_status_full(int *out_playing,
                          int *out_paused,
                          int *out_recording,
                          int *out_elapsed_ms,
                          int *out_volume);

/* Legacy shim retained for the few call sites that only want playing+volume. */
int audio_ipc_status(int *out_playing, int *out_volume);

#endif
