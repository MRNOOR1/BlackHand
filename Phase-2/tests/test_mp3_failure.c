#define _DEFAULT_SOURCE
#include "mp3_service.h"
#include "audio_policy.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
int main(void) {
    char dir[] = "/tmp/blackhand-mp3-XXXXXX"; assert(mkdtemp(dir)); assert(chdir(dir) == 0);
    assert(mkdir("Music", 0700) == 0); assert(mkdir("Music/Test", 0700) == 0);
    FILE *f = fopen("Music/Test/broken.mp3", "w"); assert(f); fputs("not MPEG audio", f); fclose(f);
    assert(mp3_service_init("Music") == 0);
    assert(mp3_service_category_count() == 1);
    audio_policy_set_call_active(1);
    assert(mp3_service_play(0, 0, 0, 1) != 0);
    audio_policy_set_call_active(0);
    assert(mp3_service_play(0, 0, 0, 1) == 0);
    for (int i = 0; i < 100 && mp3_service_get_state() != MP3_ERROR; i++) usleep(10000);
    assert(mp3_service_get_state() == MP3_ERROR);
    mp3_service_update(); assert(mp3_service_get_state() == MP3_ERROR);
    mp3_service_shutdown();
    puts("PASS: call blocks playback; corrupt MP3 reports error without queue advance");
}
