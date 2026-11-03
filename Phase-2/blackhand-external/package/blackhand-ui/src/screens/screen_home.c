#include <notcurses/notcurses.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "ui.h"
#include "config.h"
#include "draw_utils.h"
#include "bh_skin.h"
#include "services/theme_service.h"
#include "services/mp3_service.h"
#include "services/settings_service.h"
#include "services/pin_service.h"

typedef struct {
    const char *label;
    screen_id   target;
} menu_item;

static const menu_item items[] = {
    {"PHONE",    SCREEN_CALLS},
    {"SMS",      SCREEN_MESSAGES},
    {"CONTACTS", SCREEN_CONTACTS},
    {"MUSIC",    SCREEN_MP3},
    {"NOTES",    SCREEN_NOTES},
    {"VOICE",    SCREEN_VOICE_MEMO},
    {"SETTINGS", SCREEN_SETTINGS},
};
static const int item_count = sizeof(items) / sizeof(items[0]);

static int selected = 0;
static int hw_exit_armed = 0;
static long hw_exit_arm_ms = 0; /* CLOCK_MONOTONIC ms -- see screen_home_input() */
static int hw_pin_prompt = 0;
static char hw_pin_buf[8] = {0};
static int hw_pin_len = 0;
static char hw_pin_error[32] = {0};

typedef struct { uint32_t text; uint32_t cell; int fill; } heat_row_t;
static const heat_row_t k_heat[] = {
    {0xF09595, 0xE24B4A, 5}, {0xFAC775, 0xEF9F27, 3}, {0xC0DD97, 0x97C459, 2},
    {0x85B7EB, 0x378ADD, 1}, {0xC0DD97, 0x97C459, 2}, {0xFAC775, 0xEF9F27, 4},
    {0x85B7EB, 0x378ADD, 1}, {0xC0DD97, 0x97C459, 3},
};

static void draw_hand_white(struct ncplane *phone, unsigned rows, unsigned cols)
{
    time_t now = time(NULL);
    struct tm tm_now;
    char tbuf[16] = "00:00";
    if (localtime_r(&now, &tm_now))
        snprintf(tbuf, sizeof(tbuf), "%02d:%02d", tm_now.tm_hour, tm_now.tm_min);

    ghost_text(phone, CONTENT_START_ROW + 2, CONTENT_COL, theme_text_muted(), "HAND WHITE MODE");
    ghost_text(phone, CONTENT_START_ROW + 4, CONTENT_COL, theme_text_primary(), tbuf);
    ghost_text(phone, CONTENT_START_ROW + 6, CONTENT_COL, theme_text_muted(), "GPS: NO VERIFIED FIX");
    ghost_text(phone, CONTENT_START_ROW + 7, CONTENT_COL, theme_text_muted(), "");

    if (hw_pin_prompt) {
        int w = HOME_PIN_POPUP_WIDTH, h = HOME_PIN_POPUP_HEIGHT;
        int top = ((int)rows - h) / 2, left = ((int)cols - w) / 2;
        if (top < UI_POPUP_MIN_TOP) top = UI_POPUP_MIN_TOP;
        if (left < UI_POPUP_MIN_LEFT) left = UI_POPUP_MIN_LEFT;
        ghost_fill_rect(phone, top, left, h, w, ' ', theme_text_primary(), theme_bg());
        ghost_text(phone, top + UI_POPUP_TITLE_ROW_OFFSET, left + UI_POPUP_TEXT_INSET_X,
                   theme_text_primary(), "ENTER PIN");
        char dots[8] = "    ";
        for (int i = 0; i < hw_pin_len && i < 4; i++) dots[i] = '*';
        ghost_text(phone, top + UI_POPUP_INPUT_ROW_OFFSET, left + UI_POPUP_TEXT_INSET_X,
                   theme_text_primary(), dots);
        if (hw_pin_error[0])
            ghost_text(phone, top + UI_POPUP_INPUT_ROW_OFFSET + 1,
                       left + UI_POPUP_TEXT_INSET_X, theme_border(), hw_pin_error);
    } else {
        ghost_text(phone, CONTENT_START_ROW + 9, CONTENT_COL, theme_text_muted(),
                   hw_exit_armed ? "PRESS E NOW" : "Q THEN E TO EXIT");
    }
    ghost_softkeys(phone, NULL, NULL);
}

