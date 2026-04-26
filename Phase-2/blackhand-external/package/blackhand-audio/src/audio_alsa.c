#include "audio_alsa.h"
#include <alsa/asoundlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

/* ALSA handles */
static snd_pcm_t *pcm_handle = NULL;
static snd_mixer_t *mixer_handle = NULL;
static snd_mixer_elem_t *master_elem = NULL;

/* Playback state */
static pthread_t play_thread_id;
static int playback_active = 0;      /* 1 if a thread is playing */
static int stop_requested = 0;        /* set by audio_stop() to interrupt playback */
static pthread_mutex_t state_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Volume state */
static int current_volume = 50;       /* default 50% */

/* PCM parameters */
#define SAMPLE_RATE 44100
#define CHANNELS 2
#define FORMAT SND_PCM_FORMAT_S16_LE
#define PERIOD_FRAMES 1024

/* Helper: convert percent to the correct value in a given range (also called mixer linear value) 
i.e what is the 75% value in the range from 10 to 85 */
static long percent_to_mixer_value(int percent, long min, long max) {
    return min + (percent * (max - min)) / 100;
}

/* Helper: update mixer volume from current_volume */
/* In other words, Set the hardware specific value that correspond to the master element.*/
static void apply_volume(void) {
    if (!master_elem) return; /* if master element doesn't exist terminate*/
    long min, max;
    snd_mixer_selem_get_playback_volume_range(master_elem, &min, &max); /* retrieves the hardware’s min and max volume values */
    long value = percent_to_mixer_value(current_volume, min, max); /* Compute hardware specific value*/
    snd_mixer_selem_set_playback_volume_all(master_elem, value); /* set the volume */
}

