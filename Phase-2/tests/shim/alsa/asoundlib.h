/*
 * shim/alsa/asoundlib.h — host-only stand-in for libasound's public header.
 *
 * WHY THIS EXISTS
 * ---------------
 * The WAV header parser (pb_parse_wav) and writer (rc_write_wav_header) in
 * blackhand-audio/src/audio_alsa.c are pure logic — no hardware needed — but
 * they are `static` and live in a translation unit that #includes
 * <alsa/asoundlib.h>. Development hosts (macOS, and Linux boxes without
 * alsa-lib-dev) have no such header, so the file cannot be compiled at all.
 *
 * This shim declares exactly the ALSA surface audio_alsa.c uses, so the REAL
 * source compiles unmodified and the real WAV functions are exercised by
 * tests/test_audio_wav.c.
 *
 * IMPORTANT: every PCM/mixer entry point here FAILS (open returns -1, elem
 * lookups return NULL). That is deliberate. It guarantees the host tests can
 * never open a device, start a thread, or touch audio hardware — they only
 * reach the file-format logic. Nothing here emulates ALSA behaviour, and
 * nothing here may be used to claim a playback/capture path is verified.
 *
 * Only the WAV parse/write logic is host-verifiable. Everything else in
 * audio_alsa.c still requires the on-device runbook.
 */
#ifndef BH_TEST_SHIM_ASOUNDLIB_H
#define BH_TEST_SHIM_ASOUNDLIB_H

#include <stddef.h>
#include <errno.h>

/*
 * EBADFD is a Linux-only errno. audio_alsa.c's capture loop compares against
 * it (-EBADFD from snd_pcm_start on an already-running stream), so a BSD/macOS
 * host cannot compile the file without it. Target builds get the real value
 * from glibc; this define only ever applies to host test builds.
 */
#ifndef EBADFD
#define EBADFD 77
#endif

/* Opaque handles — audio_alsa.c only ever holds pointers to these. */
typedef struct bh_shim_pcm        snd_pcm_t;
typedef struct bh_shim_mixer      snd_mixer_t;
typedef struct bh_shim_mixer_elem snd_mixer_elem_t;

typedef long          snd_pcm_sframes_t;
typedef unsigned long snd_pcm_uframes_t;
typedef int           snd_pcm_format_t;
typedef int           snd_mixer_selem_channel_id_t;

#define SND_PCM_FORMAT_U8            1
#define SND_PCM_FORMAT_S16_LE        2
#define SND_PCM_FORMAT_S24_3LE       3

#define SND_PCM_STREAM_PLAYBACK      0
#define SND_PCM_STREAM_CAPTURE       1

#define SND_PCM_ACCESS_RW_INTERLEAVED 3
#define SND_PCM_NONBLOCK              0x00000001
#define SND_MIXER_SCHN_MONO           0

/* ── PCM: every call fails or no-ops ─────────────────────────────────── */

static inline int snd_pcm_open(snd_pcm_t **pcm, const char *name,
                               int stream, int mode)
{
    (void)name; (void)stream; (void)mode;
    if (pcm) *pcm = NULL;
    return -1;                 /* ENODEV-ish: no device on a build host */
}

static inline int snd_pcm_set_params(snd_pcm_t *pcm, snd_pcm_format_t fmt,
                                     int access, unsigned int channels,
                                     unsigned int rate, int soft_resample,
                                     unsigned int latency)
{
    (void)pcm; (void)fmt; (void)access; (void)channels;
    (void)rate; (void)soft_resample; (void)latency;
    return -1;
}

static inline int snd_pcm_close(snd_pcm_t *pcm)   { (void)pcm; return 0; }
static inline int snd_pcm_drop(snd_pcm_t *pcm)    { (void)pcm; return 0; }
static inline int snd_pcm_drain(snd_pcm_t *pcm)   { (void)pcm; return 0; }
static inline int snd_pcm_prepare(snd_pcm_t *pcm) { (void)pcm; return 0; }
static inline int snd_pcm_start(snd_pcm_t *pcm)   { (void)pcm; return -1; }

static inline snd_pcm_sframes_t snd_pcm_writei(snd_pcm_t *pcm, const void *buf,
                                               snd_pcm_uframes_t frames)
{
    (void)pcm; (void)buf; (void)frames;
    return -1;
}

static inline snd_pcm_sframes_t snd_pcm_readi(snd_pcm_t *pcm, void *buf,
                                              snd_pcm_uframes_t frames)
{
    (void)pcm; (void)buf; (void)frames;
    return -1;
}

static inline int snd_pcm_recover(snd_pcm_t *pcm, int err, int silent)
{
    (void)pcm; (void)err; (void)silent;
    return -1;                 /* unrecoverable: I/O loops exit immediately */
}

static inline const char *snd_strerror(int err)
{
    (void)err;
    return "shim: no ALSA on host";
}

/* ── Mixer: no cards, so no element is ever found ────────────────────── */

static inline int snd_mixer_open(snd_mixer_t **mixer, int mode)
{
    (void)mode;
    if (mixer) *mixer = NULL;
    return -1;
}

static inline int snd_mixer_attach(snd_mixer_t *m, const char *card)
{
    (void)m; (void)card; return -1;
}

static inline int snd_mixer_selem_register(snd_mixer_t *m, void *opts, void **cls)
{
    (void)m; (void)opts; (void)cls; return -1;
}

static inline int snd_mixer_load(snd_mixer_t *m)          { (void)m; return -1; }
static inline int snd_mixer_close(snd_mixer_t *m)         { (void)m; return 0; }
static inline int snd_mixer_handle_events(snd_mixer_t *m) { (void)m; return 0; }

static inline snd_mixer_elem_t *snd_mixer_first_elem(snd_mixer_t *m)
{
    (void)m; return NULL;
}

static inline snd_mixer_elem_t *snd_mixer_elem_next(snd_mixer_elem_t *e)
{
    (void)e; return NULL;
}

static inline const char *snd_mixer_selem_get_name(snd_mixer_elem_t *e)
{
    (void)e; return "";
}

static inline int snd_mixer_selem_has_playback_volume(snd_mixer_elem_t *e)
{
    (void)e; return 0;
}

static inline int snd_mixer_selem_get_playback_dB_range(snd_mixer_elem_t *e,
                                                        long *min, long *max)
{
    (void)e; if (min) *min = 0; if (max) *max = 0; return -1;
}

static inline int snd_mixer_selem_set_playback_dB_all(snd_mixer_elem_t *e,
                                                      long value, int dir)
{
    (void)e; (void)value; (void)dir; return -1;
}

static inline int snd_mixer_selem_get_playback_dB(snd_mixer_elem_t *e,
                                                  snd_mixer_selem_channel_id_t c,
                                                  long *value)
{
    (void)e; (void)c; if (value) *value = 0; return -1;
}

static inline int snd_mixer_selem_get_playback_volume_range(snd_mixer_elem_t *e,
                                                            long *min, long *max)
{
    (void)e; if (min) *min = 0; if (max) *max = 0; return -1;
}

static inline int snd_mixer_selem_set_playback_volume_all(snd_mixer_elem_t *e,
                                                          long value)
{
    (void)e; (void)value; return -1;
}

static inline int snd_mixer_selem_get_playback_volume(snd_mixer_elem_t *e,
                                                      snd_mixer_selem_channel_id_t c,
                                                      long *value)
{
    (void)e; (void)c; if (value) *value = 0; return -1;
}

#endif /* BH_TEST_SHIM_ASOUNDLIB_H */
