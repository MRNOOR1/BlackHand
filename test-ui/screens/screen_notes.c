#include <notcurses/notcurses.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "config.h"
#include "draw_utils.h"
#include "bh_skin.h"
#include "ui.h"
#include "services/notes_service.h"
#include "services/multitap_service.h"
#include "services/theme_service.h"

/* ── Notes screen modes ──────────────────────────────────────────────────
 *
 * LIST mode:
 *   up/down    = select note
 *   left       = delete selected note
 *   right      = edit selected note
 *   center     = view note (read-only)
 *   LSK (q)    = back to menu
 *   RSK (e)    = create new note
 *
 * VIEW mode (read-only):
 *   up/down    = scroll content
 *   LSK (q)    = back (prompts save/rename)
 *   RSK (e)    = enable editing
 *   On exit    = always prompted to save name or update
 *
 * EDIT mode:
 *   multi-tap text entry
 *   LSK (Q)    = cancel
 *   RSK (E)    = save
 *
 * SAVE_PROMPT mode:
 *   shown when exiting view - allows renaming the note
 *   LSK (q)    = discard name change
 *   RSK (e)    = save name
 * ────────────────────────────────────────────────────────────────────── */

typedef enum {
    NOTES_MODE_LIST,
    NOTES_MODE_VIEW,
    NOTES_MODE_EDIT,
    NOTES_MODE_SAVE_PROMPT,
} notes_mode_t;

static notes_mode_t mode = NOTES_MODE_LIST;
static int selected = 0;
static int list_scroll = 0;
static int scroll_offset = 0;

/* Delete confirmation */
static int delete_prompt = 0;
static int delete_choice_yes = 0;
static int delete_target = -1;

/* Edit state */
static int edit_is_new = 1;
static int edit_index = -1;
static char edit_body[2048];
static multitap_state s_multitap;

/* Save/rename prompt state */
static char save_name_buf[128];
static int save_name_cursor = 0;
static multitap_state s_save_multitap;
static char save_target_filename[256];
static int save_name_pristine = 0;

static void put_clipped(struct ncplane *p, int row, int col, int max_w, const char *text) {
    if (!text || max_w <= 0) return;
    char buf[512];
    snprintf(buf, sizeof(buf), "%s", text);
    if ((int)strlen(buf) > max_w) {
        if (max_w > 3) { buf[max_w - 3] = '.'; buf[max_w - 2] = '.'; buf[max_w - 1] = '.'; }
        buf[max_w] = '\0';
    }
    ncplane_putstr_yx(p, row, col, buf);
}

static void draw_delete_popup(struct ncplane *phone) {
    ghost_confirm_popup(phone, "DELETE NOTE?", delete_choice_yes);
}

static void begin_new_note_edit(void) {
    edit_is_new = 1;
    edit_index = -1;
    snprintf(edit_body, sizeof(edit_body), "");
    multitap_reset(&s_multitap);
    s_multitap.field = 0;
    mode = NOTES_MODE_EDIT;
}

static void begin_edit_existing(int index) {
    size_t count = notes_service_note_count();
    if (index < 0 || index >= (int)count) return;
    Note **notes = notes_service_list_all(NULL);
    if (!notes || !notes[index]) return;

    edit_is_new = 0;
    edit_index = index;
    snprintf(edit_body, sizeof(edit_body), "%s",
             notes[index]->content ? notes[index]->content : "");
    multitap_reset(&s_multitap);
    s_multitap.field = 0;
    mode = NOTES_MODE_EDIT;
}

static void enter_save_prompt(int index) {
    size_t count = notes_service_note_count();
    if (index < 0 || index >= (int)count) {
        mode = NOTES_MODE_LIST;
        return;
    }
    Note **notes = notes_service_list_all(NULL);
    if (!notes || !notes[index]) {
        mode = NOTES_MODE_LIST;
        return;
    }
    /* Pre-fill with current title, highlighted for easy replacement */
    snprintf(save_name_buf, sizeof(save_name_buf), "%s",
             notes[index]->title ? notes[index]->title : "Untitled");
    snprintf(save_target_filename, sizeof(save_target_filename), "%s",
             notes[index]->filename ? notes[index]->filename : "");
    save_name_cursor = (int)strlen(save_name_buf);
    save_name_pristine = 1;
    multitap_init(&s_save_multitap);
    mode = NOTES_MODE_SAVE_PROMPT;
}

