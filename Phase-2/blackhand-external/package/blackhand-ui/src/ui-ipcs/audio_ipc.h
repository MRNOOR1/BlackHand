#ifndef AUDIO_IPC_H
#define AUDIO_IPC_H

/*
 * audio_ipc.h
 *
 * Client-side IPC wrapper for talking to blackhand-audio.
 *
 * This file does NOT play audio directly.
 * It sends JSON-RPC commands to the audio process over IPC.
 */

/*
 * Checks whether the audio service is alive.
 *
 * Sends:
 *   {"jsonrpc":"2.0","method":"ping","id":1}
 *
 * Expects:
 *   {"jsonrpc":"2.0","result":"pong","id":1}
 *
 * Returns:
 *   0  on success
 *  -1  on failure
 */
int audio_ipc_ping(void);

/*
 * Ask audio service to play an audio file.
 *
 * filepath example:
 *   /data/music/song.mp3
 *
 * Sends:
 *   {"jsonrpc":"2.0","method":"play","params":{"file":"..."},"id":1}
 *
 * Returns:
 *   0  on success
 *  -1  on failure
 */
int audio_ipc_play(const char *filepath);

/*
 * Ask audio service to pause current playback.
 *
 * Sends:
 *   {"jsonrpc":"2.0","method":"pause","id":1}
 *
 * Returns:
 *   0  on success
 *  -1  on failure
 */
int audio_ipc_pause(void);
int audio_ipc_stop(void);

/*
 * Ask audio service to set volume.
 *
 * percent must be between 0 and 100.
 *
 * Sends:
 *   {"jsonrpc":"2.0","method":"volume","params":{"volume":80},"id":1}
 *
 * Returns:
 *   0  on success
 *  -1  on failure
 */
int audio_ipc_volume(int percent);

/*
 * Ask audio service for current status.
 *
 * Sends:
 *   {"jsonrpc":"2.0","method":"status","id":1}
 *
 * Expects:
 *   {"jsonrpc":"2.0","result":{"playing":true,"volume":80},"id":1}
 *
 * Writes:
 *   *out_playing = 1 if playing, 0 if not playing
 *   *out_volume  = current volume percentage
 *
 * Returns:
 *   0  on success
 *  -1  on failure
 */
int audio_ipc_status(int *out_playing, int *out_volume);

/*
 * Convenience: return the current system volume (0–100).
 *
 * This is the single canonical read path for volume.  All UI controls,
 * headphone buttons, and future BT controls should read volume through
 * this function and write through audio_ipc_volume().
 *
 * blackhand-audio is the sole owner of volume state.  Never cache it
 * locally — always call this before computing a delta (vol +/- step).
 *
 * Returns:
 *   0–100  current volume
 *  -1      on IPC failure (caller should keep last known value)
 */
int audio_ipc_get_volume(void);

#endif