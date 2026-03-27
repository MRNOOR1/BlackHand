#include <notcurses/notcurses.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "draw_utils.h"
#include "ui.h"
#include "services/mp3_service.h"
#include "services/theme_service.h"

/* ── Music screen modes ──────────────────────────────────────────────────
 *
 * CATEGORIES: list of category folders
 *   up/down    = move through list
 *   center     = open selected category
 *   LSK (q)    = back to main menu
 *   RSK (e)    = back to main menu
 *
 * COLLECTIONS: list of collections in a category
 *   up/down    = move through list
 *   center     = open selected collection (or play direct track)
 *   LSK (q)    = back to categories
 *   RSK (e)    = back to main menu
 *
 * TRACKS: list of tracks in a collection
 *   up/down    = move through list
 *   center     = play selected track
 *   LSK (q)    = back to collections
 *   RSK (e)    = back to main menu
 *
 * NOW_PLAYING: currently playing track
 *   center     = toggle play/pause
 *   left       = previous track
 *   right      = next track
 *   up         = volume up
 *   down       = volume down
 *   LSK (q)    = back to track list (music continues)
 *   RSK (e)    = stop playback
 * ────────────────────────────────────────────────────────────────────── */

typedef enum {
    MP3_MODE_CATEGORIES,
    MP3_MODE_COLLECTIONS,
    MP3_MODE_TRACKS,
    MP3_MODE_NOW_PLAYING,
} mp3_mode_t;

static mp3_mode_t mode = MP3_MODE_CATEGORIES;
static int sel_cat = 0;
static int sel_col = 0;
static int sel_track = 0;
static int scroll_cat = 0;
static int scroll_col = 0;
static int scroll_track = 0;

/* Track whether we entered NOW_PLAYING from TRACKS or COLLECTIONS (direct) */
static int playing_direct = 0;

static void put_clipped(struct ncplane *p, int row, int col, int max_w, const char *text) {
    if (!text || max_w <= 0) return;
    char buf[256];
    snprintf(buf, sizeof(buf), "%s", text);
    if ((int)strlen(buf) > max_w) {
        if (max_w > 3) { buf[max_w-3]='.'; buf[max_w-2]='.'; buf[max_w-1]='.'; }
        buf[max_w] = '\0';
    }
    ncplane_putstr_yx(p, row, col, buf);
}

/* ── Draw: Categories list ───────────────────────────────────────────── */
static void draw_categories(struct ncplane *phone, unsigned rows, unsigned cols) {
    int footer = (int)rows - FOOTER_ROW_OFFSET;
    int width = INNER_WIDTH(cols);
    size_t count = mp3_service_category_count();

    ncplane_set_fg_rgb(phone, theme_text_primary());
    ncplane_set_bg_rgb(phone, theme_bg());
    ncplane_putstr_yx(phone, CONTENT_START_ROW, CONTENT_COL, "MUSIC");

    ncplane_set_fg_rgb(phone, theme_text_muted());
    for (int x = 0; x < width && CONTENT_COL + x < (int)cols - 1; x++)
        ncplane_putstr_yx(phone, CONTENT_START_ROW + 1, CONTENT_COL + x, "\u2500");

    if (count == 0) {
        int mid = (CONTENT_START_ROW + 2 + footer) / 2;
        ghost_text(phone, mid - 1, CONTENT_COL, theme_text_muted(), "\u266B  \u266A  \u266B");
        ghost_text(phone, mid + 1, CONTENT_COL, theme_text_muted(), "No music found");
        ghost_text(phone, mid + 2, CONTENT_COL, theme_text_muted(), "Place files in /data/music/");
        ghost_softkeys(phone, "[Back]", "[Menu]");
        return;
    }

    /* Check if music is playing - show indicator */
    mp3_playback_state st = mp3_service_get_state();
    if (st == MP3_PLAYING || st == MP3_PAUSED) {
        const char *tname = mp3_service_current_track_name();
        char now_line[64];
        snprintf(now_line, sizeof(now_line), "%s %s",
                 (st == MP3_PLAYING) ? "\u25B6" : "\u258C\u258C",
                 tname ? tname : "");
        ghost_text(phone, CONTENT_START_ROW + 2, CONTENT_COL, theme_text_muted(), now_line);
    }

    if (sel_cat < 0) sel_cat = 0;
    if (sel_cat >= (int)count) sel_cat = (int)count - 1;

    int list_start = CONTENT_START_ROW + 3;
    int max_rows = footer - list_start - 1;
    if (max_rows < 1) max_rows = 1;

    if (sel_cat < scroll_cat) scroll_cat = sel_cat;
    if (sel_cat >= scroll_cat + max_rows) scroll_cat = sel_cat - max_rows + 1;
    if (scroll_cat < 0) scroll_cat = 0;

    for (int i = 0; i < max_rows; i++) {
        int idx = scroll_cat + i;
        if (idx >= (int)count) break;

        const Mp3Category *cat = mp3_service_get_category((size_t)idx);
        if (!cat) continue;

        int row = list_start + i;
        int sel = (idx == sel_cat);

        ncplane_set_fg_rgb(phone, sel ? theme_text_primary() : theme_text_muted());
        ncplane_set_bg_rgb(phone, theme_bg());
        ncplane_putstr_yx(phone, row, CONTENT_COL, sel ? MENU_CURSOR : MENU_CURSOR_BLANK);
        put_clipped(phone, row, CONTENT_COL + 2, width - 2, cat->name ? cat->name : "?");
    }

    ghost_softkeys(phone, "[Back]", "[Menu]");
}

