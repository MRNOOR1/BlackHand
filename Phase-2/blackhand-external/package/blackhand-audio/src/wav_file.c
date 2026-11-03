#define _POSIX_C_SOURCE 200809L
#include "wav_file.h"
#include <string.h>
#include <unistd.h>
#include <errno.h>
static void le16(unsigned char *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
static void le32(unsigned char *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = v >> (8 * i); }
int wav_file_header(FILE *f, uint32_t bytes, uint16_t channels, uint32_t rate)
{
    if (!channels || bytes > UINT32_MAX - 36) return -1;
    unsigned char h[44] = {0};
    memcpy(h, "RIFF", 4); le32(h + 4, 36 + bytes);
    memcpy(h + 8, "WAVEfmt ", 8); le32(h + 16, 16); le16(h + 20, 1);
    le16(h + 22, channels); le32(h + 24, rate);
    le32(h + 28, rate * channels * 2); le16(h + 32, channels * 2);
    le16(h + 34, 16); memcpy(h + 36, "data", 4); le32(h + 40, bytes);
    clearerr(f); /* allow finalisation after a short data write */
    if (fseek(f, 0, SEEK_SET) != 0) return -1;
    if (fwrite(h, 1, sizeof(h), f) != sizeof(h) || fflush(f) != 0) return -1;
    return fsync(fileno(f));
}
int wav_file_append(FILE *f, const void *data, size_t bytes, uint16_t channels, uint32_t *total)
{
    size_t frame = (size_t)channels * 2;
    if (!frame || bytes % frame || bytes > UINT32_MAX - 36 - *total) return -1;
    size_t written = fwrite(data, 1, bytes, f);
    size_t complete = written - written % frame;
    *total += (uint32_t)complete;
    if (written != bytes || ferror(f)) {
        /* Files are unbuffered. Remove a partial sample; keep a playable prefix. */
        if (written != complete) ftruncate(fileno(f), (off_t)44 + *total);
        return -1;
    }
    return 0;
}