/* Thread function: plays a WAV file */
static void *playback_thread(void *arg) { /* arg is file path, */
    const char *path = (const char *)arg;
    FILE *wav = fopen(path, "rb"); /* open file in binary mode*/
    /* If the file fails to open, then*/
    if (!wav) {
        fprintf(stderr, "audio_play: cannot open %s\n", path);
        pthread_mutex_lock(&state_mutex);
        playback_active = 0;
        pthread_mutex_unlock(&state_mutex);
        return NULL;
    }

    /* FYI: WAV file contains different parts: RIFF chunk, then fmt subchunk */



    /* ----- Simple WAV parser - Variables to store fields from the WAV RIFF chunks ----- */
    char chunk_id[4];
    unsigned int chunk_size;
    short audio_format, num_channels;
    unsigned int sample_rate, byte_rate;
    short block_align, bits_per_sample;
    char data_id[4];
    unsigned int data_size;

    /* Read RIFF header - Reads the first 12 bytes*/
    fread(chunk_id, 1, 4, wav);
    fread(&chunk_size, 4, 1, wav);
    char format[4];
    fread(format, 1, 4, wav);

    /* Compares the identifiers; if not "RIFF" or not "WAVE", return NULL*/
    if (memcmp(chunk_id, "RIFF", 4) != 0 || memcmp(format, "WAVE", 4) != 0) {
        fprintf(stderr, "Not a valid WAV file\n");
        fclose(wav);
        pthread_mutex_lock(&state_mutex);
        playback_active = 0;
        pthread_mutex_unlock(&state_mutex);
        return NULL;
    }

    /* Read fmt chunk */
    fread(chunk_id, 1, 4, wav);
    fread(&chunk_size, 4, 1, wav);
    fread(&audio_format, 2, 1, wav); /* 1 for PCM (uncompressed)*/
    fread(&num_channels, 2, 1, wav); /* 1 (mono) or 2(stereo)*/
    fread(&sample_rate, 4, 1, wav); /* in Hz*/
    fread(&byte_rate, 4, 1, wav); /* sample_rate * num_channels * bits_per_sample/8. */
    fread(&block_align, 2, 1, wav); /* num_channels * bits_per_sample/8. */
    fread(&bits_per_sample, 2, 1, wav);

    /* Skip extra fmt bytes if any */
    if (chunk_size > 16) fseek(wav, chunk_size - 16, SEEK_CUR);

    /* Find data chunk */
    while (1) {
        fread(data_id, 1, 4, wav);
        fread(&data_size, 4, 1, wav);
        if (memcmp(data_id, "data", 4) == 0) break;
        fseek(wav, data_size, SEEK_CUR);
        if (feof(wav)) {
            fprintf(stderr, "No data chunk found\n");
            fclose(wav);
            pthread_mutex_lock(&state_mutex);
            playback_active = 0;
            pthread_mutex_unlock(&state_mutex);
            return NULL;
        }
    }

    /* Check compatibility - Ensures the WAV file matches the PCM device’s parameters (stereo, 44.1 kHz, 16‑bit, PCM). */
    if (num_channels != CHANNELS || sample_rate != SAMPLE_RATE ||
        bits_per_sample != 16 || audio_format != 1) {
        fprintf(stderr, "Unsupported WAV format: %d ch, %d Hz, %d bit\n",
                num_channels, sample_rate, bits_per_sample);
        fclose(wav);
        pthread_mutex_lock(&state_mutex);
        playback_active = 0;
        pthread_mutex_unlock(&state_mutex);
        return NULL;
    }

    /* ----- Playback loop ----- */
    char *buffer = malloc(PERIOD_FRAMES * CHANNELS * (bits_per_sample/8)); /* Allocates a buffer that holds one period of audio data. */
    if (!buffer) {
        fclose(wav);
        pthread_mutex_lock(&state_mutex);
        playback_active = 0;
        pthread_mutex_unlock(&state_mutex);
        return NULL;
    }

    unsigned int bytes_per_frame = CHANNELS * (bits_per_sample/8); /* 4 bytes for stereo 16‑bit. */
    unsigned int frames_remaining = data_size / bytes_per_frame; /* total number of frames in the data chunk. */

    while (frames_remaining > 0 && !stop_requested) { /* Loop until all frames are consumed OR stop_requested is set by audio_stop(). */

        /* Determines how many frames to read, reads it, if nothing is read, break the loop*/
        unsigned int frames_to_read = (frames_remaining < PERIOD_FRAMES) ? frames_remaining : PERIOD_FRAMES;
        size_t bytes_to_read = frames_to_read * bytes_per_frame;
        size_t bytes_read = fread(buffer, 1, bytes_to_read, wav);
        if (bytes_read == 0) break;

        /* writes to PCM device, return how much is written, if there is any discrepency return error*/
        int frames_read = bytes_read / bytes_per_frame;
        snd_pcm_sframes_t frames_written = snd_pcm_writei(pcm_handle, buffer, frames_read);
        if (frames_written < 0) {
            if (frames_written == -EPIPE) {
                /* underrun – recover */
                snd_pcm_prepare(pcm_handle);
                continue;
            }
            fprintf(stderr, "snd_pcm_writei error: %s\n", snd_strerror(frames_written));
            break;
        }
        frames_remaining -= frames_written; /* update frames remaining*/
    }
    /* free buffer and close the WAV file*/
    free(buffer);
    fclose(wav);

    /* Drain and clean up: if not stopping is requested wait for all pending frames before closing 
        If early stopping is requested, drop all frames before closing*/
    if (!stop_requested) {
        snd_pcm_drain(pcm_handle);
    } else {
        snd_pcm_drop(pcm_handle);
        stop_requested = 0;
    }
    /* Clear the playback_active flag under mutex, then exit thread. */
    pthread_mutex_lock(&state_mutex);
    playback_active = 0;
    pthread_mutex_unlock(&state_mutex);
    return NULL;
}

/* Public functions */

