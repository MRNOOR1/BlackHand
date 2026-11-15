#ifndef BLACKHAND_RESTART_POLICY_H
#define BLACKHAND_RESTART_POLICY_H
#include <time.h>
#define BACKOFF_MAX_COUNT 6
#define SVCMGR_MAX_DEATHS 5
#define SVCMGR_WINDOW_SEC 60
/* Pure restart decisions, shared by production and host tests. */
typedef struct { int deaths; time_t window_start; } RestartGuard;
static inline int backoff_delay(int count) {
    if (count <= 0) return 0;
    if (count >= BACKOFF_MAX_COUNT) return 30;
    return 1 << (count - 1);
}
static inline int restart_guard_exhausted(RestartGuard *guard, time_t now) {
    if (now - guard->window_start > SVCMGR_WINDOW_SEC) {
        guard->window_start = now; guard->deaths = 0;
    }
    if (guard->deaths <= SVCMGR_MAX_DEATHS) guard->deaths++;
    return guard->deaths > SVCMGR_MAX_DEATHS;
}
static inline int restart_due(int running, int enabled, int pending, time_t deadline, time_t now) {
    return !running && enabled && pending && now >= deadline;
}
#endif
