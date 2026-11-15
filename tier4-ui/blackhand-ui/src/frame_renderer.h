#ifndef FRAME_RENDERER_H
#define FRAME_RENDERER_H

#include <stdbool.h>
#include <notcurses/notcurses.h>

/* Frame, border and shared status strip. */
void draw_frame(struct ncplane *phone, int tick, const char *screen_name);


#endif