/* ── Draw: Collections list ──────────────────────────────────────────── */
static void draw_collections(struct ncplane *phone, unsigned rows, unsigned cols) {
    int footer = (int)rows - FOOTER_ROW_OFFSET;
    int width = INNER_WIDTH(cols);

    const Mp3Category *cat = mp3_service_get_category((size_t)sel_cat);
    if (!cat) { mode = MP3_MODE_CATEGORIES; return; }

    ncplane_set_fg_rgb(phone, theme_text_primary());
    ncplane_set_bg_rgb(phone, theme_bg());
    put_clipped(phone, CONTENT_START_ROW, CONTENT_COL, width, cat->name ? cat->name : "CATEGORY");

    ncplane_set_fg_rgb(phone, theme_text_muted());
    for (int x = 0; x < width && CONTENT_COL + x < (int)cols - 1; x++)
        ncplane_putstr_yx(phone, CONTENT_START_ROW + 1, CONTENT_COL + x, "\u2500");

    /* Build combined list: collections + direct tracks */
    size_t col_count = cat->collection_count;
    size_t dir_count = cat->direct_track_count;
    size_t total = col_count + dir_count;

    if (total == 0) {
        int mid = (CONTENT_START_ROW + 2 + footer) / 2;
        ghost_text(phone, mid, CONTENT_COL, theme_text_muted(), "No tracks");
        ghost_softkeys(phone, "[Back]", "[Menu]");
        return;
    }

    if (sel_col < 0) sel_col = 0;
    if (sel_col >= (int)total) sel_col = (int)total - 1;

    int list_start = CONTENT_START_ROW + 3;
    int max_rows = footer - list_start - 1;
    if (max_rows < 1) max_rows = 1;

    if (sel_col < scroll_col) scroll_col = sel_col;
    if (sel_col >= scroll_col + max_rows) scroll_col = sel_col - max_rows + 1;
    if (scroll_col < 0) scroll_col = 0;

    for (int i = 0; i < max_rows; i++) {
        int idx = scroll_col + i;
        if (idx >= (int)total) break;

        int row = list_start + i;
        int sel = (idx == sel_col);

        ncplane_set_fg_rgb(phone, sel ? theme_text_primary() : theme_text_muted());
        ncplane_set_bg_rgb(phone, theme_bg());
        ncplane_putstr_yx(phone, row, CONTENT_COL, sel ? MENU_CURSOR : MENU_CURSOR_BLANK);

        if (idx < (int)col_count) {
            /* Collection */
            const Mp3Collection *c = &cat->collections[idx];
            char line[256];
            snprintf(line, sizeof(line), "\u25B7 %s (%zu)",
                     c->name ? c->name : "?", c->track_count);
            put_clipped(phone, row, CONTENT_COL + 2, width - 2, line);
        } else {
            /* Direct track */
            size_t tidx = (size_t)(idx - (int)col_count);
            const Mp3Track *t = &cat->direct_tracks[tidx];
            char line[256];
            snprintf(line, sizeof(line), "\u266A %s", t->title ? t->title : "?");
            put_clipped(phone, row, CONTENT_COL + 2, width - 2, line);
        }
    }

    ghost_softkeys(phone, "[Back]", "[Menu]");
}

