#ifndef MULTITAP_SERVICE_H
#define MULTITAP_SERVICE_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t key;
    int index;
    int field;
    size_t pos;
    long long last_ms;
    int active;
    int uppercase;
} multitap_state;

void multitap_init(multitap_state *state);
void multitap_reset(multitap_state *state);
void multitap_set_field(multitap_state *state, int field);
int multitap_toggle_case(multitap_state *state);
int multitap_apply_key(multitap_state *state, uint32_t key, int field, char *buf, size_t buflen);
void multitap_backspace(multitap_state *state, int field, char *buf);

#endif
