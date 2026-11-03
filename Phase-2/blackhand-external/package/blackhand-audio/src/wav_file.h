#ifndef BLACKHAND_WAV_FILE_H
#define BLACKHAND_WAV_FILE_H
#include <stdint.h>
#include <stdio.h>
int wav_file_header(FILE *f, uint32_t bytes, uint16_t channels, uint32_t rate);
/* Counts only complete frames actually written. Returns -1 on any short write. */
int wav_file_append(FILE *f, const void *data, size_t bytes, uint16_t channels, uint32_t *total);
#endif