/* ── Draw: Tracks list ───────────────────────────────────────────────── */
static void draw_tracks(struct ncplane *phone, unsigned rows, unsigned cols) {
    int footer = (int)rows - FOOTER_ROW_OFFSET;
    int width = INNER_WIDTH(cols);

    const Mp3Collection *col = mp3_service_get_collection((size_t)sel_cat, (size_t)sel_col);
    if (!col) { mode = MP3_MODE_COLLECTIONS; return; }

    ncplane_set_fg_rgb(phone, theme_text_primary());
    ncplane_set_bg_rgb(phone, theme_bg());
    put_clipped(phone, CONTENT_START_ROW, CONTENT_COL, width, col->name ? col->name : "COLLECTION");

    ncplane_set_fg_rgb(phone, theme_text_muted());
    for (int x = 0; x < width && CONTENT_COL + x < (int)cols - 1; x++)
        ncplane_putstr_yx(phone, CONTENT_START_ROW + 1, CONTENT_COL + x, "\u2500");

    size_t count = col->track_count;
    if (count == 0) {
        int mid = (CONTENT_START_ROW + 2 + footer) / 2;
        ghost_text(phone, mid, CONTENT_COL, theme_text_muted(), "No tracks");
        ghost_softkeys(phone, "[Back]", "[Menu]");
        return;
    }

    if (sel_track < 0) sel_track = 0;
    if (sel_track >= (int)count) sel_track = (int)count - 1;

    int list_start = CONTENT_START_ROW + 3;
    int max_rows = footer - list_start - 1;
    if (max_rows < 1) max_rows = 1;

    if (sel_track < scroll_track) scroll_track = sel_track;
    if (sel_track >= scroll_track + max_rows) scroll_track = sel_track - max_rows + 1;
    if (scroll_track < 0) scroll_track = 0;

    for (int i = 0; i < max_rows; i++) {
        int idx = scroll_track + i;
        if (idx >= (int)count) break;

        const Mp3Track *t = &col->tracks[idx];
        int row = list_start + i;
        int sel = (idx == sel_track);

        ncplane_set_fg_rgb(phone, sel ? theme_text_primary() : theme_text_muted());
        ncplane_set_bg_rgb(phone, theme_bg());
        ncplane_putstr_yx(phone, row, CONTENT_COL, sel ? MENU_CURSOR : MENU_CURSOR_BLANK);

        char line[256];
        snprintf(line, sizeof(line), "%s%s",
                 sel ? "\u266A " : "",
                 t->title ? t->title : "?");
        put_clipped(phone, row, CONTENT_COL + 2, width - 2, line);
    }

    ghost_softkeys(phone, "[Back]", "[Menu]");
}