static void save_edit(void) {
    const char *body = edit_body;

    if (edit_is_new) {
        /* Create note - will go to save prompt after */
        Note *n = notes_service_create("Untitled", body);
        if (n) {
            selected = 0;
            list_scroll = 0;
            /* Go to save prompt to name it */
            enter_save_prompt(0);
            return;
        }
    } else {
        size_t count = notes_service_note_count();
        if (edit_index >= 0 && edit_index < (int)count) {
            Note **notes = notes_service_list_all(NULL);
            if (notes && notes[edit_index]) {
                Note *n = notes[edit_index];
                free(n->content);
                n->content = strdup(body);
                notes_service_update_note(n);
                selected = 0;
                list_scroll = 0;
            }
        }
        /* Go to save prompt */
        enter_save_prompt(0);
        return;
    }

    mode = NOTES_MODE_LIST;
}

/* ── DRAW: List mode ─────────────────────────────────────────────────── */
static void draw_list(struct ncplane *phone) {
    unsigned rows, cols;
    ncplane_dim_yx(phone, &rows, &cols);
    int footer = (int)rows - FOOTER_ROW_OFFSET;
    int width = INNER_WIDTH(cols);
    if (width < 10) return;

    size_t count = notes_service_note_count();
    if (count == 0) {
        int mid = (CONTENT_START_ROW + footer) / 2;
        ghost_text(phone, mid, CONTENT_COL, theme_text_muted(), "No notes yet");
        ghost_text(phone, mid + 1, CONTENT_COL, theme_text_muted(), "Press E to create");
        ghost_softkeys(phone, NULL, NULL);
        if (delete_prompt) draw_delete_popup(phone);
        return;
    }

    if (selected < 0) selected = 0;
    if (selected >= (int)count) selected = (int)count - 1;

    Note **notes = notes_service_list_all(NULL);
    if (!notes) return;

    int list_start = CONTENT_START_ROW;
    int max_visible = footer - list_start - 2;
    if (max_visible < 1) max_visible = 1;

    if (selected < list_scroll) list_scroll = selected;
    if (selected >= list_scroll + max_visible) list_scroll = selected - max_visible + 1;
    if (list_scroll < 0) list_scroll = 0;

    for (int i = 0; i < max_visible; i++) {
        int idx = list_scroll + i;
        if (idx >= (int)count) break;
        int row = list_start + i;
        int sel = (idx == selected);

        char nm[64];
        snprintf(nm, sizeof(nm), "%s", notes[idx]->title ? notes[idx]->title : "Untitled");
        int maxlbl = width - 4;
        if (maxlbl > 0 && (int)strlen(nm) > maxlbl) nm[maxlbl] = '\0';
        bh_list_item(phone, row, CONTENT_COL, width, nm, "", sel, idx);
    }

    ghost_softkeys(phone, NULL, NULL);
    ghost_text(phone, (int)rows - 3, CONTENT_COL, theme_text_muted(),
               "A:View  E:New  D:Delete");
    if (delete_prompt) draw_delete_popup(phone);
}

/* ── DRAW: View mode (read-only) ─────────────────────────────────────── */
static void draw_view(struct ncplane *phone) {
    unsigned rows, cols;
    ncplane_dim_yx(phone, &rows, &cols);
    int footer = (int)rows - FOOTER_ROW_OFFSET;
    int width = INNER_WIDTH(cols);
    if (width < 10) return;

    size_t count = notes_service_note_count();
    if (selected < 0 || selected >= (int)count) { mode = NOTES_MODE_LIST; return; }
    Note **notes = notes_service_list_all(NULL);
    if (!notes || !notes[selected]) { mode = NOTES_MODE_LIST; return; }
    Note *n = notes[selected];

    /* No title shown in view - content only as per spec */
    int content_start = CONTENT_START_ROW;
    int max_lines = footer - content_start - 1;
    if (max_lines < 1) max_lines = 1;

    if (n->content && n->content[0] != '\0') {
        const char *ptr = n->content;
        int line_num = 0;
        int drawn = 0;
        while (*ptr && drawn < max_lines) {
            const char *eol = strchr(ptr, '\n');
            int line_len = eol ? (int)(eol - ptr) : (int)strlen(ptr);
            if (line_num >= scroll_offset) {
                char line[256];
                int copy_len = line_len;
                if (copy_len > width) copy_len = width;
                if (copy_len > 255) copy_len = 255;
                memcpy(line, ptr, copy_len);
                line[copy_len] = '\0';
                ncplane_set_fg_rgb(phone, theme_text_primary());
                ncplane_putstr_yx(phone, content_start + drawn, CONTENT_COL, line);
                drawn++;
            }
            line_num++;
            if (eol) ptr = eol + 1; else break;
        }
    } else {
        ghost_text(phone, content_start, CONTENT_COL, theme_text_muted(), "(empty)");
    }

    ghost_softkeys(phone, "[Back]", "[Edit]");
}

