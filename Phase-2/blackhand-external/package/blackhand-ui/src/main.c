/*
 * Black Hand OS — notcurses TUI
 *
 * Runs directly on the Linux framebuffer console (/dev/tty1).
 * Planes:
 *   - status bar  (top row: title + clock)
 *   - content     (menu list)
 *   - nav bar     (bottom row)
 *
 * Logs to stderr (visible over SSH).
 */

#define _POSIX_C_SOURCE 200809L

#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <notcurses/notcurses.h>

/* ---- Menu items ---- */
static const char *menu_items[] = {
    "phone",
    "messages",
    "audio",
    "notes",
    "settings",
};
#define MENU_COUNT (sizeof(menu_items) / sizeof(menu_items[0]))

/* ---- State ---- */
static int selected = 0;

/* ---- Draw status bar (top row): "BLACK HAND" left, HH:MM right ---- */
static void draw_status_bar(struct ncplane *bar, unsigned cols)
{
    ncplane_erase(bar);

    /* dark background */
    uint64_t chan = 0;
    ncchannels_set_bg_rgb8(&chan, 0x1a, 0x1a, 0x1a);
    ncchannels_set_fg_rgb8(&chan, 0xcc, 0xcc, 0xcc);
    ncplane_set_channels(bar, chan);

    /* fill background */
    for (unsigned c = 0; c < cols; c++) {
        ncplane_putchar_yx(bar, 0, c, ' ');
    }

    /* title */
    ncplane_set_channels(bar, chan);
    ncplane_printf_yx(bar, 0, 2, "BLACK HAND");

    /* clock */
    time_t now = time(NULL);
    struct tm *tm_now = localtime(&now);
    if (tm_now) {
        char buf[8];
        strftime(buf, sizeof(buf), "%H:%M", tm_now);
        int clock_x = (int)cols - (int)strlen(buf) - 2;
        if (clock_x < 0) clock_x = 0;
        ncplane_printf_yx(bar, 0, clock_x, "%s", buf);
    }
}

/* ---- Draw content (menu list) ---- */
static void draw_content(struct ncplane *content, unsigned rows, unsigned cols)
{
    ncplane_erase(content);

    /* black background */
    uint64_t bg = 0;
    ncchannels_set_bg_rgb8(&bg, 0x00, 0x00, 0x00);
    ncchannels_set_fg_rgb8(&bg, 0xaa, 0xaa, 0xaa);
    ncplane_set_channels(content, bg);

    /* fill */
    for (unsigned r = 0; r < rows; r++) {
        for (unsigned c = 0; c < cols; c++) {
            ncplane_putchar_yx(content, r, c, ' ');
        }
    }

    /* menu items, starting a few rows from top */
    unsigned start_row = 2;
    unsigned spacing = 3;

    for (unsigned i = 0; i < MENU_COUNT; i++) {
        unsigned row = start_row + i * spacing;
        if (row >= rows) break;

        uint64_t ch = 0;
        ncchannels_set_bg_rgb8(&ch, 0x00, 0x00, 0x00);

        if ((int)i == selected) {
            /* highlight: green text */
            ncchannels_set_fg_rgb8(&ch, 0x00, 0xff, 0x41);
            ncplane_set_channels(content, ch);
            ncplane_printf_yx(content, row, 4, "> %s", menu_items[i]);
        } else {
            /* normal: gray text */
            ncchannels_set_fg_rgb8(&ch, 0xaa, 0xaa, 0xaa);
            ncplane_set_channels(content, ch);
            ncplane_printf_yx(content, row, 4, "  %s", menu_items[i]);
        }
    }
}

