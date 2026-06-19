#include <notcurses/notcurses.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#include "config.h"
#include "draw_utils.h"
#include "bh_skin.h"
#include "ui.h"
#include "services/theme_service.h"
#include "services/voice_memo_service.h"
#include "services/multitap_service.h"

/* ── Voice Memo screen modes ─────────────────────────────────────────────
 *
 * LIST mode:
 *   up/down    = scroll through memos
 *   left       = delete selected memo
 *   right      = rename selected memo
 *   center     = play/pause memo
 *   LSK (q)    = back to menu
 *   RSK (e)    = record new memo
 *
 * RECORDING mode:
 *   LSK (q)    = back (stops recording)
 *   RSK (e)    = stop recording -> enters NAME_PROMPT
 *
 * NAME_PROMPT mode (after recording):
 *   multi-tap text entry for name
 *   default name is date-based, highlighted for easy replacement
 *   LSK (q)    = skip naming (use default)
 *   RSK (e)    = save name
 *
 * PLAYBACK mode:
 *   left/right = seek backward/forward in recording
 *   up/down    = speed up/slow down
 *   LSK (q)    = back from recording
 *   RSK (e)    = delete recording
 *
 * RENAME_PROMPT mode:
 *   multi-tap text entry for new name
 *   LSK (q)    = cancel
 *   RSK (e)    = save
 * ────────────────────────────────────────────────────────────────────── */

typedef enum {
    VM_MODE_LIST,
    VM_MODE_RECORDING,
    VM_MODE_NAME_PROMPT,
    VM_MODE_PLAYBACK,
    VM_MODE_RENAME_PROMPT,
} vm_mode_t;

static vm_mode_t ui_mode = VM_MODE_LIST;
static int selected = 0;
static int list_scroll = 0;
static int anim_tick = 0;

/* Delete confirmation (list mode) */
static int delete_prompt = 0;

/* Discard confirmation (recording mode) */
static int s_discard_prompt = 0;

/* Name/rename prompt */
static char name_buf[128];
static int name_cursor = 0;
static multitap_state s_name_multitap;
static int name_multitap_ready = 0;
static int name_pristine = 0;

/* Seek step in ms */
#define SEEK_STEP_MS 5000

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

static void memo_display_name(const char *filename, char *out, size_t out_size) {
    if (!out || out_size == 0) return;
    out[0] = '\0';
    if (!filename) return;
    snprintf(out, out_size, "%s", filename);
    char *dot = strrchr(out, '.');
    if (dot && strcmp(dot, ".wav") == 0) *dot = '\0';
    for (char *p = out; *p; p++) {
        if (*p == '_') *p = ' ';
    }
}

static void format_time_ms(int ms, char *buf, size_t buf_size) {
    int sec = ms / 1000;
    int min = sec / 60;
    sec %= 60;
    snprintf(buf, buf_size, "%02d:%02d", min, sec);
}

static void ensure_multitap(void) {
    if (!name_multitap_ready) {
        multitap_init(&s_name_multitap);
        name_multitap_ready = 1;
    }
}

/* ── Recording screen ──────────────────────────────────────────────────── */
static void draw_recording(struct ncplane *phone, unsigned rows, unsigned cols) {
    int footer = (int)rows - FOOTER_ROW_OFFSET;
    int width = INNER_WIDTH(cols);
    int elapsed = voice_memo_service_elapsed_ms();
    char tbuf[16];
    format_time_ms(elapsed, tbuf, sizeof(tbuf));

    int dot_visible = ((anim_tick / 4) % 2 == 0);
    ncplane_set_fg_rgb(phone, theme_border());
    ncplane_set_bg_rgb(phone, theme_bg());
    ncplane_putstr_yx(phone, CONTENT_START_ROW, CONTENT_COL,
                      dot_visible ? "RECORDING  \u25CF" : "RECORDING");

    ncplane_set_fg_rgb(phone, theme_text_primary());
    ncplane_putstr_yx(phone, CONTENT_START_ROW + 2, CONTENT_COL, tbuf);

    int bar_row = CONTENT_START_ROW + 4;
    int bar_w = width > 16 ? 16 : width;
    int fill = (elapsed / 1000) % (bar_w + 1);
    if (bar_row < footer) {
        for (int i = 0; i < bar_w; i++)
            ncplane_putstr_yx(phone, bar_row, CONTENT_COL + i, (i < fill) ? "\u2588" : "\u2591");
    }

    if (s_discard_prompt) {
        ghost_confirm_popup(phone, "DISCARD RECORDING?", 0);
    }
    if (s_discard_prompt)
        ghost_softkeys(phone, "[Keep]", "");
    else
        ghost_softkeys(phone, "[Discard?]", "[Save]");
}

