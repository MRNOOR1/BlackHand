#include <notcurses/notcurses.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "draw_utils.h"
#include "bh_skin.h"
#include "ui.h"
#include "services/settings_service.h"
#include "services/theme_service.h"
#include "services/contacts_service.h"
#include "services/notes_service.h"
#include "services/voice_memo_service.h"
#include "services/comm_service.h"
#include "services/pin_service.h"

/* ── Settings screen architecture ────────────────────────────────────────
 *
 * Main Settings menu:
 *   APPEARANCE    -> sub-screen (dark/light mode, themes)
 *   SECURITY      -> sub-screen (hand white, white wipe, pin)
 *   CONNECTIVITY  -> sub-screen (bluetooth)
 *   SYSTEM INFO   -> sub-screen (device info)
 *
 * Navigation:
 *   up/down       = move through list
 *   center/RSK    = open selected setting
 *   LSK           = back to previous menu / main menu
 * ────────────────────────────────────────────────────────────────────── */

typedef enum {
    SETT_MODE_MAIN,
    SETT_MODE_APPEARANCE,
    SETT_MODE_SECURITY,
    SETT_MODE_CONNECTIVITY,
    SETT_MODE_INFO,
    SETT_MODE_PIN_ENTRY,      /* PIN verification/entry overlay */
    SETT_MODE_PIN_CHANGE,     /* Change PIN flow */
    SETT_MODE_WIPE_CONFIRM,   /* White wipe confirmation */
} settings_mode_t;

/* Main menu items */
#define MAIN_ITEM_COUNT SETTINGS_MAIN_ITEM_COUNT
static const char *main_items[] = {
    SETTINGS_MAIN_ITEM_APPEARANCE,
    SETTINGS_MAIN_ITEM_SECURITY,
    SETTINGS_MAIN_ITEM_CONNECTIVITY,
    SETTINGS_MAIN_ITEM_SYSTEM_INFO,
};

/* Appearance sub-menu items */
typedef enum {
    APP_NIGHT_MODE,
    APP_THEME,
} appearance_item_t;

/* Security sub-menu items */
typedef enum {
    SEC_HAND_WHITE,
    SEC_WHITE_WIPE,
    SEC_UPDATE_PIN,
} security_item_t;

static settings_mode_t mode = SETT_MODE_MAIN;
static int s_selected = 0;
static int s_sub_selected = 0;

/* PIN entry state */
static char pin_buf[8] = {0};
static int pin_len = 0;
static int pin_purpose = 0; /* 0=wipe verify, 1=change old, 2=change new, 3=change confirm */
static char pin_new_buf[8] = {0};
static char pin_error[64] = {0};

/* Wipe confirmation */
static int s_wipe_yes = 0;

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

static void wipe_phone_data(void) {
    size_t count = 0;

    const Contact **contacts = contact_service_list_all(&count);
    while (count > 0 && contacts && contacts[0] && contacts[0]->id) {
        contact_service_delete(contacts[0]->id);
        contacts = contact_service_list_all(&count);
    }

    Note **notes = notes_service_list_all(&count);
    while (count > 0 && notes && notes[0]) {
        notes_service_delete_note(notes[0]);
        notes = notes_service_list_all(&count);
    }

    const VoiceMemo **memos = voice_memo_service_list_all(&count);
    while (count > 0 && memos && memos[0] && memos[0]->filename) {
        voice_memo_service_delete(memos[0]->filename);
        memos = voice_memo_service_list_all(&count);
    }

    comm_service_reset();
    settings_service_reset_defaults();
    pin_service_reset();
    theme_service_sync_from_settings();
}

static void reset_pin_entry(void) {
    pin_buf[0] = '\0';
    pin_len = 0;
    pin_error[0] = '\0';
}

