#include <notcurses/notcurses.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "config.h"
#include "draw_utils.h"
#include "services/comm_service.h"
#include "services/contacts_service.h"
#include "services/theme_service.h"
#include "ui.h"

typedef enum {
    CONTACTS_MODE_LIST,
    CONTACTS_MODE_PROFILE,
} contacts_mode_t;

static int s_selected = 0;
static int s_scroll = 0;
static int s_seed = 1;
static int s_action = 0;
static int s_delete_prompt = 0;
static int s_delete_yes = 0;
static contacts_mode_t s_mode = CONTACTS_MODE_LIST;

static void draw_delete_popup(struct ncplane *phone, unsigned rows, unsigned cols) {
    (void)rows;
    (void)cols;
    ghost_confirm_popup(phone, "ARE YOU SURE?", s_delete_yes);
}

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

void screen_contacts_draw(struct ncplane *phone) {
    unsigned rows, cols;
    ncplane_dim_yx(phone, &rows, &cols);

    int footer = (int)rows - FOOTER_ROW_OFFSET;
    int width = INNER_WIDTH(cols);
    if (width < 10) return;

    size_t count = 0;
    const Contact **contacts = contact_service_list_all(&count);

    /* title */
    ncplane_set_fg_rgb(phone, theme_text_primary());
    ncplane_set_bg_rgb(phone, theme_bg());
    ncplane_putstr_yx(phone, CONTENT_START_ROW, CONTENT_COL, "CONTACTS");

    /* separator */
    ncplane_set_fg_rgb(phone, theme_text_muted());
    for (int x = 0; x < width && CONTENT_COL + x < (int)cols - 1; x++)
        ncplane_putstr_yx(phone, CONTENT_START_ROW + 1, CONTENT_COL + x, "\u2500");

    char badge[32];
    snprintf(badge, sizeof(badge), "STATE: %zu", count);
    ncplane_set_fg_rgb(phone, theme_text_muted());
    ncplane_putstr_yx(phone, CONTENT_START_ROW + 2, CONTENT_COL, badge);

    if (count == 0) {
        s_mode = CONTACTS_MODE_LIST;
    }

    if (s_mode == CONTACTS_MODE_PROFILE && count > 0) {
        if (s_selected < 0) s_selected = 0;
        if (s_selected >= (int)count) s_selected = (int)count - 1;

        const Contact *c = contacts[s_selected];
        const char *name = (c && c->name) ? c->name : "Unknown";
        const char *number = (c && c->phone_number) ? c->phone_number : "";

        int card_top = CONTENT_START_ROW + 4;
        int card_h = 8;
        if (card_top + card_h >= footer) card_h = footer - card_top - 1;
        if (card_h < 4) card_h = 4;

        ncplane_set_fg_rgb(phone, theme_text_muted());
        ncplane_set_bg_rgb(phone, theme_bg());
        for (int r = 0; r < card_h; r++) {
            for (int x = CONTENT_COL; x < CONTENT_COL + width; x++) {
                ncplane_putchar_yx(phone, card_top + r, x, ' ');
            }
        }

        for (int x = 0; x < width; x++) {
            ncplane_putstr_yx(phone, card_top, CONTENT_COL + x, "-");
            ncplane_putstr_yx(phone, card_top + card_h - 1, CONTENT_COL + x, "-");
        }

        put_clipped(phone, card_top + 2, CONTENT_COL + 2, width - 4, "Name:");
        put_clipped(phone, card_top + 2, CONTENT_COL + 9, width - 11, name);
        put_clipped(phone, card_top + 3, CONTENT_COL + 2, width - 4, "Phone:");
        put_clipped(phone, card_top + 3, CONTENT_COL + 9, width - 11, number);

        ghost_text(phone, card_top + 4, CONTENT_COL + 2, theme_text_muted(),
                   s_action == 0 ? "> CALL" : "  CALL");
        ghost_text(phone, card_top + 5, CONTENT_COL + 2, theme_text_muted(),
                   s_action == 1 ? "> MESSAGE" : "  MESSAGE");

        ghost_text(phone, footer - 1, CONTENT_COL, theme_text_muted(), "Up/Down:Action  Enter:Send");
        ghost_softkeys(phone, "[Back]", "[Send]");
        return;
    }

    int list_start = CONTENT_START_ROW + 4;
    int card_h = 3;
    int visible = (footer - list_start) / card_h;
    if (visible < 1) visible = 1;

    if (count == 0) {
        s_selected = 0;
        s_scroll = 0;
    } else {
        if (s_selected < 0) s_selected = 0;
        if (s_selected >= (int)count) s_selected = (int)count - 1;
        if (s_selected < s_scroll) s_scroll = s_selected;
        if (s_selected >= s_scroll + visible) s_scroll = s_selected - visible + 1;
        if (s_scroll < 0) s_scroll = 0;
    }

    if (count == 0) {
        int mid = (list_start + footer) / 2;
        ncplane_set_fg_rgb(phone, theme_text_muted());
        ncplane_set_bg_rgb(phone, theme_bg());
        ncplane_putstr_yx(phone, mid, CONTENT_COL, "No contacts yet");
        ncplane_putstr_yx(phone, mid + 1, CONTENT_COL, "Press Right Soft to add");
    } else {
        for (int i = 0; i < visible; i++) {
            int idx = s_scroll + i;
            if (idx >= (int)count) break;

            const Contact *c = contacts[idx];
            int top = list_start + (i * card_h);
            int sel = (idx == s_selected);

            uint32_t card_fg = sel ? theme_text_primary() : theme_text_muted();
            ncplane_set_bg_rgb(phone, theme_bg());
            ncplane_set_fg_rgb(phone, card_fg);

            for (int r = 0; r < card_h - 1; r++) {
                for (int x = CONTENT_COL; x < CONTENT_COL + width; x++) {
                    ncplane_putchar_yx(phone, top + r, x, ' ');
                }
            }

            ncplane_putstr_yx(phone, top, CONTENT_COL, sel ? MENU_CURSOR : MENU_CURSOR_BLANK);
            put_clipped(phone, top, CONTENT_COL + 2, width - 2, c->name ? c->name : "");

            ncplane_set_fg_rgb(phone, theme_text_muted());
            put_clipped(phone, top + 1, CONTENT_COL + 2, width - 2, c->phone_number ? c->phone_number : "");
        }
    }

    ghost_text(phone, footer - 1, CONTENT_COL, theme_text_muted(), "Left:Delete  Enter:Open");
    ghost_softkeys(phone, "[Back]", "[Add]");
    if (s_delete_prompt && s_mode == CONTACTS_MODE_LIST) draw_delete_popup(phone, rows, cols);
}

