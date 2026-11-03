#define _POSIX_C_SOURCE 200809L
#include "pin_service.h"
#include "file_store.h"
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/crypto.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <errno.h>
#include <limits.h>
#ifndef PIN_FILE
#define PIN_FILE "/data/pin.conf"
#endif
#define PIN_ROUNDS 600000
#define PIN_FREE_ATTEMPTS 2
#define PIN_LOCKOUT_STEP_S 5
#define PIN_LOCKOUT_MAX_S 60
static unsigned char s_salt[16], s_hash[32];
static int s_ready, s_fail_count;
static int64_t s_lockout_until_ms;
static int64_t monotonic_ms(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
static int valid_pin(const char *pin) {
    return pin && strlen(pin) == 4 && strspn(pin, "0123456789") == 4;
}
static int derive(const char *pin, const unsigned char *salt, unsigned char *out) {
    return PKCS5_PBKDF2_HMAC(pin, 4, salt, 16, PIN_ROUNDS, EVP_sha256(), 32, out) == 1 ? 0 : -1;
}
static int store_pin(const char *pin) {
    unsigned char salt[16], hash[32];
    if (!valid_pin(pin) || RAND_bytes(salt, sizeof(salt)) != 1 || derive(pin, salt, hash) != 0) return -1;
    char temp[512]; FILE *f = file_store_begin(PIN_FILE, temp, sizeof(temp));
    if (!f) { OPENSSL_cleanse(hash, sizeof(hash)); return -1; }
    fputs("bhpin1:", f);
    for (size_t i = 0; i < sizeof(salt); i++) fprintf(f, "%02x", salt[i]);
    fputc(':', f);
    for (size_t i = 0; i < sizeof(hash); i++) fprintf(f, "%02x", hash[i]);
    fputc('\n', f);
    int rc = file_store_commit(f, temp, PIN_FILE, 0);
    if (rc == 0) { memcpy(s_salt, salt, sizeof(salt)); memcpy(s_hash, hash, sizeof(hash)); s_ready = 1; }
    OPENSSL_cleanse(hash, sizeof(hash));
    return rc;
}
static int unhex(const char *s, unsigned char *out, size_t n) {
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        const char *a = strchr(digits, s[2*i]);
        const char *b = strchr(digits, s[2*i+1]);
        if (!a || !b) return -1;
        out[i] = (unsigned char)(((a - digits) << 4) | (b - digits));
    }
    return 0;
}
void pin_service_init(void) {
    s_ready = 0;
    FILE *f = fopen(PIN_FILE, "r");
    if (!f) { if (errno == ENOENT) store_pin("0000"); return; }
    char line[128] = {0};
    size_t n = fread(line, 1, sizeof(line) - 1, f);
    int ok = !ferror(f) && feof(f); fclose(f);
    if (ok) {
        if (n && line[n-1] == '\n') line[--n] = '\0';
        if (n == 4 && valid_pin(line)) store_pin(line); /* atomic legacy migration */
        else if (n == 104 && !strncmp(line, "bhpin1:", 7) && line[39] == ':' &&
                 unhex(line + 7, s_salt, 16) == 0 && unhex(line + 40, s_hash, 32) == 0) s_ready = 1;
    }
    OPENSSL_cleanse(line, sizeof(line));
}
bool pin_service_verify(const char *pin) {
    int64_t now = monotonic_ms();
    if (!s_ready || !valid_pin(pin) || now < s_lockout_until_ms) return false;
    unsigned char hash[32];
    int ok = derive(pin, s_salt, hash) == 0 && CRYPTO_memcmp(hash, s_hash, sizeof(hash)) == 0;
    OPENSSL_cleanse(hash, sizeof(hash));
    if (ok) { s_fail_count = 0; s_lockout_until_ms = 0; return true; }
    if (s_fail_count < PIN_FREE_ATTEMPTS + PIN_LOCKOUT_MAX_S / PIN_LOCKOUT_STEP_S) s_fail_count++;
    if (s_fail_count > PIN_FREE_ATTEMPTS)
        s_lockout_until_ms = now + (int64_t)(s_fail_count - PIN_FREE_ATTEMPTS) * PIN_LOCKOUT_STEP_S * 1000;
    return false;
}
int pin_service_lockout_remaining_sec(void) {
    int64_t now = monotonic_ms();
    if (now >= s_lockout_until_ms) return 0;
    return (int)((s_lockout_until_ms - now + 999) / 1000); /* round up */
}


int pin_service_update(const char *old_pin, const char *new_pin) {
    if (!valid_pin(new_pin) || !pin_service_verify(old_pin)) return -1;
    return store_pin(new_pin);
}
int pin_service_reset(void) {
    int rc = store_pin("0000");
    if (rc == 0) { s_fail_count = 0; s_lockout_until_ms = 0; }
    return rc;
}