/* ── Name prompt (after recording or for rename) ───────────────────────── */
static void draw_name_prompt(struct ncplane *phone, unsigned rows, unsigned cols, int is_rename) {
    int width = INNER_WIDTH(cols);
    int w = width + 2;
    int h = VOICE_MEMO_NAME_POPUP_HEIGHT;
    int top = ((int)rows - h) / 2;
    int left = ((int)cols - w) / 2;
    if (top < UI_POPUP_MIN_TOP) top = UI_POPUP_MIN_TOP;
    if (left < UI_POPUP_MIN_LEFT) left = UI_POPUP_MIN_LEFT;

    ghost_fill_rect(phone, top, left, h, w, ' ', theme_text_primary(), theme_bg());
    ghost_text(phone, top + UI_POPUP_TITLE_ROW_OFFSET, left + UI_POPUP_TEXT_INSET_X, theme_text_primary(),
               is_rename ? "RENAME MEMO:" : "NAME MEMO:");
    ghost_text(phone, top + UI_POPUP_INPUT_ROW_OFFSET, left + UI_POPUP_TEXT_INSET_X,
               theme_text_primary(), name_buf);

    int cursor_col = left + UI_POPUP_TEXT_INSET_X + name_cursor;
    if (cursor_col < left + w - 1)
        ghost_text(phone, top + UI_POPUP_INPUT_ROW_OFFSET, cursor_col, theme_border(), "_");

    ghost_text(phone, top + UI_POPUP_HINT_ROW_OFFSET, left + UI_POPUP_TEXT_INSET_X,
               theme_text_muted(), "2-9:ABC #:Case *:Punct");
    ghost_softkeys(phone, "[Skip]", "[Save]");
}

/* ── Playback screen ───────────────────────────────────────────────────── */
static void draw_playback(struct ncplane *phone, unsigned rows, unsigned cols) {
    int footer = (int)rows - FOOTER_ROW_OFFSET;
    int width = INNER_WIDTH(cols);
    VMState st = voice_memo_service_state();
    int elapsed = voice_memo_service_elapsed_ms();
    int total = voice_memo_service_total_ms();
    const VoiceMemo *cur = voice_memo_service_current();

    ncplane_set_fg_rgb(phone, theme_text_primary());
    ncplane_set_bg_rgb(phone, theme_bg());

    const char *state_str = (st == VM_PLAYING) ? "PLAYBACK" : "PAUSED";
    const char *icon = (st == VM_PLAYING) ? "\u25B6" : "\u258C\u258C";
    char header[64];
    snprintf(header, sizeof(header), "%s  %s", state_str, icon);
    ncplane_putstr_yx(phone, CONTENT_START_ROW, CONTENT_COL, header);

    /* Track name */
    if (cur) {
        char dname[256];
        memo_display_name(cur->filename, dname, sizeof(dname));
        ncplane_set_fg_rgb(phone, theme_text_primary());
        put_clipped(phone, CONTENT_START_ROW + 2, CONTENT_COL, width, dname[0] ? dname : "Unknown");
    }

    /* Time display */
    char time_line[64];
    char elapsed_str[16], total_str[16];
    format_time_ms(elapsed, elapsed_str, sizeof(elapsed_str));
    format_time_ms(total, total_str, sizeof(total_str));
    snprintf(time_line, sizeof(time_line), "%s / %s", elapsed_str, total_str);
    ghost_text(phone, CONTENT_START_ROW + 4, CONTENT_COL, theme_text_primary(), time_line);

    /* Progress bar */
    int bar_row = CONTENT_START_ROW + 6;
    if (bar_row < footer - 3) {
        int bar_w = width > 20 ? 20 : width;
        int fill = (total > 0) ? (elapsed * bar_w / total) : 0;
        if (fill > bar_w) fill = bar_w;
        for (int i = 0; i < bar_w; i++)
            ncplane_putstr_yx(phone, bar_row, CONTENT_COL + i, (i < fill) ? "\u2588" : "\u2591");
    }

    /* Speed indicator */
    char speed_str[32];
    snprintf(speed_str, sizeof(speed_str), "Speed: %d%%", voice_memo_service_get_speed_percent());
    ghost_text(phone, CONTENT_START_ROW + 8, CONTENT_COL, theme_text_muted(), speed_str);

    ghost_softkeys(phone, NULL, NULL);
    ghost_text(phone, (int)rows - 3, CONTENT_COL, theme_text_muted(),
               "D/E:Seek  W/S:Speed  Q:Back");
}