/* ---- Draw nav bar (bottom row) ---- */
static void draw_nav_bar(struct ncplane *nav, unsigned cols)
{
    ncplane_erase(nav);

    uint64_t chan = 0;
    ncchannels_set_bg_rgb8(&chan, 0x1a, 0x1a, 0x1a);
    ncchannels_set_fg_rgb8(&chan, 0x88, 0x88, 0x88);
    ncplane_set_channels(nav, chan);

    /* fill */
    for (unsigned c = 0; c < cols; c++) {
        ncplane_putchar_yx(nav, 0, c, ' ');
    }

    /* centered hint */
    const char *hint = "[UP/DOWN] navigate  [ENTER] select  [q] quit";
    int hx = ((int)cols - (int)strlen(hint)) / 2;
    if (hx < 0) hx = 0;
    ncplane_printf_yx(nav, 0, hx, "%s", hint);
}

/* ---- Main ---- */
int main(void)
{
    setlocale(LC_ALL, "");
    fprintf(stderr, "[blackhand-ui] starting (notcurses, direct fbcon)...\n");

    struct notcurses_options opts = {
        .flags = NCOPTION_SUPPRESS_BANNERS
               | NCOPTION_NO_ALTERNATE_SCREEN,
    };

    struct notcurses *nc = notcurses_init(&opts, NULL);
    if (!nc) {
        fprintf(stderr, "[blackhand-ui] notcurses_init failed\n");
        return 1;
    }

    struct ncplane *stdp = notcurses_stdplane(nc);
    unsigned rows, cols;
    ncplane_dim_yx(stdp, &rows, &cols);
    fprintf(stderr, "[blackhand-ui] terminal: %u x %u\n", cols, rows);

    if (rows < 5 || cols < 20) {
        fprintf(stderr, "[blackhand-ui] terminal too small\n");
        notcurses_stop(nc);
        return 1;
    }

    /* Create planes */

    /* Status bar: top row */
    struct ncplane_options bar_opts = {
        .y = 0, .x = 0,
        .rows = 1, .cols = cols,
    };
    struct ncplane *status_bar = ncplane_create(stdp, &bar_opts);

    /* Nav bar: bottom row */
    struct ncplane_options nav_opts = {
        .y = (int)(rows - 1), .x = 0,
        .rows = 1, .cols = cols,
    };
    struct ncplane *nav_bar = ncplane_create(stdp, &nav_opts);

    /* Content: everything between status bar and nav bar */
    unsigned content_rows = rows - 2;
    struct ncplane_options content_opts = {
        .y = 1, .x = 0,
        .rows = content_rows, .cols = cols,
    };
    struct ncplane *content = ncplane_create(stdp, &content_opts);

    /* Initial draw */
    draw_status_bar(status_bar, cols);
    draw_content(content, content_rows, cols);
    draw_nav_bar(nav_bar, cols);
    notcurses_render(nc);

    fprintf(stderr, "[blackhand-ui] UI rendered. entering input loop...\n");

    /* Input loop */
    struct ncinput ni;
    while (1) {
        /* Non-blocking get with timeout (1 second) for clock updates */
        uint32_t r = notcurses_get(nc, &(struct timespec){.tv_sec = 1}, &ni);

        if (r == (uint32_t)-1) {
            break; /* error */
        }

        if (r == 0) {
            /* timeout — just refresh clock */
            draw_status_bar(status_bar, cols);
            notcurses_render(nc);
            continue;
        }

        /* Process input */
        if (ni.id == 'q' || ni.id == 'Q') {
            break;
        }

        int redraw = 0;

        if (ni.id == NCKEY_UP || ni.id == 'k') {
            if (selected > 0) {
                selected--;
                redraw = 1;
            }
        } else if (ni.id == NCKEY_DOWN || ni.id == 'j') {
            if (selected < (int)MENU_COUNT - 1) {
                selected++;
                redraw = 1;
            }
        } else if (ni.id == NCKEY_ENTER) {
            fprintf(stderr, "[blackhand-ui] selected: %s\n", menu_items[selected]);
            /* TODO: switch to sub-screen */
        }

        if (redraw) {
            draw_content(content, content_rows, cols);
            draw_status_bar(status_bar, cols);
            notcurses_render(nc);
        }
    }

    notcurses_stop(nc);
    fprintf(stderr, "[blackhand-ui] exited cleanly\n");
    return 0;
}
