#include <notcurses/notcurses.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "draw_utils.h"
#include "bh_skin.h"
#include "services/alarm_service.h"
#include "services/theme_service.h"
#include "ui.h"

/* ── Alarm screen ────────────────────────────────────────────────────────
 *
 * LIST mode:
 *   up/down      = move through alarm list
 *   center       = toggle enable/disable
 *   left         = delete selected alarm
 *   right        = edit selected alarm (time editor)
 *   LSK (q)      = back to main menu
 *   RSK (e)      = create new alarm
 *
 * TIME_EDITOR mode (popup):
 *   left/right   = switch between hours and minutes
 *   up/down      = change value
 *   0-9          = direct 4-digit entry
 *   center/RSK   = confirm
 *   LSK          = cancel
 *
 * Display format:
 *   HH:MM  ON/OFF  Label  [Repeat]
 * ────────────────────────────────────────────────────────────────────── */

static int s_selected = 0;
static int s_scroll = 0;
static int s_seed = 1;

/* Delete confirmation */
static int s_delete_prompt = 0;
static int s_delete_yes = 0;

/* Time editor popup */
static int s_time_prompt = 0;
static int s_time_hour = 7;
static int s_time_minute = 0;
static int s_time_field = 0; /* 0=hour, 1=minute */
static alarm_repeat_t s_time_repeat = ALARM_ONCE;
static int s_edit_existing = 0;
static char s_edit_id[128] = {0};
static int s_digit_len = 0;
static char s_digit_buf[5] = {0};

static const char *repeat_label(alarm_repeat_t r);

static void draw_delete_popup(struct ncplane *phone) {
    ghost_confirm_popup(phone, "DELETE ALARM?", s_delete_yes);
}

static void draw_time_popup(struct ncplane *phone, unsigned rows, unsigned cols) {
    int w = ALARM_TIME_POPUP_WIDTH;
    int h = ALARM_TIME_POPUP_HEIGHT;
    int top = ((int)rows - h) / 2;
    int left = ((int)cols - w) / 2;
    if (top < UI_POPUP_MIN_TOP) top = UI_POPUP_MIN_TOP;
    if (left < ALARM_TIME_POPUP_MIN_LEFT) left = ALARM_TIME_POPUP_MIN_LEFT;

    ghost_fill_rect(phone, top, left, h, w, ' ', theme_text_primary(), theme_bg());
    ghost_text(phone, top + UI_POPUP_TITLE_ROW_OFFSET, left + UI_POPUP_TEXT_INSET_X, theme_text_primary(),
               s_edit_existing ? "EDIT ALARM TIME" : "SET ALARM TIME");

    char timebuf[16];
    snprintf(timebuf, sizeof(timebuf), "%02d:%02d", s_time_hour, s_time_minute);
    ghost_text(phone, top + UI_POPUP_INPUT_ROW_OFFSET, left + UI_POPUP_TEXT_INSET_X,
               theme_text_primary(), timebuf);
    char repeat_buf[32];
    snprintf(repeat_buf, sizeof(repeat_buf), "Repeat: %s", repeat_label(s_time_repeat));
    ghost_text(phone, top + UI_POPUP_INPUT_ROW_OFFSET + 1, left + UI_POPUP_TEXT_INSET_X, theme_text_muted(),
               s_time_field == 0 ? "^^ hour" : "   ^^ minute");
    ghost_text(phone, top + UI_POPUP_HINT_ROW_OFFSET, left + UI_POPUP_TEXT_INSET_X,
               theme_text_muted(), repeat_buf);

    ghost_softkeys(phone, "[Cancel]", "[Save]");
}

static const char *repeat_label(alarm_repeat_t r) {
    switch (r) {
        case ALARM_DAILY:    return "Daily";
        case ALARM_WEEKDAYS: return "Weekdays";
        default:             return "Once";
    }
}

