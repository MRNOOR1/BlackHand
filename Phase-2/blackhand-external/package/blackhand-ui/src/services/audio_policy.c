#include "audio_policy.h"
#include <stdatomic.h>
static atomic_int call_active;
void audio_policy_set_call_active(int active) { atomic_store(&call_active, !!active); }
int audio_policy_call_active(void) { return atomic_load(&call_active); }