/* ── List mode ─────────────────────────────────────────────────────────── */
static void draw_list(struct ncplane *phone, unsigned rows, unsigned cols) {
    int footer = (int)rows - FOOTER_ROW_OFFSET;
    int width = INNER_WIDTH(cols);

    size_t count = 0;
    const VoiceMemo **list = voice_memo_service_list_all(&count);

    if (count == 0) {
        int mid = (CONTENT_START_ROW + footer) / 2;
        ncplane_set_fg_rgb(phone, theme_text_muted());
        ncplane_putstr_yx(phone, mid, CONTENT_COL, "No voice memos yet");
        ncplane_putstr_yx(phone, mid + 1, CONTENT_COL, "Press E to record");
        ghost_softkeys(phone, NULL, NULL);
        return;
    }

    if (selected < 0) selected = 0;
    if (count > 0 && selected >= (int)count) selected = (int)count - 1;

    int list_start = CONTENT_START_ROW;
    int max_rows = footer - list_start - 2;
    if (max_rows < 1) max_rows = 1;

    if (selected < list_scroll) list_scroll = selected;
    if (selected >= list_scroll + max_rows) list_scroll = selected - max_rows + 1;
    if (list_scroll < 0) list_scroll = 0;

    for (int i = 0; i < max_rows; i++) {
        int index = list_scroll + i;
        if (index >= (int)count) break;

        const VoiceMemo *m = list[index];
        int row = list_start + i;
        int sel = (index == selected);

        char dbuf[16];
        format_time_ms(m->duration_ms, dbuf, sizeof(dbuf));

        char dname[256];
        memo_display_name(m->filename, dname, sizeof(dname));
        int maxlbl = width - 10;
        if (maxlbl > 0 && (int)strlen(dname) > maxlbl) dname[maxlbl] = '\0';

        bh_list_item(phone, row, CONTENT_COL, width, dname[0] ? dname : "?", dbuf, sel, index);
    }

    ghost_softkeys(phone, NULL, NULL);
    ghost_text(phone, (int)rows - 3, CONTENT_COL, theme_text_muted(),
               "A:Play  D:Delete  E:Record");
    if (delete_prompt) ghost_confirm_popup(phone, "DELETE MEMO?", 0);
}

void screen_voice_memo_draw(struct ncplane *phone) {
    unsigned rows, cols;
    ncplane_dim_yx(phone, &rows, &cols);
    anim_tick++;

    switch (ui_mode) {
        case VM_MODE_LIST:
            draw_list(phone, rows, cols);
            break;
        case VM_MODE_RECORDING:
            draw_recording(phone, rows, cols);
            break;
        case VM_MODE_NAME_PROMPT:
            draw_name_prompt(phone, rows, cols, 0);
            break;
        case VM_MODE_PLAYBACK:
            draw_playback(phone, rows, cols);
            break;
        case VM_MODE_RENAME_PROMPT:
            draw_name_prompt(phone, rows, cols, 1);
            break;
    }
}

/* Helper: start naming prompt with default date-based name */
static void start_name_prompt(void) {
    ensure_multitap();
    /* Generate a default date-based name */
    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    strftime(name_buf, sizeof(name_buf), "Memo %Y-%m-%d %H:%M", &tmv);
    name_cursor = (int)strlen(name_buf);
    name_pristine = 1;
    multitap_reset(&s_name_multitap);
    ui_mode = VM_MODE_NAME_PROMPT;
}

/* Helper: start rename prompt with existing filename */
static void start_rename_prompt(void) {
    ensure_multitap();
    size_t count = 0;
    const VoiceMemo **list = voice_memo_service_list_all(&count);
    if (count == 0 || !list || selected >= (int)count) return;

    /* Strip .wav extension for display */
    const char *fn = list[selected]->filename;
    memo_display_name(fn, name_buf, sizeof(name_buf));

    name_cursor = (int)strlen(name_buf);
    name_pristine = 0;
    multitap_reset(&s_name_multitap);
    ui_mode = VM_MODE_RENAME_PROMPT;
}

