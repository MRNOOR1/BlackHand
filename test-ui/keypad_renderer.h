#ifndef KEYPAD_RENDERER_H
#define KEYPAD_RENDERER_H

#include <notcurses/notcurses.h>
#include <stdint.h>

typedef struct {
    int start_row;
    int softkey_row;
    int dpad_row;
    int num_start_row;
    int row_spacing;
} keypad_layout;

keypad_layout keypad_layout_compute(int rows, int cols);

/*
 * draw_keypad() — Render the visual on-screen keypad
 *
 * Draws onto the phone plane starting at row KEYPAD_START_ROW.
 * Shows: 2 soft keys, D-pad with center OK, 12-key numeric pad.
 *
 * The 'active_key' parameter briefly highlights a key when pressed
 * for visual feedback.  Pass 0 for no highlight.
 */
void draw_keypad(struct ncplane *phone, uint32_t active_key);

#endif
