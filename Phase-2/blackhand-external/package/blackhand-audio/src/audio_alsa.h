#ifndef AUDIO_ALSA_H
#define AUDIO_ALSA_H

/*
 * Initialises the ALSA PCM device. Must be called once at startup.
 * Returns 0 on success, -1 on error.
 */
int audio_init(void);

/*
 * Starts playing a WAV file in a background thread.
 * If another playback is already running, it is stopped first.
 * The function returns immediately; playback happens asynchronously.
 */
void audio_play(const char *path);

/*
 * Stops any ongoing playback and drains the PCM device.
 */
void audio_stop(void);

/*
 * Sets the master playback volume (0 = mute, 100 = maximum).
 */
void audio_set_volume(int percent);

/*
 * Returns the current volume level (0–100).
 */
int audio_get_volume(void);

/*
 * Returns 1 if playback is currently active, otherwise 0.
 */
 
int audio_is_playing(void);

#endif