int audio_init(void) {
    int err;

    /* Open PCM device */
    err = snd_pcm_open(&pcm_handle, "default", SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) {
        fprintf(stderr, "audio_init: snd_pcm_open error: %s\n", snd_strerror(err));
        return -1;
    }

    /* Set parameters */
    err = snd_pcm_set_params(pcm_handle,
                             FORMAT,
                             SND_PCM_ACCESS_RW_INTERLEAVED,
                             CHANNELS,
                             SAMPLE_RATE,
                             1,      /* soft resample */
                             500000);/* 0.5 sec latency */
    if (err < 0) {
        fprintf(stderr, "audio_init: snd_pcm_set_params error: %s\n", snd_strerror(err));
        snd_pcm_close(pcm_handle);
        return -1;
    }

    /* Open mixer - open mixer, attach to card "default", register simple element class, load the configuration. */
    err = snd_mixer_open(&mixer_handle, 0);
    if (err < 0) {
        fprintf(stderr, "audio_init: snd_mixer_open error: %s\n", snd_strerror(err));
        snd_pcm_close(pcm_handle);
        return -1;
    }

    err = snd_mixer_attach(mixer_handle, "default");
    if (err < 0) {
        fprintf(stderr, "audio_init: snd_mixer_attach error: %s\n", snd_strerror(err));
        snd_mixer_close(mixer_handle);
        snd_pcm_close(pcm_handle);
        return -1;
    }

    err = snd_mixer_selem_register(mixer_handle, NULL, NULL);
    if (err < 0) {
        fprintf(stderr, "audio_init: snd_mixer_selem_register error: %s\n", snd_strerror(err));
        snd_mixer_close(mixer_handle);
        snd_pcm_close(pcm_handle);
        return -1;
    }

    err = snd_mixer_load(mixer_handle);
    if (err < 0) {
        fprintf(stderr, "audio_init: snd_mixer_load error: %s\n", snd_strerror(err));
        snd_mixer_close(mixer_handle);
        snd_pcm_close(pcm_handle);
        return -1;
    }

    /* Find Master control */
    snd_mixer_elem_t *elem = snd_mixer_first_elem(mixer_handle);
    while (elem) {
        if (snd_mixer_selem_has_playback_volume(elem) &&
            strcmp(snd_mixer_selem_get_name(elem), "Master") == 0) {
            master_elem = elem;
            break;
        }
        elem = snd_mixer_elem_next(elem);
    }
    if (!master_elem) {
        fprintf(stderr, "audio_init: Master playback control not found\n");
        snd_mixer_close(mixer_handle);
        snd_pcm_close(pcm_handle);
        return -1;
    }

    /* Set initial volume */
    audio_set_volume(current_volume);
    return 0;
}

void audio_play(const char *path) {
    /* acquire mutex*/
    pthread_mutex_lock(&state_mutex);

    /* if audio is playing*/
    if (playback_active) {
        /* Stop current playback first */
        pthread_mutex_unlock(&state_mutex);
        audio_stop();
        pthread_mutex_lock(&state_mutex);
    }
    /* updated parameters */
    stop_requested = 0;
    playback_active = 1;
    
    pthread_mutex_unlock(&state_mutex); /* realease mutex*/

    /* Duplicate path string for thread */
    char *path_copy = strdup(path);
    if (!path_copy) {
        fprintf(stderr, "audio_play: strdup failed\n");
        pthread_mutex_lock(&state_mutex);
        playback_active = 0;
        pthread_mutex_unlock(&state_mutex);
        return;
    }
    /* Creates a new thread executing playback_thread, passing the duplicated path as argument. */
    pthread_create(&play_thread_id, NULL, playback_thread, path_copy);
    pthread_detach(play_thread_id);   /* thread will free path_copy itself */
}

void audio_stop(void) {
    pthread_mutex_lock(&state_mutex);
    if (playback_active) {
        stop_requested = 1;
        /* Wait for thread to finish? We'll just set flag; thread will exit. */
        /* No join to avoid blocking – thread will clean up. */
    }
    pthread_mutex_unlock(&state_mutex);
}

void audio_set_volume(int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    current_volume = percent;
    if (master_elem) apply_volume();
}

int audio_get_volume(void) {
    return current_volume;
}

int audio_is_playing(void) {
    int ret;
    pthread_mutex_lock(&state_mutex);
    ret = playback_active;
    pthread_mutex_unlock(&state_mutex);
    return ret;
}