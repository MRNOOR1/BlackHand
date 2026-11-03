/* Pure production policy tests; no init, mount, reboot or hardware stubs. */
#include "restart_policy.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    assert(backoff_delay(0) == 0);
    assert(backoff_delay(1) == 1);
    assert(backoff_delay(2) == 2);
    assert(backoff_delay(3) == 4);
    assert(backoff_delay(4) == 8);
    assert(backoff_delay(5) == 16);
    assert(backoff_delay(BACKOFF_MAX_COUNT) == 30);
    assert(backoff_delay(1000) == 30);
    RestartGuard guard = {0}; time_t t = 1000;
    for (int i = 0; i < SVCMGR_MAX_DEATHS; i++) assert(!restart_guard_exhausted(&guard, t));
    assert(restart_guard_exhausted(&guard, t));
    assert(!restart_guard_exhausted(&guard, t + SVCMGR_WINDOW_SEC + 1));
    assert(!restart_due(0, 1, 0, 0, 0));
    assert(restart_due(0, 1, 1, 0, 0));
    assert(!restart_due(0, 1, 1, 5, 4));
    assert(restart_due(0, 1, 1, 5, 5));
    assert(!restart_due(1, 1, 1, 5, 100));
    assert(!restart_due(0, 0, 1, 5, 100));
    puts("selftest: OK");
}