/* ── Draw: Now Playing ───────────────────────────────────────────────── */
static void draw_now_playing(struct ncplane *phone, unsigned rows, unsigned cols) {
    mp3_playback_state st = mp3_service_get_state();
    if (st == MP3_STOPPED) {
        /* If playback stopped, return to track list */
        if (playing_direct)
            mode = MP3_MODE_COLLECTIONS;
        else
            mode = MP3_MODE_TRACKS;
        return;
    }

    unsigned elapsed = mp3_service_get_elapsed();
    int footer = (int)rows - FOOTER_ROW_OFFSET;
    int width = INNER_WIDTH(cols);

    /* Header */
    ncplane_set_fg_rgb(phone, theme_text_primary());
    ncplane_set_bg_rgb(phone, theme_bg());
    ncplane_putstr_yx(phone, CONTENT_START_ROW, CONTENT_COL, "NOW PLAYING");

    ncplane_set_fg_rgb(phone, theme_text_muted());
    for (int x = 0; x < width && CONTENT_COL + x < (int)cols - 1; x++)
        ncplane_putstr_yx(phone, CONTENT_START_ROW + 1, CONTENT_COL + x, "\u2500");

    int info_row = CONTENT_START_ROW + 3;

    /* Play/pause icon + state */
    const char *state_str = (st == MP3_PLAYING) ? "PLAYING" : "PAUSED";
    const char *icon = (st == MP3_PLAYING) ? "\u25B6" : "\u258C\u258C";
    char status_line[128];
    unsigned mins = elapsed / 60;
    unsigned secs = elapsed % 60;
    snprintf(status_line, sizeof(status_line), "%s  %s  %02u:%02u", icon, state_str, mins, secs);
    ghost_text(phone, info_row, CONTENT_COL, theme_text_primary(), status_line);

    /* Track name */
    const char *track = mp3_service_current_track_name();
    ncplane_set_fg_rgb(phone, theme_text_primary());
    put_clipped(phone, info_row + 2, CONTENT_COL, width, track ? track : "Unknown");

    /* Collection name */
    const char *collection = mp3_service_current_collection_name();
    if (collection && collection[0] != '\0') {
        ncplane_set_fg_rgb(phone, theme_text_muted());
        put_clipped(phone, info_row + 3, CONTENT_COL, width, collection);
    }

    /* Category name */
    const char *category = mp3_service_current_category_name();
    ncplane_set_fg_rgb(phone, theme_text_muted());
    put_clipped(phone, info_row + 4, CONTENT_COL, width, category ? category : "");

    /* Progress bar */
    int bar_row = info_row + 6;
    if (bar_row < footer - 3) {
        int bar_width = width - 2;
        if (bar_width > 0) {
            int meter_width = bar_width;
            if (meter_width < 1) meter_width = 1;
            int fill = (int)(elapsed % 240) * meter_width / 240;
            if (fill > meter_width) fill = meter_width;

            ncplane_set_fg_rgb(phone, theme_text_primary());
            ncplane_set_bg_rgb(phone, theme_bg());
            for (int x = 0; x < meter_width; x++)
                ncplane_putstr_yx(phone, bar_row, CONTENT_COL + x,
                                  (x < fill) ? "\u2588" : "\u2591");
        }
    }

    /* Volume */
    int vol_row = bar_row + 2;
    if (vol_row < footer - 1) {
        int vol = mp3_service_get_volume();
        char vol_str[32];
        snprintf(vol_str, sizeof(vol_str), "VOL: %d%%", vol);
        ghost_text(phone, vol_row, CONTENT_COL, theme_text_muted(), vol_str);
    }

    /* Visualizer */
    int viz_row = vol_row + 1;
    if (viz_row < footer - 1) {
        unsigned char levels[16] = {0};
        size_t bins = mp3_service_get_visualizer(levels, 16);
        if (bins == 0) bins = 16;
        char meter[64];
        int p = 0;
        for (int i = 0; i < 10 && p < (int)sizeof(meter) - 2; i++)
            meter[p++] = (levels[i % bins] >= 4) ? '|' : '.';
        meter[p] = '\0';
        ghost_text(phone, viz_row, CONTENT_COL, theme_text_muted(), meter);
    }

    ghost_text(phone, footer - 1, CONTENT_COL, theme_text_muted(),
               "\u25C0:Prev \u25B6:Next \u25B2\u25BC:Vol");
    ghost_softkeys(phone, "[Back]", "[Stop]");
}

/* ── Main draw dispatcher ────────────────────────────────────────────── */
void screen_mp3_draw(struct ncplane *phone) {
    unsigned rows, cols;
    ncplane_dim_yx(phone, &rows, &cols);

    /* If music was playing and user re-enters, show NOW_PLAYING */
    mp3_playback_state st = mp3_service_get_state();
    if (st != MP3_STOPPED && mode != MP3_MODE_NOW_PLAYING) {
        /* Highlight current track if browsing */
    }

    switch (mode) {
        case MP3_MODE_CATEGORIES:  draw_categories(phone, rows, cols);  break;
        case MP3_MODE_COLLECTIONS: draw_collections(phone, rows, cols); break;
        case MP3_MODE_TRACKS:      draw_tracks(phone, rows, cols);      break;
        case MP3_MODE_NOW_PLAYING: draw_now_playing(phone, rows, cols);  break;
    }
}

