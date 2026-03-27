#ifndef NOTES_SERVICE_H
#define NOTES_SERVICE_H
#include <stddef.h>

/*
 * notes_service.h
 *
 * Notes storage service.
 * - Filename is the note title (e.g. "My Note.md")
 * - File contains only the content (no metadata header)
 * - Notes are stored in ./Notes/
 */

typedef struct {
    char *filename; /* full filename including .md extension */
    char *title;    /* display title = filename without .md */
    char *content;  /* file body (content only, no metadata) */
} Note;

void   notes_service_init(void);
Note  *notes_service_create(const char *title, const char *content);
size_t notes_service_note_count(void);
Note **notes_service_list_all(size_t *out_count);
int    notes_service_delete_note(const Note *n);
int    notes_service_update_note(Note *n);
int    notes_service_rename_note(Note *n, const char *new_title);
void   notes_service_shutdown(void);

#endif