void screen_home_draw(struct ncplane *phone)
{
    unsigned rows, cols;
    ncplane_dim_yx(phone, &rows, &cols);
    if (rows < HOME_MIN_ROWS || cols < HOME_MIN_COLS) {
        ncplane_putstr_yx(phone, 2, 2, TEXT_TOO_SMALL);
        return;
    }

    if (settings_service_get_bool(SETTINGS_KEY_HAND_WHITE)) {
        draw_hand_white(phone, rows, cols);
        return;
    }

    const bh_theme_t *th = theme_active();
    int col     = 3;
    int width   = (int)cols - 6;
    int start   = HOME_CONTENT_START_ROW;
    int footer  = (int)rows - 3;
    int spacing = ((footer - start) >= item_count * 2) ? 2 : 1;

    for (int i = 0; i < item_count; i++) {
        int row = start + i * spacing;
        if (row >= (int)rows - 3) break;
        int sel = (i == selected);

        if (th->is_heat) {
            const heat_row_t *h = &k_heat[i % (int)(sizeof(k_heat)/sizeof(k_heat[0]))];
            char lbl[32];
            snprintf(lbl, sizeof(lbl), "%s %s", sel ? "›" : " ", items[i].label);
            ncplane_set_bg_rgb(phone, th->bg);
            ncplane_set_fg_rgb(phone, h->text);
            ncplane_putstr_yx(phone, row, col, lbl);
            const int tiles = 5, tw = 2;
            int sx = col + width - tiles * tw;
            for (int k = 0; k < tiles; k++) {
                ncplane_set_fg_rgb(phone, k < h->fill ? h->cell : 0x2C2C2A);
                ncplane_set_bg_rgb(phone, th->bg);
                ncplane_putstr_yx(phone, row, sx + k * tw, "██");
            }
        } else {
            bh_list_item(phone, row, col, width, items[i].label, "", sel, i);
        }
    }

    if (th->has_stamp) {
        int sr = (int)rows - 6;
        int sc = (int)cols - 12;
        if (sr > start && sc > col) {
            ncplane_set_fg_rgb(phone, 0xA32D2D);
            ncplane_set_bg_rgb(phone, th->bg);
            ncplane_putstr_yx(phone, sr,     sc, "┌────────┐");
            ncplane_putstr_yx(phone, sr + 1, sc, "│ BH·OS  │");
            ncplane_putstr_yx(phone, sr + 2, sc, "└────────┘");
        }
    }

    ghost_softkeys(phone, NULL, NULL);
}

screen_id screen_home_input(uint32_t key)
{
    if (settings_service_get_bool(SETTINGS_KEY_HAND_WHITE)) {
        /* This device has no RTC (wall clock jumps on correction), so the
         * 2-second exit-arm window is measured off CLOCK_MONOTONIC, not
         * time(NULL). See kmono_sec() in blackhand-init for the same issue. */
        struct timespec ts_now;
        clock_gettime(CLOCK_MONOTONIC, &ts_now);
        long now_ms = ts_now.tv_sec * 1000L + ts_now.tv_nsec / 1000000L;

        if (hw_pin_prompt) {
            if (key == KEY_SOFT_LEFT_ACTION) {
                hw_pin_prompt = 0; hw_pin_len = 0;
                hw_pin_buf[0] = '\0'; hw_pin_error[0] = '\0';
                return SCREEN_HOME;
            }
            if (key == NCKEY_BACKSPACE || key == 127 || key == KEY_ACTION_SECONDARY) {
                if (hw_pin_len > 0) { hw_pin_len--; hw_pin_buf[hw_pin_len] = '\0'; }
                return SCREEN_HOME;
            }
            if (key >= '0' && key <= '9') {
                if (hw_pin_len < 4) {
                    hw_pin_buf[hw_pin_len++] = (char)key;
                    hw_pin_buf[hw_pin_len] = '\0';
                }
                if (hw_pin_len == 4) {
                    if (pin_service_verify(hw_pin_buf)) {
                        settings_service_toggle_by_key(SETTINGS_KEY_HAND_WHITE);
                        theme_service_sync_from_settings();
                        hw_pin_prompt = 0; hw_pin_len = 0;
                        hw_pin_buf[0] = '\0'; hw_pin_error[0] = '\0';
                    } else {
                        int lockout = pin_service_lockout_remaining_sec();
                        if (lockout > 0)
                            snprintf(hw_pin_error, sizeof(hw_pin_error), "LOCKED %ds", lockout);
                        else
                            snprintf(hw_pin_error, sizeof(hw_pin_error), "WRONG PIN");
                        hw_pin_len = 0; hw_pin_buf[0] = '\0';
                    }
                }
            }
            return SCREEN_HOME;
        }

        if (hw_exit_armed && now_ms - hw_exit_arm_ms > 2000) hw_exit_armed = 0;

        if (key == KEY_SOFT_LEFT_ACTION) {
            hw_exit_armed = 1; hw_exit_arm_ms = now_ms;
            return SCREEN_HOME;
        }
        if ((key == KEY_SOFT_RIGHT_ACTION) && hw_exit_armed) {
            hw_pin_prompt = 1; hw_exit_armed = 0;
            hw_pin_len = 0; hw_pin_buf[0] = '\0'; hw_pin_error[0] = '\0';
            return SCREEN_HOME;
        }
        return SCREEN_HOME;
    }

    switch (key) {
    case NCKEY_UP:
        if (selected > 0) selected--;
        return SCREEN_HOME;
    case NCKEY_DOWN:
        if (selected < item_count - 1) selected++;
        return SCREEN_HOME;
    case NCKEY_ENTER:
    case '\n':
    case KEY_SOFT_RIGHT_ACTION:   /* E = open selected app */
    case KEY_ACTION_PRIMARY:      /* A = accept/open (consistent with A=confirm) */
        return items[selected].target;
    case KEY_ACTION_SECONDARY:    /* D unused on Home */
        return SCREEN_HOME;
    /* Q (SOFT_LEFT) falls through to default: Home has no "back", so it does
     * nothing — reserved for future quick-settings per spec. */
    default:
        /* Number keys 1-7 are direct app shortcuts. This is the "accessible
         * shortcut" from the spec: implemented on Home only, because making
         * digits jump apps from inside an app would clash with dial / T9 /
         * PIN entry. Item order matches the spec's app list exactly:
         *   1 Phone  2 SMS  3 Contacts  4 Music  5 Notes  6 Voice  7 Settings */
        if (key >= '1' && key <= '0' + item_count)
            return items[key - '1'].target;
        return SCREEN_HOME;
    }
}
