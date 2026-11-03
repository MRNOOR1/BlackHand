#ifndef BLACKHAND_FILE_STORE_H
#define BLACKHAND_FILE_STORE_H
#include <stdio.h>
#include <stddef.h>
/* Private temporary file next to destination; no hardware/UI dependencies. */
FILE *file_store_begin(const char *path, char *temp, size_t size);
/* Consumes FILE on every path. exclusive prevents overwriting an existing file. */
int file_store_commit(FILE *file, const char *temp, const char *path, int exclusive);
#endif
