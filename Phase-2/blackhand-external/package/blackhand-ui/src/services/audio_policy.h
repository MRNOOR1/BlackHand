#ifndef BLACKHAND_AUDIO_POLICY_H
#define BLACKHAND_AUDIO_POLICY_H
/* Shared across render, headset-input and AVRCP threads. */
void audio_policy_set_call_active(int active);
int audio_policy_call_active(void);
#endif
