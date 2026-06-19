#include <notcurses/notcurses.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "draw_utils.h"
#include "bh_skin.h"
#include "ui.h"
#include "services/mp3_service.h"
#include "services/theme_service.h"

/* clip a label to fit then render it as a themed list row */
static void list_row(struct ncplane *p, int row, int width, int idx, int sel,
                     const char *name, const char *meta) {
    char nm[64];
    snprintf(nm, sizeof(nm), "%s", name ? name : "?");
    int maxlbl = width - 8;
    if (maxlbl > 0 && (int)strlen(nm) > maxlbl) { nm[maxlbl] = '\0'; }
    bh_list_item(p, row, CONTENT_COL, width, nm, meta ? meta : "", sel, idx);
}

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

    /* Show now-playing indicator if music is active */
    mp3_playback_state st = mp3_service_get_state();
    if (st == MP3_PLAYING || st == MP3_PAUSED) {
        const char *tname = mp3_service_current_track_name();
        char now_line[64];
        snprintf(now_line, sizeof(now_line), "%s %s",
                 (st == MP3_PLAYING) ? "\u25B6" : "\u258C\u258C",
                 tname ? tname : "");
        ghost_text(phone, CONTENT_START_ROW, CONTENT_COL, theme_text_muted(), now_line);
    }

    if (count == 0) {
        int mid = (CONTENT_START_ROW + footer) / 2;
        char music_hint[96];
        snprintf(music_hint, sizeof(music_hint), "Place files in %s/", APP_PATH_MUSIC_DIR);
        ghost_text(phone, mid - 1, CONTENT_COL, theme_text_muted(), "\u266B  \u266A  \u266B");
        ghost_text(phone, mid + 1, CONTENT_COL, theme_text_muted(), "No music found");
        ghost_text(phone, mid + 2, CONTENT_COL, theme_text_muted(), music_hint);
        ghost_softkeys(phone, NULL, NULL);
        return;
    }

    if (sel_cat < 0) sel_cat = 0;
    if (sel_cat >= (int)count) sel_cat = (int)count - 1;

    int list_start = CONTENT_START_ROW + 1;
    int max_rows = footer - list_start - 2;
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
        list_row(phone, row, width, idx, sel, cat->name ? cat->name : "?", "");
    }

    ghost_softkeys(phone, NULL, NULL);
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

    /* Build combined list: collections + direct tracks */
    size_t col_count = cat->collection_count;
    size_t dir_count = cat->direct_track_count;
    size_t total = col_count + dir_count;

    if (total == 0) {
        int mid = (CONTENT_START_ROW + footer) / 2;
        ghost_text(phone, mid, CONTENT_COL, theme_text_muted(), "No tracks");
        ghost_softkeys(phone, NULL, NULL);
        return;
    }

    if (sel_col < 0) sel_col = 0;
    if (sel_col >= (int)total) sel_col = (int)total - 1;

    int list_start = CONTENT_START_ROW + 1;
    int max_rows = footer - list_start - 2;
    if (max_rows < 1) max_rows = 1;

    if (sel_col < scroll_col) scroll_col = sel_col;
    if (sel_col >= scroll_col + max_rows) scroll_col = sel_col - max_rows + 1;
    if (scroll_col < 0) scroll_col = 0;

    for (int i = 0; i < max_rows; i++) {
        int idx = scroll_col + i;
        if (idx >= (int)total) break;

        int row = list_start + i;
        int sel = (idx == sel_col);

        if (idx < (int)col_count) {
            /* Collection: name + track-count meta on the right */
            const Mp3Collection *c = &cat->collections[idx];
            char meta[16];
            snprintf(meta, sizeof(meta), "%zu", c->track_count);
            list_row(phone, row, width, idx, sel, c->name ? c->name : "?", meta);
        } else {
            /* Direct track */
            size_t tidx = (size_t)(idx - (int)col_count);
            const Mp3Track *t = &cat->direct_tracks[tidx];
            list_row(phone, row, width, idx, sel, t->title ? t->title : "?", "");
        }
    }

    ghost_softkeys(phone, NULL, NULL);
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

    size_t count = col->track_count;
    if (count == 0) {
        int mid = (CONTENT_START_ROW + footer) / 2;
        ghost_text(phone, mid, CONTENT_COL, theme_text_muted(), "No tracks");
        ghost_softkeys(phone, NULL, NULL);
        return;
    }

    if (sel_track < 0) sel_track = 0;
    if (sel_track >= (int)count) sel_track = (int)count - 1;

    int list_start = CONTENT_START_ROW + 1;
    int max_rows = footer - list_start - 2;
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
        /* the highlighted track shows a play glyph while audio is playing */
        const char *meta = (sel && mp3_service_get_state() == MP3_PLAYING) ? "\u25B6" : "";
        list_row(phone, row, width, idx, sel, t->title ? t->title : "?", meta);
    }

    ghost_softkeys(phone, NULL, NULL);
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
    unsigned total = mp3_service_get_total_duration();
    int width = INNER_WIDTH(cols);
    const bh_theme_t *th = theme_active();

    /* Row 3 : NOW PLAYING (dim, letter-spaced) */
    ghost_text(phone, CONTENT_START_ROW, CONTENT_COL, th->dim, "N O W   P L A Y I N G");

    /* Row 4 : track title (bright, truncated) */
    const char *track = mp3_service_current_track_name();
    ncplane_set_fg_rgb(phone, th->bright);
    ncplane_set_bg_rgb(phone, th->bg);
    put_clipped(phone, CONTENT_START_ROW + 1, CONTENT_COL, width, track ? track : "Unknown");

    /* Row 5 : COLLECTION \u00B7 CATEGORY (mid) */
    const char *collection = mp3_service_current_collection_name();
    const char *category   = mp3_service_current_category_name();
    char meta[96];
    if (collection && collection[0])
        snprintf(meta, sizeof(meta), "%s \u00B7 %s", collection, category ? category : "");
    else
        snprintf(meta, sizeof(meta), "%s", category ? category : "");
    ncplane_set_fg_rgb(phone, th->mid);
    ncplane_set_bg_rgb(phone, th->bg);
    put_clipped(phone, CONTENT_START_ROW + 2, CONTENT_COL, width, meta);

    /* Row 8 : 10-cell progress bar (thermal ramps cold\u2192hot) */
    int bar_row = CONTENT_START_ROW + 5;
    int cells = 10;
    int fill = (total > 0) ? (int)((elapsed * cells) / total) : 0;
    if (fill > cells) fill = cells;
    static const uint32_t ramp[10] = {
        0x378ADD,0x378ADD,0x97C459,0x97C459,0xC0DD97,
        0xEF9F27,0xEF9F27,0xE24B4A,0xE24B4A,0xE24B4A };
    ncplane_set_bg_rgb(phone, th->bg);
    for (int x = 0; x < cells; x++) {
        uint32_t fg = (x < fill) ? (th->is_heat ? ramp[x] : th->bright) : th->deep;
        ncplane_set_fg_rgb(phone, fg);
        ncplane_putstr_yx(phone, bar_row, CONTENT_COL + x, "\u2588");
    }

    /* Row 9 : elapsed (left) / total (right), dim */
    char tl[16], tr[16];
    snprintf(tl, sizeof(tl), "%u:%02u", elapsed / 60, elapsed % 60);
    snprintf(tr, sizeof(tr), "%u:%02u", total / 60, total % 60);
    ghost_text(phone, bar_row + 1, CONTENT_COL, th->dim, tl);
    ghost_text(phone, bar_row + 1, CONTENT_COL + width - (int)strlen(tr), th->dim, tr);

    /* Row 11 : play/pause control (left) \u00B7 VOL meter (right) */
    int ctl_row = bar_row + 3;
    const char *ctl = (st == MP3_PLAYING) ? "\u2016 PAUSE" : "\u25B6 PLAY";
    ghost_text(phone, ctl_row, CONTENT_COL, th->bright, ctl);
    int vol = mp3_service_get_volume();
    int vbars = (vol + 12) / 25; if (vbars > 4) vbars = 4; if (vbars < 0) vbars = 0;
    char vm[24] = "VOL ";
    for (int i = 0; i < 4; i++) strcat(vm, i < vbars ? "\u25AE" : "\u25AF");
    ghost_text(phone, ctl_row, CONTENT_COL + width - (int)strlen("VOL \u25AE\u25AE\u25AE\u25AF"), th->mid, vm);

    ghost_softkeys(phone, NULL, NULL);
    ghost_text(phone, (int)rows - 3, CONTENT_COL, theme_active()->dim,
               "Q:Prev  A:Play  E:Next  D:Stop");
}