/* ── DRAW: Edit mode ─────────────────────────────────────────────────── */
static void draw_edit(struct ncplane *phone) {
    unsigned rows, cols;
    ncplane_dim_yx(phone, &rows, &cols);
    int footer = (int)rows - FOOTER_ROW_OFFSET;
    int width = INNER_WIDTH(cols);

    ncplane_set_fg_rgb(phone, theme_text_primary());
    ncplane_set_bg_rgb(phone, theme_bg());
    ncplane_putstr_yx(phone, CONTENT_START_ROW, CONTENT_COL,
                      edit_is_new ? "NEW NOTE" : "EDIT NOTE");

    int body_row = CONTENT_START_ROW + 1;
    int body_max = footer - body_row - 1;
    if (body_max < 1) body_max = 1;

    const char *ptr = edit_body;
    for (int i = 0; i < body_max; i++) {
        if (!ptr || *ptr == '\0') break;
        const char *eol = strchr(ptr, '\n');
        int len = eol ? (int)(eol - ptr) : (int)strlen(ptr);
        char line[256];
        int copy_len = len;
        if (copy_len > width) copy_len = width;
        if (copy_len > 255) copy_len = 255;
        memcpy(line, ptr, copy_len);
        line[copy_len] = '\0';
        ncplane_set_fg_rgb(phone, theme_text_primary());
        ncplane_putstr_yx(phone, body_row + i, CONTENT_COL, line);
        if (!eol) break;
        ptr = eol + 1;
    }

    ghost_softkeys(phone, NULL, NULL);
    ghost_text(phone, (int)rows - 3, CONTENT_COL, theme_text_muted(),
               "2-9:ABC  A:Save  D:Back  Q:Discard");
}

/* ── DRAW: Save/Rename prompt ────────────────────────────────────────── */
static void draw_save_prompt(struct ncplane *phone) {
    unsigned rows, cols;
    ncplane_dim_yx(phone, &rows, &cols);
    int width = INNER_WIDTH(cols);

    int w = width + 2;
    int h = NOTES_SAVE_POPUP_HEIGHT;
    int top = ((int)rows - h) / 2;
    int left = ((int)cols - w) / 2;
    if (top < UI_POPUP_MIN_TOP) top = UI_POPUP_MIN_TOP;
    if (left < UI_POPUP_MIN_LEFT) left = UI_POPUP_MIN_LEFT;

    ghost_fill_rect(phone, top, left, h, w, ' ', theme_text_primary(), theme_bg());
    ghost_text(phone, top + UI_POPUP_TITLE_ROW_OFFSET, left + UI_POPUP_TEXT_INSET_X,
               theme_text_primary(), "SAVE NOTE AS:");
    ghost_text(phone, top + UI_POPUP_INPUT_ROW_OFFSET, left + UI_POPUP_TEXT_INSET_X,
               theme_text_primary(), save_name_buf);

    /* Show cursor */
    int cursor_col = left + UI_POPUP_TEXT_INSET_X + save_name_cursor;
    if (cursor_col < left + w - 1)
        ghost_text(phone, top + UI_POPUP_INPUT_ROW_OFFSET, cursor_col, theme_border(), "_");

    ghost_text(phone, top + UI_POPUP_HINT_ROW_OFFSET, left + UI_POPUP_TEXT_INSET_X,
               theme_text_muted(), "2-9:ABC #:Case *:Punct");
    ghost_softkeys(phone, "[Skip]", "[Save]");
}

