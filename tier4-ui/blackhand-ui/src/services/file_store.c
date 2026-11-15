#define _POSIX_C_SOURCE 200809L
#include "file_store.h"
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

FILE *file_store_begin(const char *path, char *temp, size_t size)
{
    int n = snprintf(temp, size, "%s.tmpXXXXXX", path);
    if (n < 0 || (size_t)n >= size) { errno = ENAMETOOLONG; return NULL; }
    int fd = mkstemp(temp); /* 0600, exclusive creation */
    if (fd < 0) return NULL;
    FILE *f = fdopen(fd, "w");
    if (!f) { close(fd); unlink(temp); }
    return f;
}

int file_store_commit(FILE *f, const char *temp, const char *path, int exclusive)
{
    int ok = !ferror(f);
    if (fflush(f) != 0) ok = 0;
    if (ok && fsync(fileno(f)) != 0) ok = 0;
    if (fclose(f) != 0) ok = 0; /* close even after flush failure */
    if (!ok) { unlink(temp); return -1; }
    int rc = exclusive ? link(temp, path) : rename(temp, path);
    if (exclusive || rc != 0) unlink(temp);
    if (rc != 0) return -1;
    char parent[1024];
    snprintf(parent, sizeof(parent), "%s", path);
    char *slash = strrchr(parent, '/');
    if (!slash) strcpy(parent, ".");
    else if (slash == parent) slash[1] = '\0';
    else *slash = '\0';
    int fd = open(parent, O_RDONLY);
    if (fd < 0) return -1;
    rc = fsync(fd);
    close(fd);
    return rc;
}