/* ── DRAW: Main settings menu ────────────────────────────────────────── */
static void draw_main(struct ncplane *phone) {
    unsigned rows, cols;
    ncplane_dim_yx(phone, &rows, &cols);
    int footer = (int)rows - FOOTER_ROW_OFFSET;
    int width = INNER_WIDTH(cols);

    ncplane_set_fg_rgb(phone, theme_text_primary());
    ncplane_set_bg_rgb(phone, theme_bg());
    ncplane_putstr_yx(phone, CONTENT_START_ROW, CONTENT_COL, "SETTINGS");

    ncplane_set_fg_rgb(phone, theme_text_muted());
    const char *rule = theme_rule_glyph();
    for (int x = 0; x < width && CONTENT_COL + x < (int)cols - 1; x++)
        ncplane_putstr_yx(phone, CONTENT_START_ROW + 1, CONTENT_COL + x, (rule && rule[0]) ? rule : "-");

    ghost_text(phone, CONTENT_START_ROW + 2, CONTENT_COL, theme_text_muted(),
               settings_service_get_bool(SETTINGS_KEY_NIGHT_MODE) ? "Mode: Dark" : "Mode: Light");

    if (s_selected < 0) s_selected = 0;
    if (s_selected >= MAIN_ITEM_COUNT) s_selected = MAIN_ITEM_COUNT - 1;

    for (int i = 0; i < MAIN_ITEM_COUNT; i++) {
        int row = CONTENT_START_ROW + 3 + i;
        if (row >= footer) break;

        bh_list_item(phone, row, CONTENT_COL, width, main_items[i], "", i == s_selected, i);
    }

    ghost_softkeys(phone, "[Back]", "[Open]");
}

/* ── DRAW: Appearance sub-menu ───────────────────────────────────────── */
static void draw_appearance(struct ncplane *phone) {
    unsigned rows, cols;
    ncplane_dim_yx(phone, &rows, &cols);
    int footer = (int)rows - FOOTER_ROW_OFFSET;
    int width = INNER_WIDTH(cols);

    ncplane_set_fg_rgb(phone, theme_text_primary());
    ncplane_set_bg_rgb(phone, theme_bg());
    ncplane_putstr_yx(phone, CONTENT_START_ROW, CONTENT_COL, "APPEARANCE");

    ncplane_set_fg_rgb(phone, theme_text_muted());
    const char *rule = theme_rule_glyph();
    for (int x = 0; x < width && CONTENT_COL + x < (int)cols - 1; x++)
        ncplane_putstr_yx(phone, CONTENT_START_ROW + 1, CONTENT_COL + x, (rule && rule[0]) ? rule : "-");

    int total = 2;
    if (s_sub_selected >= total) s_sub_selected = total - 1;
    if (s_sub_selected < 0) s_sub_selected = 0;

    /* Night Mode toggle (value right-aligned per the TOGGLES blueprint) */
    int row = CONTENT_START_ROW + 4;
    if (row < footer)
        bh_list_item(phone, row, CONTENT_COL, width, "DARK MODE",
                     settings_service_get_bool(SETTINGS_KEY_NIGHT_MODE) ? "ON" : "OFF",
                     s_sub_selected == 0, 0);

    /* Themes sub-screen */
    row = CONTENT_START_ROW + 5;
    if (row < footer)
        bh_list_item(phone, row, CONTENT_COL, width, "THEMES", "OPEN",
                     s_sub_selected == 1, 1);

    ghost_softkeys(phone, "[Back]", "[Select]");
}