int screen_voice_memo_is_text_entry_mode(void) {
    return (ui_mode == VM_MODE_NAME_PROMPT || ui_mode == VM_MODE_RENAME_PROMPT) ? 1 : 0;
}

/* Handle name/rename prompt input */
static screen_id handle_name_input(uint32_t key, int is_rename) {
    ensure_multitap();

    switch (key) {
        case KEY_SOFT_RIGHT_ACTION:
        case NCKEY_ENTER:
        case '\n': {
            multitap_reset(&s_name_multitap);
            if (is_rename) {
                /* Rename the selected memo */
                size_t count = 0;
                const VoiceMemo **list = voice_memo_service_list_all(&count);
                if (count > 0 && list && selected < (int)count && name_buf[0] != '\0') {
                    voice_memo_service_rename(list[selected]->filename, name_buf);
                }
                ui_mode = VM_MODE_LIST;
            } else {
                /* Name the just-recorded memo (it's at index 0) */
                size_t count = 0;
                const VoiceMemo **list = voice_memo_service_list_all(&count);
                if (count > 0 && list && name_buf[0] != '\0') {
                    voice_memo_service_rename(list[0]->filename, name_buf);
                }
                ui_mode = VM_MODE_LIST;
                selected = 0;
                list_scroll = 0;
            }
            return SCREEN_VOICE_MEMO;
        }
        case KEY_SOFT_LEFT_ACTION:
            multitap_reset(&s_name_multitap);
            ui_mode = VM_MODE_LIST;
            if (!is_rename) { selected = 0; list_scroll = 0; }
            return SCREEN_VOICE_MEMO;
        case NCKEY_BACKSPACE:
        case 127:
            multitap_backspace(&s_name_multitap, 0, name_buf);
            name_cursor = (int)strlen(name_buf);
            return SCREEN_VOICE_MEMO;
        case '#':
            multitap_toggle_case(&s_name_multitap);
            return SCREEN_VOICE_MEMO;
        default:
            if ((key >= '0' && key <= '9') || key == '*') {
                if (name_pristine) {
                    name_buf[0] = '\0';
                    name_pristine = 0;
                }
                multitap_apply_key(&s_name_multitap, key, 0,
                                   name_buf, sizeof(name_buf));
                name_cursor = (int)strlen(name_buf);
                return SCREEN_VOICE_MEMO;
            }
            if (key >= 32 && key <= 126) {
                if (name_pristine) {
                    name_buf[0] = '\0';
                    name_pristine = 0;
                }
                multitap_reset(&s_name_multitap);
                size_t len = strlen(name_buf);
                if (len + 1 < sizeof(name_buf)) {
                    name_buf[len] = (char)key;
                    name_buf[len + 1] = '\0';
                }
                name_cursor = (int)strlen(name_buf);
                return SCREEN_VOICE_MEMO;
            }
            return SCREEN_VOICE_MEMO;
    }
}