static void start_time_prompt_new(void) {
    s_edit_existing = 0;
    s_edit_id[0] = '\0';
    s_time_hour = 7;
    s_time_minute = 0;
    s_time_field = 0;
    s_time_repeat = ALARM_ONCE;
    s_digit_len = 0;
    s_digit_buf[0] = '\0';
    s_time_prompt = 1;
}

static void start_time_prompt_edit(const Alarm *a) {
    if (!a || !a->id) return;
    s_edit_existing = 1;
    snprintf(s_edit_id, sizeof(s_edit_id), "%s", a->id);
    s_time_hour = a->hour;
    s_time_minute = a->minute;
    s_time_field = 0;
    s_time_repeat = a->repeat;
    s_digit_len = 0;
    s_digit_buf[0] = '\0';
    s_time_prompt = 1;
}

void screen_alarm_draw(struct ncplane *phone) {
    unsigned rows, cols;
    ncplane_dim_yx(phone, &rows, &cols);
    int footer = (int)rows - FOOTER_ROW_OFFSET;
    int width = INNER_WIDTH(cols);
    if (width < 10) return;

    ncplane_set_fg_rgb(phone, theme_text_primary());
    ncplane_set_bg_rgb(phone, theme_bg());
    ncplane_putstr_yx(phone, CONTENT_START_ROW, CONTENT_COL, "ALARM");

    ncplane_set_fg_rgb(phone, theme_text_muted());
    const char *rule = theme_rule_glyph();
    for (int x = 0; x < width && CONTENT_COL + x < (int)cols - 1; x++)
        ncplane_putstr_yx(phone, CONTENT_START_ROW + 1, CONTENT_COL + x, (rule && rule[0]) ? rule : "-");

    size_t count = 0;
    const Alarm **alarms = alarm_service_list_all(&count);
    if (count == 0) {
        int mid = (CONTENT_START_ROW + 2 + footer) / 2;
        ghost_text(phone, mid, CONTENT_COL, theme_text_muted(), "No alarms");
        ghost_text(phone, mid + 1, CONTENT_COL, theme_text_muted(), "Press right soft key to add");
        ghost_softkeys(phone, "[Back]", "[Add]");
        return;
    }

    if (s_selected < 0) s_selected = 0;
    if (s_selected >= (int)count) s_selected = (int)count - 1;

    int list_start = CONTENT_START_ROW + 3;
    int max_rows = footer - list_start - 1;
    if (max_rows < 1) max_rows = 1;

    if (s_selected < s_scroll) s_scroll = s_selected;
    if (s_selected >= s_scroll + max_rows) s_scroll = s_selected - max_rows + 1;
    if (s_scroll < 0) s_scroll = 0;

    for (int i = 0; i < max_rows; i++) {
        int idx = s_scroll + i;
        if (idx >= (int)count) break;

        int row = list_start + i;
        int selected = (idx == s_selected);
        const Alarm *a = alarms[idx];

        /* HH:MM (left) · ON/OFF/DAILY state (right) per the WAKE blueprint */
        char tm[16];
        snprintf(tm, sizeof(tm), "%02d:%02d", a->hour, a->minute);
        const char *state = !a->enabled ? "OFF" : repeat_label(a->repeat);
        bh_list_item(phone, row, CONTENT_COL, width, tm, state, selected, idx);
    }

    ghost_text(phone, footer - 1, CONTENT_COL, theme_text_muted(),
               "\u25C0:Del OK:Toggle \u25B6:Edit");
    ghost_softkeys(phone, "[Back]", "[Add]");

    const Alarm *ringing = alarm_service_current_ringing();
    if (ringing) {
        int w = ALARM_RING_POPUP_WIDTH;
        int h = ALARM_RING_POPUP_HEIGHT;
        int top = ((int)rows - h) / 2;
        int left = ((int)cols - w) / 2;
        if (top < UI_POPUP_MIN_TOP) top = UI_POPUP_MIN_TOP;
        if (left < UI_POPUP_MIN_LEFT) left = UI_POPUP_MIN_LEFT;
        ghost_fill_rect(phone, top, left, h, w, ' ', theme_text_primary(), theme_bg());
        ghost_text(phone, top + UI_POPUP_TITLE_ROW_OFFSET, left + UI_POPUP_TEXT_INSET_X,
                   theme_text_primary(), "ALARM RINGING");
        char tbuf[16];
        snprintf(tbuf, sizeof(tbuf), "%02d:%02d", ringing->hour, ringing->minute);
        ghost_text(phone, top + UI_POPUP_INPUT_ROW_OFFSET, left + UI_POPUP_TEXT_INSET_X,
                   theme_text_primary(), tbuf);
        ghost_text(phone, top + UI_POPUP_INPUT_ROW_OFFSET + 1, left + UI_POPUP_TEXT_INSET_X,
                   theme_text_muted(), ringing->label ? ringing->label : "Alarm");
        ghost_softkeys(phone, "[Snooze]", "[Dismiss]");
    }

    if (s_delete_prompt) draw_delete_popup(phone);
    if (s_time_prompt) draw_time_popup(phone, rows, cols);
}