/* ── Main draw dispatcher ────────────────────────────────────────────── */
void screen_notes_draw(struct ncplane *phone) {
    switch (mode) {
        case NOTES_MODE_LIST:        draw_list(phone); break;
        case NOTES_MODE_VIEW:        draw_view(phone); break;
        case NOTES_MODE_EDIT:        draw_edit(phone); break;
        case NOTES_MODE_SAVE_PROMPT: draw_save_prompt(phone); break;
    }
}

/* ── Input handling ──────────────────────────────────────────────────── */
screen_id screen_notes_input(uint32_t key) {
    static int s_multitap_ready = 0;
    if (!s_multitap_ready) {
        multitap_init(&s_multitap);
        multitap_init(&s_save_multitap);
        s_multitap_ready = 1;
    }

    size_t count = notes_service_note_count();

    /* ── SAVE PROMPT mode ──────────────────────────────────────────── */
    if (mode == NOTES_MODE_SAVE_PROMPT) {
        switch (key) {
            case KEY_SOFT_RIGHT_ACTION:
            case NCKEY_ENTER:
            case '\n': {
                /* Save the name */
                multitap_reset(&s_save_multitap);
                if (save_name_buf[0] != '\0') {
                    Note **notes = notes_service_list_all(NULL);
                    count = notes_service_note_count();
                    if (count > 0 && notes) {
                        for (size_t i = 0; i < count; i++) {
                            if (notes[i] && notes[i]->filename &&
                                strcmp(notes[i]->filename, save_target_filename) == 0) {
                                notes_service_rename_note(notes[i], save_name_buf);
                                break;
                            }
                        }
                    }
                }
                mode = NOTES_MODE_LIST;
                selected = 0;
                list_scroll = 0;
                save_name_pristine = 0;
                return SCREEN_NOTES;
            }
            case KEY_SOFT_LEFT_ACTION:
                /* Skip rename, go back to list */
                multitap_reset(&s_save_multitap);
                mode = NOTES_MODE_LIST;
                selected = 0;
                list_scroll = 0;
                save_name_pristine = 0;
                return SCREEN_NOTES;
            case NCKEY_BACKSPACE:
            case 127:
                multitap_backspace(&s_save_multitap, 0, save_name_buf);
                save_name_cursor = (int)strlen(save_name_buf);
                return SCREEN_NOTES;
            case '#':
                multitap_toggle_case(&s_save_multitap);
                return SCREEN_NOTES;
            default:
                if ((key >= '0' && key <= '9') || key == '*') {
                    if (save_name_pristine) {
                        save_name_buf[0] = '\0';
                        save_name_pristine = 0;
                    }
                    multitap_apply_key(&s_save_multitap, key, 0,
                                       save_name_buf, sizeof(save_name_buf));
                    save_name_cursor = (int)strlen(save_name_buf);
                    return SCREEN_NOTES;
                }
                if (key >= 32 && key <= 126) {
                    if (save_name_pristine) {
                        save_name_buf[0] = '\0';
                        save_name_pristine = 0;
                    }
                    multitap_reset(&s_save_multitap);
                    size_t len = strlen(save_name_buf);
                    if (len + 1 < sizeof(save_name_buf)) {
                        save_name_buf[len] = (char)key;
                        save_name_buf[len + 1] = '\0';
                    }
                    save_name_cursor = (int)strlen(save_name_buf);
                    return SCREEN_NOTES;
                }
                return SCREEN_NOTES;
        }
    }

    /* ── LIST mode ─────────────────────────────────────────────────── */
    if (mode == NOTES_MODE_LIST) {
        if (delete_prompt) {
            switch (key) {
                case NCKEY_ENTER:
                case '\n':
                    /* A=Yes: delete. */
                    if (delete_target >= 0) {
                        Note **notes = notes_service_list_all(NULL);
                        if (notes && delete_target < (int)count) {
                            notes_service_delete_note(notes[delete_target]);
                        }
                        count = notes_service_note_count();
                        if (selected >= (int)count && selected > 0) selected--;
                    }
                    delete_prompt = 0;
                    delete_target = -1;
                    return SCREEN_NOTES;
                case KEY_SOFT_LEFT_ACTION:
                    /* Q=No: cancel. */
                    delete_prompt = 0;
                    delete_target = -1;
                    return SCREEN_NOTES;
                default:
                    return SCREEN_NOTES;
            }
        }

        switch (key) {
            case NCKEY_UP:
                if (selected > 0) selected--;
                return SCREEN_NOTES;
            case NCKEY_DOWN:
                if (selected < (int)count - 1) selected++;
                return SCREEN_NOTES;
            case NCKEY_ENTER:
            case '\n':
                /* A: view note in read-only mode. */
                if (count > 0) {
                    scroll_offset = 0;
                    mode = NOTES_MODE_VIEW;
                }
                return SCREEN_NOTES;
            case NCKEY_LEFT:
                /* D: delete confirmation. */
                if (count > 0) {
                    delete_prompt = 1;
                    delete_target = selected;
                }
                return SCREEN_NOTES;
            case KEY_SOFT_RIGHT_ACTION:
                /* E: create new note. */
                begin_new_note_edit();
                return SCREEN_NOTES;
            case KEY_SOFT_LEFT_ACTION:
                /* Q: back to home. */
                return SCREEN_HOME;
            default:
                return SCREEN_NOTES;
        }
    }

    /* ── VIEW mode (read-only) ─────────────────────────────────────── */
    if (mode == NOTES_MODE_VIEW) {
        switch (key) {
            case NCKEY_UP:
                if (scroll_offset > 0) scroll_offset--;
                return SCREEN_NOTES;
            case NCKEY_DOWN:
                scroll_offset++;
                return SCREEN_NOTES;
            case KEY_SOFT_RIGHT_ACTION:
                /* RSK = enable editing */
                begin_edit_existing(selected);
                return SCREEN_NOTES;
            case KEY_SOFT_LEFT_ACTION:
                /* LSK = go back, but prompt to save/rename first */
                scroll_offset = 0;
                enter_save_prompt(selected);
                return SCREEN_NOTES;
            default:
                return SCREEN_NOTES;
        }
    }

    /* ── EDIT mode ─────────────────────────────────────────────────── */
    switch (key) {
        case KEY_SOFT_RIGHT_ACTION:
            /* E: save. */
            multitap_reset(&s_multitap);
            save_edit();
            return SCREEN_NOTES;
        case KEY_SOFT_LEFT_ACTION:
            /* Q: discard and return to list. */
            multitap_reset(&s_multitap);
            mode = NOTES_MODE_LIST;
            return SCREEN_NOTES;
        case 'a':
        case 'A':
            /* A: save (text entry mode — raw key passes through). */
            multitap_reset(&s_multitap);
            save_edit();
            return SCREEN_NOTES;
        case 'd':
        case 'D':
            /* D: backspace (text entry mode — raw key passes through). */
            multitap_backspace(&s_multitap, 0, edit_body);
            return SCREEN_NOTES;
        case NCKEY_ENTER:
        case '\n': {
            multitap_reset(&s_multitap);
            size_t len = strlen(edit_body);
            if (len + 1 < sizeof(edit_body)) {
                edit_body[len] = '\n';
                edit_body[len + 1] = '\0';
            }
            return SCREEN_NOTES;
        }
        case NCKEY_BACKSPACE:
        case 127:
            multitap_backspace(&s_multitap, 0, edit_body);
            return SCREEN_NOTES;
        case '#':
            multitap_toggle_case(&s_multitap);
            return SCREEN_NOTES;
        default:
            if ((key >= '0' && key <= '9') || key == '*') {
                multitap_apply_key(&s_multitap, key, 0,
                                   edit_body, sizeof(edit_body));
                return SCREEN_NOTES;
            }
            if (key >= 32 && key <= 126) {
                multitap_reset(&s_multitap);
                size_t len = strlen(edit_body);
                if (len + 1 < sizeof(edit_body)) {
                    edit_body[len] = (char)key;
                    edit_body[len + 1] = '\0';
                }
            }
            return SCREEN_NOTES;
    }
}

int screen_notes_is_edit_mode(void) {
    return (mode == NOTES_MODE_EDIT || mode == NOTES_MODE_SAVE_PROMPT) ? 1 : 0;
}