screen_id screen_voice_memo_input(uint32_t key) {
    size_t count = 0;
    const VoiceMemo **list = voice_memo_service_list_all(&count);
    VMState st = voice_memo_service_state();

    /* ── NAME_PROMPT mode ─────────────────────────────────────────── */
    if (ui_mode == VM_MODE_NAME_PROMPT) {
        return handle_name_input(key, 0);
    }

    /* ── RENAME_PROMPT mode ───────────────────────────────────────── */
    if (ui_mode == VM_MODE_RENAME_PROMPT) {
        return handle_name_input(key, 1);
    }

    /* ── PLAYBACK mode ────────────────────────────────────────────── */
    if (ui_mode == VM_MODE_PLAYBACK) {
        /* Check if playback ended naturally */
        if (st == VM_IDLE) {
            ui_mode = VM_MODE_LIST;
            return SCREEN_VOICE_MEMO;
        }

        switch (key) {
            case NCKEY_LEFT:
                /* Seek backward */
                voice_memo_service_seek_relative(-SEEK_STEP_MS);
                return SCREEN_VOICE_MEMO;
            case NCKEY_RIGHT:
                /* Seek forward */
                voice_memo_service_seek_relative(SEEK_STEP_MS);
                return SCREEN_VOICE_MEMO;
            case NCKEY_UP:
                /* Speed up */
                voice_memo_service_set_speed_percent(voice_memo_service_get_speed_percent() + 25);
                return SCREEN_VOICE_MEMO;
            case NCKEY_DOWN:
                /* Slow down */
                voice_memo_service_set_speed_percent(voice_memo_service_get_speed_percent() - 25);
                return SCREEN_VOICE_MEMO;
            case NCKEY_ENTER:
            case '\n':
                /* Toggle play/pause */
                if (st == VM_PLAYING) voice_memo_service_play_pause();
                else if (st == VM_PAUSED) voice_memo_service_play_resume();
                return SCREEN_VOICE_MEMO;
            case KEY_SOFT_LEFT_ACTION:
                /* Back from playback */
                voice_memo_service_play_stop();
                ui_mode = VM_MODE_LIST;
                return SCREEN_VOICE_MEMO;
            case KEY_SOFT_RIGHT_ACTION:
                /* Delete while playing */
                voice_memo_service_play_stop();
                if (count > 0 && list && selected < (int)count) {
                    voice_memo_service_delete(list[selected]->filename);
                    voice_memo_service_list_all(&count);
                    if (selected >= (int)count && selected > 0) selected--;
                }
                ui_mode = VM_MODE_LIST;
                return SCREEN_VOICE_MEMO;
            default:
                return SCREEN_VOICE_MEMO;
        }
    }

    /* ── RECORDING mode ───────────────────────────────────────────── */
    if (ui_mode == VM_MODE_RECORDING || st == VM_RECORDING) {
        if (s_discard_prompt) {
            switch (key) {
                case NCKEY_ENTER:
                case '\n':
                    /* A=Yes: discard. */
                    voice_memo_service_record_stop(NULL);
                    s_discard_prompt = 0;
                    ui_mode = VM_MODE_LIST;
                    selected = 0;
                    list_scroll = 0;
                    return SCREEN_VOICE_MEMO;
                case KEY_SOFT_LEFT_ACTION:
                    /* Q=No: keep recording. */
                    s_discard_prompt = 0;
                    return SCREEN_VOICE_MEMO;
                default:
                    return SCREEN_VOICE_MEMO;
            }
        }
        switch (key) {
            case KEY_SOFT_RIGHT_ACTION:
            case NCKEY_ENTER:
            case '\n':
                /* A/E: stop recording and save (go to name prompt). */
                voice_memo_service_record_stop(NULL);
                start_name_prompt();
                return SCREEN_VOICE_MEMO;
            case NCKEY_LEFT:
            case KEY_SOFT_LEFT_ACTION:
                /* D/Q: show discard confirmation. */
                s_discard_prompt = 1;
                return SCREEN_VOICE_MEMO;
            default:
                return SCREEN_VOICE_MEMO;
        }
    }

    /* ── LIST mode ────────────────────────────────────────────────── */
    if (delete_prompt) {
        switch (key) {
            case NCKEY_ENTER:
            case '\n':
                /* A=Yes: delete. */
                if (count > 0 && list && selected < (int)count) {
                    voice_memo_service_delete(list[selected]->filename);
                    voice_memo_service_list_all(&count);
                    if (selected >= (int)count && selected > 0) selected--;
                }
                delete_prompt = 0;
                return SCREEN_VOICE_MEMO;
            case KEY_SOFT_LEFT_ACTION:
                /* Q=No: cancel. */
                delete_prompt = 0;
                return SCREEN_VOICE_MEMO;
            default:
                return SCREEN_VOICE_MEMO;
        }
    }

    switch (key) {
        case NCKEY_UP:
            if (selected > 0) selected--;
            return SCREEN_VOICE_MEMO;
        case NCKEY_DOWN:
            if (selected < (int)count - 1) selected++;
            return SCREEN_VOICE_MEMO;
        case NCKEY_ENTER:
        case '\n':
            /* A: play selected memo. */
            if (count > 0 && list) {
                voice_memo_service_play_start(list[selected]->filename);
                ui_mode = VM_MODE_PLAYBACK;
            }
            return SCREEN_VOICE_MEMO;
        case NCKEY_LEFT:
            /* D: delete confirmation. */
            if (count > 0 && list) {
                delete_prompt = 1;
            }
            return SCREEN_VOICE_MEMO;
        case KEY_SOFT_RIGHT_ACTION:
            /* E: record new memo. */
            s_discard_prompt = 0;
            voice_memo_service_record_start();
            ui_mode = VM_MODE_RECORDING;
            return SCREEN_VOICE_MEMO;
        case KEY_SOFT_LEFT_ACTION:
            /* Q: back to home. */
            delete_prompt = 0;
            return SCREEN_HOME;
        default:
            return SCREEN_VOICE_MEMO;
    }
}
