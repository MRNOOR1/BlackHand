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

void screen_calls_draw(struct ncplane *phone) {
    unsigned rows, cols;
    ncplane_dim_yx(phone, &rows, &cols);
    int footer = (int)rows - FOOTER_ROW_OFFSET;
    int width = INNER_WIDTH(cols);
    if (width < 10) return;

    size_t count = comm_service_call_count();

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
        ghost_text(phone, mid,     CONTENT_COL, theme_text_muted(), "No call history yet");
        ghost_text(phone, mid + 1, CONTENT_COL, theme_text_muted(), "Add from Contacts profile");
        ghost_softkeys(phone, NULL, NULL);
        return;
    }

    /* compact call log */
    for (int i = 0; i < visible; i++) {
        int idx = s_scroll + i;
        if (idx >= (int)count) break;

        const CommCall *call = comm_service_call_at((size_t)idx);
        if (!call) continue;

        int row = list_start + (i * row_h);
        if (row >= footer) break;

        int selected = (idx == s_selected);
        char nm[16]; snprintf(nm, sizeof(nm), "%.12s", call->name[0] ? call->name : "?");
        bh_list_item(phone, row, CONTENT_COL, width, nm,
                     call->time[0] ? call->time : "", selected, idx);
    }

    ghost_softkeys(phone, NULL, NULL);
    ghost_text(phone, (int)rows - 3, CONTENT_COL, theme_text_muted(), "A:Call  D:Delete  E:New");
    if (s_delete_prompt) draw_delete_popup(phone, rows, cols);
}

screen_id screen_calls_input(uint32_t key) {
    if (s_delete_prompt) {
        switch (key) {
            case NCKEY_ENTER:
            case '\n':
                /* A=Yes: delete the selected entry. */
                if (comm_service_call_count() > 0) {
                    comm_service_call_delete((size_t)s_selected);
                    if (s_selected >= (int)comm_service_call_count() && s_selected > 0)
                        s_selected--;
                }
                s_delete_prompt = 0;
                return SCREEN_CALLS;
            case KEY_SOFT_LEFT_ACTION:
                /* Q=No: cancel. */
                s_delete_prompt = 0;
                return SCREEN_CALLS;
            default:
                return SCREEN_CALLS;
        }
    }

    switch (key) {
        case NCKEY_UP:
            if (s_selected > 0) s_selected--;
            return SCREEN_CALLS;
        case NCKEY_DOWN:
            if (s_selected < (int)comm_service_call_count() - 1) s_selected++;
            return SCREEN_CALLS;
        case NCKEY_ENTER:
        case '\n':
            /* A: call (redial) the selected entry. */
            if (comm_service_call_count() > 0) {
                const CommCall *call = comm_service_call_at((size_t)s_selected);
                if (call) {
                    char who[32];
                    snprintf(who, sizeof(who), "%.28s", call->name[0] ? call->name : "Unknown");
                    comm_service_call_add(who, "outgoing");
                }
            }
            return SCREEN_CALLS;
        case KEY_SOFT_RIGHT_ACTION:
            /* E: add a new call entry. */
            {
                char who[32];
                snprintf(who, sizeof(who), "Call %d", s_seed++);
                comm_service_call_add(who, "outgoing");
                s_selected = 0;
                s_scroll = 0;
            }
            return SCREEN_CALLS;
        case NCKEY_LEFT:
            /* D: open delete confirmation. */
            if (comm_service_call_count() > 0) {
                s_delete_prompt = 1;
            }
            return SCREEN_CALLS;
        case KEY_SOFT_LEFT_ACTION:
            /* Q: back to home. */
            return SCREEN_HOME;
        default:
            return SCREEN_CALLS;
    }
}
