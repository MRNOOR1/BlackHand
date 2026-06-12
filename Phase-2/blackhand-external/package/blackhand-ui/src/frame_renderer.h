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

/* Themed footer (rows-3..rows-2). Called by ghost_softkeys() after a screen
 * finishes drawing, so the list-position tracker (fed by ghost_list_row) is
 * complete for FOOTER_READOUT. Decision screens skip it via action cells. */
void draw_footer(struct ncplane *phone);

/* List-position tracker — frame resets each frame; ghost_list_row feeds it. */
void frame_track_reset(void);
void frame_track_row(int index, int selected);


#endif
