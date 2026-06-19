#include "multitap_service.h"

#include <ctype.h>
#include <string.h>
#include <time.h>

#define MULTITAP_TIMEOUT_MS 1000

static long long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((long long)ts.tv_sec * 1000LL) + (ts.tv_nsec / 1000000LL);
}

static const char *seq_for_key(uint32_t key) {
    switch (key) {
        case '1': return "1";
        case '2': return "abc";
        case '3': return "def";
        case '4': return "ghi";
        case '5': return "jkl";
        case '6': return "mno";
        case '7': return "pqrs";
        case '8': return "tuv";
        case '9': return "wxyz";
        case '0': return " 0";
        case '*': return ".,!?";
        default: return NULL;
    }
}

static char with_case(char ch, int uppercase) {
    if (!isalpha((unsigned char)ch)) return ch;
    return uppercase ? (char)toupper((unsigned char)ch) : (char)tolower((unsigned char)ch);
}

void multitap_init(multitap_state *state) {
    if (!state) return;
    memset(state, 0, sizeof(*state));
}

void multitap_reset(multitap_state *state) {
    if (!state) return;
    state->active = 0;
    state->key = 0;
    state->index = 0;
    state->pos = 0;
    state->last_ms = 0;
}

void multitap_set_field(multitap_state *state, int field) {
    if (!state) return;
    if (state->field != field) {
        multitap_reset(state);
        state->field = field;
    }
}

int multitap_toggle_case(multitap_state *state) {
    if (!state) return 0;
    state->uppercase = !state->uppercase;
    multitap_reset(state);
    return state->uppercase;
}

int multitap_apply_key(multitap_state *state, uint32_t key, int field, char *buf, size_t buflen) {
    if (!state || !buf || buflen == 0) return -1;
    const char *seq = seq_for_key(key);
    if (!seq) return 0;

    size_t seq_len = strlen(seq);
    if (seq_len == 0) return 0;

    multitap_set_field(state, field);
    long long t = now_ms();

    if (state->active && state->key == key && state->field == field && (t - state->last_ms) <= MULTITAP_TIMEOUT_MS) {
        state->index = (state->index + 1) % (int)seq_len;
        if (state->pos < strlen(buf)) {
            buf[state->pos] = with_case(seq[state->index], state->uppercase);
            state->last_ms = t;
            return 1;
        }
    }

    size_t len = strlen(buf);
    if (len + 1 >= buflen) return -1;

    buf[len] = with_case(seq[0], state->uppercase);
    buf[len + 1] = '\0';

    state->active = 1;
    state->key = key;
    state->index = 0;
    state->pos = len;
    state->field = field;
    state->last_ms = t;
    return 1;
}

void multitap_backspace(multitap_state *state, int field, char *buf) {
    if (!buf) return;
    size_t len = strlen(buf);
    if (len == 0) {
        if (state) multitap_reset(state);
        return;
    }
    buf[len - 1] = '\0';
    if (state && state->active && state->field == field && state->pos >= len - 1) {
        multitap_reset(state);
    }
}