/* ── Main draw dispatcher ────────────────────────────────────────────── */
void screen_mp3_draw(struct ncplane *phone) {
    unsigned rows, cols;
    ncplane_dim_yx(phone, &rows, &cols);

    /* If music is already playing, reopen player screen */
    mp3_playback_state st = mp3_service_get_state();
    if (st != MP3_STOPPED && mode != MP3_MODE_NOW_PLAYING) {
        mode = MP3_MODE_NOW_PLAYING;
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
                /* A: toggle play/pause. */
                if (mp3_service_get_state() == MP3_PLAYING)
                    mp3_service_pause();
                else if (mp3_service_get_state() == MP3_PAUSED)
                    mp3_service_resume();
                return SCREEN_MP3;
            case NCKEY_UP: {
                /* W: volume up. */
                int vol = mp3_service_get_volume();
                mp3_service_set_volume(vol + 5);
                return SCREEN_MP3;
            }
            case NCKEY_DOWN: {
                /* S: volume down. */
                int vol = mp3_service_get_volume();
                mp3_service_set_volume(vol - 5);
                return SCREEN_MP3;
            }
            case KEY_SOFT_LEFT_ACTION:
                /* Q: previous track. */
                mp3_service_prev();
                return SCREEN_MP3;
            case KEY_SOFT_RIGHT_ACTION:
                /* E: next track. */
                mp3_service_next();
                return SCREEN_MP3;
            case NCKEY_LEFT:
                /* D: stop playback and return to library. */
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
            case KEY_SOFT_LEFT_ACTION:
                /* Back to collections */
                mode = MP3_MODE_COLLECTIONS;
                sel_track = 0;
                scroll_track = 0;
                return SCREEN_MP3;
            case KEY_SOFT_RIGHT_ACTION:
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
            case KEY_SOFT_LEFT_ACTION:
                /* Back to categories */
                mode = MP3_MODE_CATEGORIES;
                sel_col = 0;
                scroll_col = 0;
                return SCREEN_MP3;
            case KEY_SOFT_RIGHT_ACTION:
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
            case KEY_SOFT_LEFT_ACTION:
                /* LSK = back to main menu */
                return SCREEN_HOME;
            case KEY_SOFT_RIGHT_ACTION:
                /* RSK = back to main menu */
                return SCREEN_HOME;
            default:
                return SCREEN_MP3;
        }
    }
}
