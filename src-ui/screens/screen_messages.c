#include <notcurses/notcurses.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "draw_utils.h"
#include "services/comm_service.h"
#include "services/theme_service.h"
#include "ui.h"

static int s_selected = 0;
static int s_scroll = 0;
static int s_seed = 1;
static int s_delete_prompt = 0;
static int s_delete_yes = 0;

static void draw_delete_popup(struct ncplane *phone, unsigned rows, unsigned cols) {
    (void)rows;
    (void)cols;
    ghost_confirm_popup(phone, "ARE YOU SURE?", s_delete_yes);
}

void screen_messages_draw(struct ncplane *phone) {
    unsigned rows, cols;
    ncplane_dim_yx(phone, &rows, &cols);
    int footer = (int)rows - FOOTER_ROW_OFFSET;
    int width = INNER_WIDTH(cols);
    if (width < 10) return;

    /* title */
    ncplane_set_fg_rgb(phone, theme_text_primary());
    ncplane_set_bg_rgb(phone, theme_bg());
    ncplane_putstr_yx(phone, CONTENT_START_ROW, CONTENT_COL, "MESSAGES");

    /* separator */
    ncplane_set_fg_rgb(phone, theme_text_muted());
    for (int x = 0; x < width && CONTENT_COL + x < (int)cols - 1; x++)
        ncplane_putstr_yx(phone, CONTENT_START_ROW + 1, CONTENT_COL + x, "\u2500");

    size_t count = comm_service_message_count();
    if (s_selected < 0) s_selected = 0;
    if (count > 0 && s_selected >= (int)count) s_selected = (int)count - 1;

    ghost_text(phone, CONTENT_START_ROW + 2, CONTENT_COL, theme_text_muted(), "STATE: INBOX");

    int list_start = CONTENT_START_ROW + 4;
    int row_h = 1;
    int visible = (footer - list_start - 1) / row_h;
    if (visible < 1) visible = 1;

    if (count == 0) {
        s_selected = 0;
        s_scroll = 0;
    }
    if (s_selected < s_scroll) s_scroll = s_selected;
    if (s_selected >= s_scroll + visible) s_scroll = s_selected - visible + 1;
    if (s_scroll < 0) s_scroll = 0;

    if (count == 0) {
        int mid = (CONTENT_START_ROW + 2 + footer) / 2;
        ncplane_set_fg_rgb(phone, theme_text_muted());
        ncplane_putstr_yx(phone, mid - 1, CONTENT_COL + width / 2 - 3, "\u2709  \u2709  \u2709");
        ncplane_putstr_yx(phone, mid + 1, CONTENT_COL + width / 2 - 7, "No messages yet");
        ncplane_putstr_yx(phone, mid + 2, CONTENT_COL + width / 2 - 11, "Right soft key creates one");
    } else {
        for (int i = 0; i < visible; i++) {
            int idx = s_scroll + i;
            if (idx >= (int)count) break;

            const CommMessage *msg = comm_service_message_at((size_t)idx);
            if (!msg) continue;

            int row = list_start + (i * row_h);
            if (row >= footer - 1) break;

            int selected = (idx == s_selected);
            char line[256];
            snprintf(line, sizeof(line), "%s %-10s %s",
                     selected ? MENU_CURSOR : MENU_CURSOR_BLANK,
                     msg->sender,
                     msg->body);
            if ((int)strlen(line) > width) line[width] = '\0';
            ncplane_set_fg_rgb(phone, selected ? theme_text_primary() : theme_text_muted());
            ncplane_set_bg_rgb(phone, theme_bg());
            ncplane_putstr_yx(phone, row, CONTENT_COL, line);
        }
    }

    ghost_text(phone, footer - 1, CONTENT_COL, theme_text_muted(), "Left:Delete  Enter:Open");
    ghost_softkeys(phone, "[Back]", "[Add]");
    if (s_delete_prompt) draw_delete_popup(phone, rows, cols);
}

screen_id screen_messages_input(uint32_t key) {
    if (s_delete_prompt) {
        switch (key) {
            case NCKEY_LEFT:
            case NCKEY_UP:
                s_delete_yes = 0;
                return SCREEN_MESSAGES;
            case NCKEY_RIGHT:
            case NCKEY_DOWN:
                s_delete_yes = 1;
                return SCREEN_MESSAGES;
            case NCKEY_ENTER:
            case '\n':
            case 'e':
            case 'E':
                if (s_delete_yes && comm_service_message_count() > 0) {
                    comm_service_message_delete((size_t)s_selected);
                    if (s_selected >= (int)comm_service_message_count() && s_selected > 0) s_selected--;
                }
                s_delete_prompt = 0;
                s_delete_yes = 0;
                return SCREEN_MESSAGES;
            case 'q':
            case 'Q':
                s_delete_prompt = 0;
                s_delete_yes = 0;
                return SCREEN_MESSAGES;
            default:
                return SCREEN_MESSAGES;
        }
    }

    switch (key) {
        case NCKEY_UP:
            if (s_selected > 0) s_selected--;
            return SCREEN_MESSAGES;
        case NCKEY_DOWN:
            if (s_selected < (int)comm_service_message_count() - 1) s_selected++;
            return SCREEN_MESSAGES;
        case NCKEY_RIGHT:
        case 'e':
        case 'E':
            {
            char sender[32];
            char body[64];
            snprintf(sender, sizeof(sender), "Contact %d", s_seed);
            snprintf(body, sizeof(body), "Demo message %d", s_seed);
            s_seed++;
            comm_service_message_add(sender, body);
            s_selected = 0;
            s_scroll = 0;
            }
            return SCREEN_MESSAGES;
        case NCKEY_ENTER:
        case '\n':
            return SCREEN_MESSAGES;
        case NCKEY_LEFT:
            if (comm_service_message_count() > 0) {
                s_delete_prompt = 1;
                s_delete_yes = 0;
            }
            return SCREEN_MESSAGES;
        case 'q':
        case 'Q':
            return SCREEN_HOME;
        default:
            return SCREEN_MESSAGES;
    }
}