screen_id screen_contacts_input(uint32_t key) {
    size_t count = 0;
    contact_service_list_all(&count);

    if (s_delete_prompt && s_mode == CONTACTS_MODE_LIST) {
        switch (key) {
            case NCKEY_LEFT:
            case NCKEY_UP:
                s_delete_yes = 0;
                return SCREEN_CONTACTS;
            case NCKEY_RIGHT:
            case NCKEY_DOWN:
                s_delete_yes = 1;
                return SCREEN_CONTACTS;
            case NCKEY_ENTER:
            case '\n':
            case 'e':
            case 'E':
                if (s_delete_yes && count > 0) {
                    size_t cc = 0;
                    const Contact **cl = contact_service_list_all(&cc);
                    if (cl && s_selected < (int)cc && cl[s_selected] && cl[s_selected]->id) {
                        contact_service_delete(cl[s_selected]->id);
                    }
                    count = 0;
                    contact_service_list_all(&count);
                    if (s_selected >= (int)count && s_selected > 0) s_selected--;
                }
                s_delete_prompt = 0;
                s_delete_yes = 0;
                return SCREEN_CONTACTS;
            case 'q':
            case 'Q':
                s_delete_prompt = 0;
                s_delete_yes = 0;
                return SCREEN_CONTACTS;
            default:
                return SCREEN_CONTACTS;
        }
    }

    switch (key) {
        case NCKEY_UP:
            if (s_mode == CONTACTS_MODE_PROFILE) {
                if (s_action > 0) s_action--;
            } else if (count > 0 && s_selected > 0) {
                s_selected--;
            }
            return SCREEN_CONTACTS;
        case NCKEY_DOWN:
            if (s_mode == CONTACTS_MODE_PROFILE) {
                if (s_action < 1) s_action++;
            } else if (count > 0 && s_selected < (int)count - 1) {
                s_selected++;
            }
            return SCREEN_CONTACTS;
        case NCKEY_RIGHT:
        case 'e':
        case 'E': {
            if (s_mode == CONTACTS_MODE_PROFILE) {
                if (count > 0) {
                    const Contact **cl = contact_service_list_all(&count);
                    if (cl && cl[s_selected]) {
                        const char *name = cl[s_selected]->name ? cl[s_selected]->name : "Unknown";
                        if (s_action == 0) {
                            comm_service_call_add(name, "outgoing");
                            return SCREEN_CALLS;
                        }
                        char body[64];
                        snprintf(body, sizeof(body), "Hey %s", name);
                        comm_service_message_add(name, body);
                        return SCREEN_MESSAGES;
                    }
                }
                return SCREEN_CONTACTS;
            }

            char name[64], number[64];
            snprintf(name, sizeof(name), "Contact %d", s_seed);
            snprintf(number, sizeof(number), "+1-202-555-%04d", 1000 + (s_seed % 9000));
            s_seed++;
            contact_service_create(name, number);
            s_selected = 0; s_scroll = 0;
            return SCREEN_CONTACTS;
        }
        case NCKEY_ENTER:
        case '\n': {
            if (s_mode == CONTACTS_MODE_PROFILE) {
                if (count > 0) {
                    const Contact **cl = contact_service_list_all(&count);
                    if (cl && cl[s_selected]) {
                        const char *name = cl[s_selected]->name ? cl[s_selected]->name : "Unknown";
                        if (s_action == 0) {
                            comm_service_call_add(name, "outgoing");
                            return SCREEN_CALLS;
                        }
                        char body[64];
                        snprintf(body, sizeof(body), "Hey %s", name);
                        comm_service_message_add(name, body);
                        return SCREEN_MESSAGES;
                    }
                }
                return SCREEN_CONTACTS;
            }
            if (count > 0) {
                s_mode = CONTACTS_MODE_PROFILE;
                s_action = 0;
            }
            return SCREEN_CONTACTS;
        }
        case NCKEY_LEFT: {
            if (s_mode == CONTACTS_MODE_PROFILE) {
                if (s_action > 0) s_action--;
                return SCREEN_CONTACTS;
            }
            if (count == 0) return SCREEN_CONTACTS;
            s_delete_prompt = 1;
            s_delete_yes = 0;
            return SCREEN_CONTACTS;
        }
        case 'q':
        case 'Q':
            if (s_mode == CONTACTS_MODE_PROFILE) {
                s_mode = CONTACTS_MODE_LIST;
                s_action = 0;
                return SCREEN_CONTACTS;
            }
            return SCREEN_HOME;
        default:
            return SCREEN_CONTACTS;
    }
}
