#ifndef AUDIO_ALSA_H
#define AUDIO_ALSA_H

/* Initialises the mixer. Must be called once at startup.
   Returns 0 on success, -1 on error. */
int audio_init(void);

/* Starts playing a file (WAV or MP3) via a background subprocess.
   Stops any current playback first. Returns immediately. */
void audio_play(const char *path);

/* Stops current playback. */
void audio_stop(void);

/* Sets master playback volume (0 = mute, 100 = max). */
void audio_set_volume(int percent);

/* Returns current volume (0–100). */
int audio_get_volume(void);

/* Returns 1 if playback is active. */
int audio_is_playing(void);

/* Starts recording from the USB mic to a WAV file (48000Hz, mono, S16_LE).
   Returns 0 on success, -1 if already recording or device unavailable. */
int audio_record_start(const char *path);

/* Stops recording, flushes, and finalises the WAV file. */
void audio_record_stop(void);

/* Returns 1 if recording is active. */
int audio_is_recording(void);

#endif
