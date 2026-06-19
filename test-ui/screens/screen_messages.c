#include <notcurses/notcurses.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "draw_utils.h"
#include "bh_skin.h"
#include "services/comm_service.h"
#include "services/theme_service.h"
#include "ui.h"

static int s_selected = 0;
static int s_scroll = 0;
static int s_seed = 1;
static int s_delete_prompt = 0;

static void draw_delete_popup(struct ncplane *phone, unsigned rows, unsigned cols) {
    (void)rows;
    (void)cols;
    ghost_confirm_popup(phone, "ARE YOU SURE?", 0);
}

void screen_messages_draw(struct ncplane *phone) {
    unsigned rows, cols;
    ncplane_dim_yx(phone, &rows, &cols);
    int footer = (int)rows - FOOTER_ROW_OFFSET;
    int width = INNER_WIDTH(cols);
    if (width < 10) return;

    size_t count = comm_service_message_count();
    if (s_selected < 0) s_selected = 0;
    if (count > 0 && s_selected >= (int)count) s_selected = (int)count - 1;

    int list_start = CONTENT_START_ROW;
    int row_h = 1;
    int visible = (footer - list_start - 2) / row_h;
    if (visible < 1) visible = 1;

    if (count == 0) {
        s_selected = 0;
        s_scroll = 0;
    }
    if (s_selected < s_scroll) s_scroll = s_selected;
    if (s_selected >= s_scroll + visible) s_scroll = s_selected - visible + 1;
    if (s_scroll < 0) s_scroll = 0;

    if (count == 0) {
        int mid = (list_start + footer - 1) / 2;
        ncplane_set_fg_rgb(phone, theme_text_muted());
        ncplane_putstr_yx(phone, mid - 1, CONTENT_COL, "\u2709  \u2709  \u2709");
        ncplane_putstr_yx(phone, mid + 1, CONTENT_COL, "No messages yet");
        ncplane_putstr_yx(phone, mid + 2, CONTENT_COL, "Press E to create one");
    } else {
        for (int i = 0; i < visible; i++) {
            int idx = s_scroll + i;
            if (idx >= (int)count) break;

            const CommMessage *msg = comm_service_message_at((size_t)idx);
            if (!msg) continue;

            int row = list_start + (i * row_h);
            if (row >= footer - 1) break;

            int selected = (idx == s_selected);
            char nm[16]; snprintf(nm, sizeof(nm), "%.12s", msg->sender[0] ? msg->sender : "?");
            char pv[16]; snprintf(pv, sizeof(pv), "%.8s",  msg->body[0]   ? msg->body   : "");
            bh_list_item(phone, row, CONTENT_COL, width, nm, pv, selected, idx);
        }
    }

    ghost_softkeys(phone, NULL, NULL);
    ghost_text(phone, (int)rows - 3, CONTENT_COL, theme_text_muted(), "A:Open  D:Delete  E:New");
    if (s_delete_prompt) draw_delete_popup(phone, rows, cols);
}

screen_id screen_messages_input(uint32_t key) {
    if (s_delete_prompt) {
        switch (key) {
            case NCKEY_ENTER:
            case '\n':
                /* A=Yes: delete. */
                if (comm_service_message_count() > 0) {
                    comm_service_message_delete((size_t)s_selected);
                    if (s_selected >= (int)comm_service_message_count() && s_selected > 0)
                        s_selected--;
                }
                s_delete_prompt = 0;
                return SCREEN_MESSAGES;
            case KEY_SOFT_LEFT_ACTION:
                /* Q=No: cancel. */
                s_delete_prompt = 0;
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
        case NCKEY_ENTER:
        case '\n':
            /* A: open selected conversation (stay on screen for now). */
            return SCREEN_MESSAGES;
        case KEY_SOFT_RIGHT_ACTION:
            /* E: start a new message. */
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
        case NCKEY_LEFT:
            /* D: delete confirmation. */
            if (comm_service_message_count() > 0) {
                s_delete_prompt = 1;
            }
            return SCREEN_MESSAGES;
        case KEY_SOFT_LEFT_ACTION:
            /* Q: back to home. */
            return SCREEN_HOME;
        default:
            return SCREEN_MESSAGES;
    }
}