/* ── DRAW: Security sub-menu ─────────────────────────────────────────── */
static void draw_security(struct ncplane *phone) {
    unsigned rows, cols;
    ncplane_dim_yx(phone, &rows, &cols);
    int footer = (int)rows - FOOTER_ROW_OFFSET;
    int width = INNER_WIDTH(cols);

    ncplane_set_fg_rgb(phone, theme_text_primary());
    ncplane_set_bg_rgb(phone, theme_bg());
    ncplane_putstr_yx(phone, CONTENT_START_ROW, CONTENT_COL, "SECURITY");

    ncplane_set_fg_rgb(phone, theme_text_muted());
    const char *rule = theme_rule_glyph();
    for (int x = 0; x < width && CONTENT_COL + x < (int)cols - 1; x++)
        ncplane_putstr_yx(phone, CONTENT_START_ROW + 1, CONTENT_COL + x, (rule && rule[0]) ? rule : "-");

    int total = 3;
    if (s_sub_selected >= total) s_sub_selected = total - 1;
    if (s_sub_selected < 0) s_sub_selected = 0;

    const char *sec_labels[] = { "HAND WHITE", "WHITE WIPE", "UPDATE PIN" };
    const char *sec_meta[]   = {
        settings_service_get_bool(SETTINGS_KEY_HAND_WHITE) ? "ON" : "OFF",
        "RUN", "OPEN",
    };

    for (int i = 0; i < total; i++) {
        int row = CONTENT_START_ROW + 3 + i;
        if (row >= footer) break;
        bh_list_item(phone, row, CONTENT_COL, width, sec_labels[i], sec_meta[i],
                     i == s_sub_selected, i);
    }

    ghost_softkeys(phone, "[Back]", "[Select]");
}

/* ── DRAW: Connectivity sub-menu ─────────────────────────────────────── */
static void draw_connectivity(struct ncplane *phone) {
    unsigned rows, cols;
    ncplane_dim_yx(phone, &rows, &cols);
    int width = INNER_WIDTH(cols);

    ncplane_set_fg_rgb(phone, theme_text_primary());
    ncplane_set_bg_rgb(phone, theme_bg());
    ncplane_putstr_yx(phone, CONTENT_START_ROW, CONTENT_COL, "CONNECTIVITY");

    ncplane_set_fg_rgb(phone, theme_text_muted());
    const char *rule = theme_rule_glyph();
    for (int x = 0; x < width && CONTENT_COL + x < (int)cols - 1; x++)
        ncplane_putstr_yx(phone, CONTENT_START_ROW + 1, CONTENT_COL + x, (rule && rule[0]) ? rule : "-");

    s_sub_selected = 0;
    int row = CONTENT_START_ROW + 3;
    bh_list_item(phone, row, CONTENT_COL, width, "BLUETOOTH",
                 settings_service_get_bool(SETTINGS_KEY_BLUETOOTH) ? "ON" : "OFF", 1, 0);

    ghost_softkeys(phone, "[Back]", "[Open]");
}

/* ── DRAW: System Info ───────────────────────────────────────────────── */
static void draw_info(struct ncplane *phone) {
    unsigned rows, cols;
    ncplane_dim_yx(phone, &rows, &cols);
    int width = INNER_WIDTH(cols);
    (void)rows;

    ncplane_set_fg_rgb(phone, theme_text_primary());
    ncplane_set_bg_rgb(phone, theme_bg());
    ncplane_putstr_yx(phone, CONTENT_START_ROW, CONTENT_COL, "SYSTEM INFO");

    ncplane_set_fg_rgb(phone, theme_text_muted());
    const char *rule = theme_rule_glyph();
    for (int x = 0; x < width && CONTENT_COL + x < (int)cols - 1; x++)
        ncplane_putstr_yx(phone, CONTENT_START_ROW + 1, CONTENT_COL + x, (rule && rule[0]) ? rule : "-");

    int row = CONTENT_START_ROW + 3;
    ghost_label_value(phone, row,     CONTENT_COL, CONTENT_COL + 12, "MODEL", "BlackHand OS");
    ghost_label_value(phone, row + 1, CONTENT_COL, CONTENT_COL + 12, "VERSION", "Phase 2.0");
    ghost_label_value(phone, row + 2, CONTENT_COL, CONTENT_COL + 12, "PLATFORM", "RPi5");
    ghost_label_value(phone, row + 3, CONTENT_COL, CONTENT_COL + 12, "DISPLAY", "HyperPixel 4");

    ghost_softkeys(phone, "[Back]", "");
}

