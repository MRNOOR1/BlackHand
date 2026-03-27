#ifndef FRAME_RENDERER_H
#define FRAME_RENDERER_H

#include <stdbool.h>
#include <notcurses/notcurses.h>

/*
frame renderer.h is for the nav bar items such as battery, signal etc.
*/

void draw_battery(struct ncplane *phone, int percent, bool charging, int tick);
void draw_signal(struct ncplane *phone, int bars, bool connected, int tick);
void draw_status_bar(struct ncplane *phone, int tick);
void draw_frame(struct ncplane *phone, int tick, const char *screen_name);


#endif