screen_id screen_alarm_input(uint32_t key) {
    const Alarm *ringing = alarm_service_current_ringing();
    if (ringing) {
        switch (key) {
            case NCKEY_ENTER:
            case '\n':
                alarm_service_stop_ringing();
                return SCREEN_ALARM;
            case KEY_SOFT_LEFT_ACTION:
                alarm_service_snooze_current(ALARM_SNOOZE_MIN);
                return SCREEN_HOME;
            case KEY_SOFT_RIGHT_ACTION:
                alarm_service_dismiss_current();
                return SCREEN_HOME;
            default:
                return SCREEN_ALARM;
        }
    }

    size_t count = 0;
    const Alarm **alarms = alarm_service_list_all(&count);

    /* ── Time editor popup ────────────────────────────────────────── */
    if (s_time_prompt) {
        switch (key) {
            case NCKEY_LEFT:
                s_time_field = 0;
                return SCREEN_ALARM;
            case NCKEY_RIGHT:
                s_time_field = 1;
                return SCREEN_ALARM;
            case NCKEY_UP:
                if (s_time_field == 0)
                    s_time_hour = (s_time_hour + 1) % 24;
                else
                    s_time_minute = (s_time_minute + 1) % 60;
                return SCREEN_ALARM;
            case NCKEY_DOWN:
                if (s_time_field == 0)
                    s_time_hour = (s_time_hour + 23) % 24;
                else
                    s_time_minute = (s_time_minute + 59) % 60;
                return SCREEN_ALARM;
            case NCKEY_ENTER:
            case '\n':
            case KEY_SOFT_RIGHT_ACTION:
                if (s_edit_existing) {
                    alarm_service_set_time(s_edit_id, s_time_hour, s_time_minute);
                    alarm_service_set_repeat(s_edit_id, s_time_repeat);
                } else {
                    char label[32];
                    snprintf(label, sizeof(label), "Alarm %d", s_seed++);
                    Alarm *created = alarm_service_create(s_time_hour, s_time_minute, label);
                    if (created && created->id) alarm_service_set_repeat(created->id, s_time_repeat);
                    (void)created;
                    s_selected = 0;
                    s_scroll = 0;
                }
                s_time_prompt = 0;
                s_digit_len = 0;
                return SCREEN_ALARM;
            case KEY_SOFT_LEFT_ACTION:
                s_time_prompt = 0;
                s_digit_len = 0;
                return SCREEN_ALARM;
            case NCKEY_BACKSPACE:
            case 127:
                if (s_digit_len > 0) {
                    s_digit_len--;
                    s_digit_buf[s_digit_len] = '\0';
                }
                return SCREEN_ALARM;
            default:
                if (key == 'r' || key == 'R' || key == '#') {
                    if (s_time_repeat == ALARM_ONCE) s_time_repeat = ALARM_DAILY;
                    else if (s_time_repeat == ALARM_DAILY) s_time_repeat = ALARM_WEEKDAYS;
                    else s_time_repeat = ALARM_ONCE;
                    return SCREEN_ALARM;
                }
                if (key >= '0' && key <= '9') {
                    if (s_digit_len < 4) {
                        s_digit_buf[s_digit_len++] = (char)key;
                        s_digit_buf[s_digit_len] = '\0';
                    }
                    if (s_digit_len == 4) {
                        int hh = (s_digit_buf[0] - '0') * 10 + (s_digit_buf[1] - '0');
                        int mm = (s_digit_buf[2] - '0') * 10 + (s_digit_buf[3] - '0');
                        if (hh >= 0 && hh <= 23) s_time_hour = hh;
                        if (mm >= 0 && mm <= 59) s_time_minute = mm;
                        s_digit_len = 0;
                        s_digit_buf[0] = '\0';
                    }
                }
                return SCREEN_ALARM;
        }
    }

    /* ── Delete confirmation ──────────────────────────────────────── */
    if (s_delete_prompt) {
        switch (key) {
            case NCKEY_LEFT:
            case NCKEY_UP:
                s_delete_yes = 0;
                return SCREEN_ALARM;
            case NCKEY_RIGHT:
            case NCKEY_DOWN:
                s_delete_yes = 1;
                return SCREEN_ALARM;
            case NCKEY_ENTER:
            case '\n':
            case KEY_SOFT_RIGHT_ACTION:
                if (s_delete_yes && count > 0 && alarms && alarms[s_selected] && alarms[s_selected]->id) {
                    alarm_service_delete(alarms[s_selected]->id);
                    alarm_service_list_all(&count);
                    if (s_selected >= (int)count && s_selected > 0) s_selected--;
                }
                s_delete_prompt = 0;
                s_delete_yes = 0;
                return SCREEN_ALARM;
            case KEY_SOFT_LEFT_ACTION:
                s_delete_prompt = 0;
                s_delete_yes = 0;
                return SCREEN_ALARM;
            default:
                return SCREEN_ALARM;
        }
    }

    /* ── Normal list input ────────────────────────────────────────── */
    switch (key) {
        case NCKEY_UP:
            if (s_selected > 0) s_selected--;
            return SCREEN_ALARM;
        case NCKEY_DOWN:
            if (s_selected < (int)count - 1) s_selected++;
            return SCREEN_ALARM;
        case NCKEY_ENTER:
        case '\n':
            /* Center key = toggle enable/disable */
            if (count > 0 && alarms && alarms[s_selected] && alarms[s_selected]->id) {
                alarm_service_toggle(alarms[s_selected]->id);
            }
            return SCREEN_ALARM;
        case NCKEY_LEFT:
            /* Left = delete */
            if (count > 0 && alarms && alarms[s_selected] && alarms[s_selected]->id) {
                s_delete_prompt = 1;
                s_delete_yes = 0;
            }
            return SCREEN_ALARM;
        case NCKEY_RIGHT:
            /* Right = edit time */
            if (count > 0 && alarms && alarms[s_selected])
                start_time_prompt_edit(alarms[s_selected]);
            return SCREEN_ALARM;
        case KEY_SOFT_RIGHT_ACTION:
            /* RSK = create new alarm */
            if (count < ALARM_MAX_COUNT) {
                start_time_prompt_new();
            }
            return SCREEN_ALARM;
        case KEY_SOFT_LEFT_ACTION:
            /* LSK = back to menu */
            return SCREEN_HOME;
        default:
            return SCREEN_ALARM;
    }
}

int screen_alarm_is_time_entry_mode(void) {
    return s_time_prompt ? 1 : 0;
}
