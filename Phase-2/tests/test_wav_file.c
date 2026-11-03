#include "wav_file.h"
#include <assert.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>
int main(void) {
    FILE *f = tmpfile(); assert(f); setvbuf(f, NULL, _IONBF, 0);
    assert(wav_file_header(f, 0, 1, 48000) == 0);
    struct rlimit old, limit; assert(getrlimit(RLIMIT_FSIZE, &old) == 0);
    limit = old; limit.rlim_cur = 101; signal(SIGXFSZ, SIG_IGN);
    assert(setrlimit(RLIMIT_FSIZE, &limit) == 0);
    char data[1024] = {0}; uint32_t bytes = 0;
    assert(wav_file_append(f, data, sizeof(data), 1, &bytes) != 0);
    assert(setrlimit(RLIMIT_FSIZE, &old) == 0);
    assert(bytes == 56); /* Only full samples that fit, not intended 1024. */
    assert(wav_file_header(f, bytes, 1, 48000) == 0);
    struct stat st; assert(fstat(fileno(f), &st) == 0); assert(st.st_size == 44 + bytes);
    unsigned char h[44]; rewind(f); assert(fread(h, 1, 44, f) == 44); assert(h[40] == 56);
    fclose(f); puts("PASS: short recording write stops with accurate playable WAV prefix");
}