/* ── DRAW: PIN entry overlay ─────────────────────────────────────────── */
static void draw_pin_entry(struct ncplane *phone) {
    unsigned rows, cols;
    ncplane_dim_yx(phone, &rows, &cols);

    int w = SETTINGS_PIN_POPUP_WIDTH;
    int h = SETTINGS_PIN_POPUP_HEIGHT;
    int top = ((int)rows - h) / 2;
    int left = ((int)cols - w) / 2;
    if (top < UI_POPUP_MIN_TOP) top = UI_POPUP_MIN_TOP;
    if (left < UI_POPUP_MIN_LEFT) left = UI_POPUP_MIN_LEFT;

    ghost_fill_rect(phone, top, left, h, w, ' ', theme_text_primary(), theme_bg());

    const char *title = "ENTER PIN";
    if (pin_purpose == 1) title = "CURRENT PIN";
    else if (pin_purpose == 2) title = "NEW PIN";
    else if (pin_purpose == 3) title = "CONFIRM PIN";

    ghost_text(phone, top + UI_POPUP_TITLE_ROW_OFFSET, left + UI_POPUP_TEXT_INSET_X, theme_text_primary(), title);

    /* Show dots for entered digits */
    char dots[8] = "    ";
    for (int i = 0; i < pin_len && i < 4; i++) dots[i] = '*';
    ghost_text(phone, top + UI_POPUP_INPUT_ROW_OFFSET, left + 10, theme_text_primary(), dots);

    if (pin_error[0] != '\0') {
        ghost_text(phone, top + UI_POPUP_HINT_ROW_OFFSET, left + UI_POPUP_TEXT_INSET_X, theme_border(), pin_error);
    }

    ghost_softkeys(phone, "[Cancel]", "[OK]");
}

/* ── DRAW: Wipe confirmation ─────────────────────────────────────────── */
static void draw_wipe_confirm(struct ncplane *phone) {
    ghost_confirm_popup(phone, "ERASE ALL DATA?", s_wipe_yes);
}

/* ── Main draw dispatcher ────────────────────────────────────────────── */
void screen_settings_draw(struct ncplane *phone) {
    switch (mode) {
        case SETT_MODE_MAIN:         draw_main(phone); break;
        case SETT_MODE_APPEARANCE:   draw_appearance(phone); break;
        case SETT_MODE_SECURITY:     draw_security(phone); break;
        case SETT_MODE_CONNECTIVITY: draw_connectivity(phone); break;
        case SETT_MODE_INFO:         draw_info(phone); break;
        case SETT_MODE_PIN_ENTRY:
        case SETT_MODE_PIN_CHANGE:
            /* Draw the underlying screen first, then overlay */
            if (pin_purpose == 0) draw_security(phone);
            else draw_security(phone);
            draw_pin_entry(phone);
            break;
        case SETT_MODE_WIPE_CONFIRM:
            draw_security(phone);
            draw_wipe_confirm(phone);
            break;
    }
}

