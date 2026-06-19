#include "comm_service.h"

#include <stdio.h>
#include <string.h>

#define COMM_MAX_ITEMS 64

static CommCall s_calls[COMM_MAX_ITEMS];
static size_t s_call_count = 0;
static int s_call_seed = 1;

static CommMessage s_messages[COMM_MAX_ITEMS];
static size_t s_message_count = 0;
static int s_message_seed = 1;

static void shift_calls_left(size_t index) {
    for (size_t i = index; i + 1 < s_call_count; i++) {
        s_calls[i] = s_calls[i + 1];
    }
}

static void shift_messages_left(size_t index) {
    for (size_t i = index; i + 1 < s_message_count; i++) {
        s_messages[i] = s_messages[i + 1];
    }
}

void comm_service_init(void) {
    s_call_count = 0;
    s_message_count = 0;
    s_call_seed = 1;
    s_message_seed = 1;

    comm_service_call_add("Noura", "incoming");
    comm_service_call_add("Mom", "outgoing");
    comm_service_call_add("Unknown", "missed");

    comm_service_message_add("Noura", "On my way.");
    comm_service_message_add("Mom", "Call me later.");
}

void comm_service_shutdown(void) {
}

size_t comm_service_call_count(void) {
    return s_call_count;
}

const CommCall *comm_service_call_at(size_t index) {
    if (index >= s_call_count) return NULL;
    return &s_calls[index];
}

int comm_service_call_add(const char *name, const char *type) {
    if (s_call_count >= COMM_MAX_ITEMS) return -1;

    for (size_t i = s_call_count; i > 0; i--) {
        s_calls[i] = s_calls[i - 1];
    }

    CommCall *c = &s_calls[0];
    snprintf(c->name, sizeof(c->name), "%s", name ? name : "Unknown");
    snprintf(c->time, sizeof(c->time), "just now");
    snprintf(c->type, sizeof(c->type), "%s", type ? type : "outgoing");

    if (type && strcmp(type, "incoming") == 0) {
        snprintf(c->icon, sizeof(c->icon), "\u2199");
    } else if (type && strcmp(type, "missed") == 0) {
        snprintf(c->icon, sizeof(c->icon), "\u2717");
    } else {
        snprintf(c->icon, sizeof(c->icon), "\u2197");
    }

    s_call_count++;
    s_call_seed++;
    return 0;
}

int comm_service_call_delete(size_t index) {
    if (index >= s_call_count) return -1;
    shift_calls_left(index);
    s_call_count--;
    return 0;
}

size_t comm_service_message_count(void) {
    return s_message_count;
}

const CommMessage *comm_service_message_at(size_t index) {
    if (index >= s_message_count) return NULL;
    return &s_messages[index];
}

int comm_service_message_add(const char *sender, const char *body) {
    if (s_message_count >= COMM_MAX_ITEMS) return -1;

    for (size_t i = s_message_count; i > 0; i--) {
        s_messages[i] = s_messages[i - 1];
    }

    CommMessage *m = &s_messages[0];
    snprintf(m->sender, sizeof(m->sender), "%s", sender ? sender : "Unknown");
    snprintf(m->body, sizeof(m->body), "%s", body ? body : "New message");
    snprintf(m->stamp, sizeof(m->stamp), "now");

    s_message_count++;
    s_message_seed++;
    return 0;
}

int comm_service_message_delete(size_t index) {
    if (index >= s_message_count) return -1;
    shift_messages_left(index);
    s_message_count--;
    return 0;
}

void comm_service_reset(void) {
    s_call_count = 0;
    s_message_count = 0;
    s_call_seed = 1;
    s_message_seed = 1;
}