/* ── Input handling ──────────────────────────────────────────────────── */
screen_id screen_mp3_input(uint32_t key) {

    /* ── NOW PLAYING ──────────────────────────────────────────────── */
    if (mode == MP3_MODE_NOW_PLAYING) {
        switch (key) {
            case NCKEY_ENTER:
            case '\n':
                /* Center key = toggle play/pause */
                if (mp3_service_get_state() == MP3_PLAYING)
                    mp3_service_pause();
                else if (mp3_service_get_state() == MP3_PAUSED)
                    mp3_service_resume();
                return SCREEN_MP3;
            case NCKEY_LEFT:
                /* Previous track */
                mp3_service_prev();
                return SCREEN_MP3;
            case NCKEY_RIGHT:
                /* Next track */
                mp3_service_next();
                return SCREEN_MP3;
            case NCKEY_UP: {
                /* Volume up */
                int vol = mp3_service_get_volume();
                mp3_service_set_volume(vol + 5);
                return SCREEN_MP3;
            }
            case NCKEY_DOWN: {
                /* Volume down */
                int vol = mp3_service_get_volume();
                mp3_service_set_volume(vol - 5);
                return SCREEN_MP3;
            }
            case 'q':
            case 'Q':
                /* Back to track list (music continues) */
                if (playing_direct)
                    mode = MP3_MODE_COLLECTIONS;
                else
                    mode = MP3_MODE_TRACKS;
                return SCREEN_MP3;
            case 'e':
            case 'E':
                /* Stop playback */
                mp3_service_stop();
                if (playing_direct)
                    mode = MP3_MODE_COLLECTIONS;
                else
                    mode = MP3_MODE_TRACKS;
                return SCREEN_MP3;
            default:
                return SCREEN_MP3;
        }
    }

    /* ── TRACKS list ──────────────────────────────────────────────── */
    if (mode == MP3_MODE_TRACKS) {
        size_t count = mp3_service_track_count((size_t)sel_cat, (size_t)sel_col);
        switch (key) {
            case NCKEY_UP:
                if (sel_track > 0) sel_track--;
                return SCREEN_MP3;
            case NCKEY_DOWN:
                if (sel_track < (int)count - 1) sel_track++;
                return SCREEN_MP3;
            case NCKEY_ENTER:
            case '\n':
                /* Play selected track */
                if (count > 0) {
                    mp3_service_play((size_t)sel_cat, (size_t)sel_col, (size_t)sel_track, 0);
                    playing_direct = 0;
                    mode = MP3_MODE_NOW_PLAYING;
                }
                return SCREEN_MP3;
            case 'q':
            case 'Q':
                /* Back to collections */
                mode = MP3_MODE_COLLECTIONS;
                sel_track = 0;
                scroll_track = 0;
                return SCREEN_MP3;
            case 'e':
            case 'E':
                /* RSK = back to main menu */
                return SCREEN_HOME;
            default:
                return SCREEN_MP3;
        }
    }

    /* ── COLLECTIONS list ─────────────────────────────────────────── */
    if (mode == MP3_MODE_COLLECTIONS) {
        const Mp3Category *cat = mp3_service_get_category((size_t)sel_cat);
        if (!cat) { mode = MP3_MODE_CATEGORIES; return SCREEN_MP3; }

        size_t col_count = cat->collection_count;
        size_t dir_count = cat->direct_track_count;
        size_t total = col_count + dir_count;

        switch (key) {
            case NCKEY_UP:
                if (sel_col > 0) sel_col--;
                return SCREEN_MP3;
            case NCKEY_DOWN:
                if (sel_col < (int)total - 1) sel_col++;
                return SCREEN_MP3;
            case NCKEY_ENTER:
            case '\n':
                if (total > 0) {
                    if (sel_col < (int)col_count) {
                        /* Open collection -> tracks */
                        sel_track = 0;
                        scroll_track = 0;
                        mode = MP3_MODE_TRACKS;
                    } else {
                        /* Direct track -> play */
                        size_t tidx = (size_t)(sel_col - (int)col_count);
                        mp3_service_play((size_t)sel_cat, 0, tidx, 1);
                        playing_direct = 1;
                        mode = MP3_MODE_NOW_PLAYING;
                    }
                }
                return SCREEN_MP3;
            case 'q':
            case 'Q':
                /* Back to categories */
                mode = MP3_MODE_CATEGORIES;
                sel_col = 0;
                scroll_col = 0;
                return SCREEN_MP3;
            case 'e':
            case 'E':
                /* RSK = back to main menu */
                return SCREEN_HOME;
            default:
                return SCREEN_MP3;
        }
    }

    /* ── CATEGORIES list ──────────────────────────────────────────── */
    {
        size_t count = mp3_service_category_count();
        switch (key) {
            case NCKEY_UP:
                if (sel_cat > 0) sel_cat--;
                return SCREEN_MP3;
            case NCKEY_DOWN:
                if (sel_cat < (int)count - 1) sel_cat++;
                return SCREEN_MP3;
            case NCKEY_ENTER:
            case '\n':
                /* Open category */
                if (count > 0) {
                    sel_col = 0;
                    scroll_col = 0;
                    mode = MP3_MODE_COLLECTIONS;
                }
                return SCREEN_MP3;
            case 'q':
            case 'Q':
                /* LSK = back to main menu */
                return SCREEN_HOME;
            case 'e':
            case 'E':
                /* RSK = back to main menu */
                return SCREEN_HOME;
            default:
                return SCREEN_MP3;
        }
    }
}