/* ── Handle PIN digit entry ──────────────────────────────────────────── */
static screen_id handle_pin_input(uint32_t key) {
    switch (key) {
        case KEY_SOFT_LEFT_ACTION:
            reset_pin_entry();
            mode = SETT_MODE_SECURITY;
            return SCREEN_SETTINGS;
        case NCKEY_BACKSPACE:
        case 127:
            if (pin_len > 0) {
                pin_len--;
                pin_buf[pin_len] = '\0';
            }
            pin_error[0] = '\0';
            return SCREEN_SETTINGS;
        default:
            if (key >= '0' && key <= '9') {
                if (pin_len < 4) {
                    pin_buf[pin_len++] = (char)key;
                    pin_buf[pin_len] = '\0';
                }
                if (pin_len == 4) {
                    if (pin_purpose == 0) {
                        /* Verify for wipe */
                        if (pin_service_verify(pin_buf)) {
                            reset_pin_entry();
                            s_wipe_yes = 0;
                            mode = SETT_MODE_WIPE_CONFIRM;
                        } else {
                            snprintf(pin_error, sizeof(pin_error), "WRONG PIN");
                            pin_len = 0;
                            pin_buf[0] = '\0';
                        }
                    } else if (pin_purpose == 1) {
                        /* Verify current PIN for change */
                        if (pin_service_verify(pin_buf)) {
                            reset_pin_entry();
                            pin_purpose = 2;
                        } else {
                            snprintf(pin_error, sizeof(pin_error), "WRONG PIN");
                            pin_len = 0;
                            pin_buf[0] = '\0';
                        }
                    } else if (pin_purpose == 2) {
                        /* Save new PIN, ask confirm */
                        memcpy(pin_new_buf, pin_buf, 5);
                        reset_pin_entry();
                        pin_purpose = 3;
                    } else if (pin_purpose == 3) {
                        /* Confirm new PIN */
                        if (strcmp(pin_buf, pin_new_buf) == 0) {
                            pin_service_update(pin_service_get(), pin_new_buf);
                            reset_pin_entry();
                            mode = SETT_MODE_SECURITY;
                        } else {
                            snprintf(pin_error, sizeof(pin_error), "PINS DON'T MATCH");
                            pin_len = 0;
                            pin_buf[0] = '\0';
                            pin_purpose = 2; /* retry new PIN */
                        }
                    }
                }
            }
            return SCREEN_SETTINGS;
    }
}

/* ── Input handling ──────────────────────────────────────────────────── */
screen_id screen_settings_input(uint32_t key) {

    /* ── PIN entry mode ───────────────────────────────────────────── */
    if (mode == SETT_MODE_PIN_ENTRY || mode == SETT_MODE_PIN_CHANGE) {
        return handle_pin_input(key);
    }

    /* ── Wipe confirmation ────────────────────────────────────────── */
    if (mode == SETT_MODE_WIPE_CONFIRM) {
        switch (key) {
            case NCKEY_LEFT:
            case NCKEY_UP:
                s_wipe_yes = 0;
                return SCREEN_SETTINGS;
            case NCKEY_RIGHT:
            case NCKEY_DOWN:
                s_wipe_yes = 1;
                return SCREEN_SETTINGS;
            case NCKEY_ENTER:
            case '\n':
            case KEY_SOFT_RIGHT_ACTION:
                if (s_wipe_yes) {
                    wipe_phone_data();
                    mode = SETT_MODE_MAIN;
                    s_selected = 0;
                    return SCREEN_HOME;
                }
                mode = SETT_MODE_SECURITY;
                return SCREEN_SETTINGS;
            case KEY_SOFT_LEFT_ACTION:
                mode = SETT_MODE_SECURITY;
                return SCREEN_SETTINGS;
            default:
                return SCREEN_SETTINGS;
        }
    }

    /* ── Appearance sub-menu ──────────────────────────────────────── */
    if (mode == SETT_MODE_APPEARANCE) {
        int total = 2;
        switch (key) {
            case NCKEY_UP:
                if (s_sub_selected > 0) s_sub_selected--;
                return SCREEN_SETTINGS;
            case NCKEY_DOWN:
                if (s_sub_selected < total - 1) s_sub_selected++;
                return SCREEN_SETTINGS;
            case NCKEY_ENTER:
            case '\n':
            case KEY_SOFT_RIGHT_ACTION:
                /* A/E: select/toggle. */
                if (s_sub_selected == APP_NIGHT_MODE) {
                    settings_service_toggle_by_key(SETTINGS_KEY_NIGHT_MODE);
                    theme_service_sync_from_settings();
                } else if (s_sub_selected == APP_THEME) {
                    return SCREEN_THEME;
                }
                return SCREEN_SETTINGS;
            case KEY_SOFT_LEFT_ACTION:
                /* Q: back to main settings. */
                mode = SETT_MODE_MAIN;
                s_sub_selected = 0;
                return SCREEN_SETTINGS;
            default:
                return SCREEN_SETTINGS;
        }
    }

    /* ── Security sub-menu ────────────────────────────────────────── */
    if (mode == SETT_MODE_SECURITY) {
        switch (key) {
            case NCKEY_UP:
                if (s_sub_selected > 0) s_sub_selected--;
                return SCREEN_SETTINGS;
            case NCKEY_DOWN:
                if (s_sub_selected < 2) s_sub_selected++;
                return SCREEN_SETTINGS;
            case NCKEY_ENTER:
            case '\n':
            case KEY_SOFT_RIGHT_ACTION:
                /* A/E: select. */
                if (s_sub_selected == SEC_HAND_WHITE) {
                    settings_service_toggle_by_key(SETTINGS_KEY_HAND_WHITE);
                    theme_service_sync_from_settings();
                } else if (s_sub_selected == SEC_WHITE_WIPE) {
                    reset_pin_entry();
                    pin_purpose = 0;
                    mode = SETT_MODE_PIN_ENTRY;
                } else if (s_sub_selected == SEC_UPDATE_PIN) {
                    reset_pin_entry();
                    pin_purpose = 1;
                    mode = SETT_MODE_PIN_CHANGE;
                }
                return SCREEN_SETTINGS;
            case KEY_SOFT_LEFT_ACTION:
                /* Q: back to main settings. */
                mode = SETT_MODE_MAIN;
                s_sub_selected = 0;
                return SCREEN_SETTINGS;
            default:
                return SCREEN_SETTINGS;
        }
    }

    /* ── Connectivity sub-menu ────────────────────────────────────── */
    if (mode == SETT_MODE_CONNECTIVITY) {
        switch (key) {
            case NCKEY_ENTER:
            case '\n':
            case KEY_SOFT_RIGHT_ACTION:
                /* A/E: enter bluetooth screen. */
                return SCREEN_BLUETOOTH;
            case KEY_SOFT_LEFT_ACTION:
                /* Q: back to main settings. */
                mode = SETT_MODE_MAIN;
                return SCREEN_SETTINGS;
            default:
                return SCREEN_SETTINGS;
        }
    }

    /* ── System Info ──────────────────────────────────────────────── */
    if (mode == SETT_MODE_INFO) {
        switch (key) {
            case KEY_SOFT_LEFT_ACTION:
            case KEY_SOFT_RIGHT_ACTION:
                /* Q/E: back to main settings. */
                mode = SETT_MODE_MAIN;
                return SCREEN_SETTINGS;
            default:
                return SCREEN_SETTINGS;
        }
    }

    /* ── Main menu ────────────────────────────────────────────────── */
    switch (key) {
        case NCKEY_UP:
            if (s_selected > 0) s_selected--;
            return SCREEN_SETTINGS;
        case NCKEY_DOWN:
            if (s_selected < MAIN_ITEM_COUNT - 1) s_selected++;
            return SCREEN_SETTINGS;
        case NCKEY_ENTER:
        case '\n':
        case KEY_SOFT_RIGHT_ACTION:
            /* A/E: enter selected submenu. */
            s_sub_selected = 0;
            switch (s_selected) {
                case 0: mode = SETT_MODE_APPEARANCE; break;
                case 1: mode = SETT_MODE_SECURITY; break;
                case 2: mode = SETT_MODE_CONNECTIVITY; break;
                case 3: mode = SETT_MODE_INFO; break;
            }
            return SCREEN_SETTINGS;
        case KEY_SOFT_LEFT_ACTION:
            /* Q: back to home. */
            return SCREEN_HOME;
        default:
            return SCREEN_SETTINGS;
    }
}

int screen_settings_is_pin_entry_mode(void) {
    return (mode == SETT_MODE_PIN_ENTRY || mode == SETT_MODE_PIN_CHANGE) ? 1 : 0;
}
