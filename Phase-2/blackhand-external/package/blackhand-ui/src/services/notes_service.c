#define _POSIX_C_SOURCE 200809L
#include "notes_service.h"
#include "file_store.h"
#include <unistd.h>
#include "../app_paths.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#include <time.h>
#include <ctype.h>

#define INITIAL_NOTES_CAPACITY 16

static Note **notes_index = NULL;
static size_t notes_count = 0;
static size_t notes_capacity = 0;
static const char *NOTES_PATH = APP_PATH_NOTES_DIR;

static int ensure_capacity(void)
{
	if (notes_count < notes_capacity)
		return 0;
	size_t new_capacity = (notes_capacity == 0) ? INITIAL_NOTES_CAPACITY : notes_capacity * 2;
	Note **new_array = realloc(notes_index, new_capacity * sizeof(Note *));
	if (!new_array)
		return -1;
	notes_index = new_array;
	notes_capacity = new_capacity;
	return 0;
}

static void insert_at_front(Note *note)
{
	for (size_t j = notes_count; j > 0; j--)
		notes_index[j] = notes_index[j - 1];
	notes_index[0] = note;
	notes_count++;
}

/* Extract display title from filename by removing .md extension */
static char *title_from_filename(const char *filename)
{
	if (!filename) return strdup("Untitled");
	char *copy = strdup(filename);
	if (!copy) return strdup("Untitled");
	char *dot = strrchr(copy, '.');
	if (dot && strcmp(dot, ".md") == 0)
		*dot = '\0';
	/* Replace underscores with spaces for display */
	for (char *p = copy; *p; p++) {
		if (*p == '_') *p = ' ';
	}
	return copy;
}

/* Create a safe filename from a title string */
static void make_safe_filename(const char *title, char *out, size_t out_size)
{
	if (!title || !out || out_size < 8) {
		if (out && out_size > 0) out[0] = '\0';
		return;
	}

	/* Cap the base name well under the 256-byte out[] bound and, more
	   importantly, under the filesystem's NAME_MAX (255 on ext4/APFS).
	   file_store_begin() appends ".tmpXXXXXX" (10 bytes) to the filename
	   for its temp file, and the duplicate-title path below can append
	   "_NNN" (up to 5 bytes) before ".md" (3 bytes). Without this cap, a
	   long-but-plausible title (e.g. ~250 alnum chars) produced a
	   filename that was itself under 256 bytes but became too long for
	   mkstemp() once ".tmpXXXXXX" was appended, so notes_service_create()
	   silently returned NULL and the note was never saved. */
#define NOTES_SAFE_BASENAME_LEN 200
	size_t j = 0;
	int last_underscore = 0;
	for (size_t i = 0; title[i] != '\0' && j + 5 < out_size && j < NOTES_SAFE_BASENAME_LEN; i++) {
		unsigned char ch = (unsigned char)title[i];
		if (isalnum(ch)) {
			out[j++] = (char)ch;
			last_underscore = 0;
		} else if (ch == ' ' || ch == '-' || ch == '_') {
			if (!last_underscore && j > 0) {
				out[j++] = '_';
				last_underscore = 1;
			}
		}
	}
	while (j > 0 && out[j - 1] == '_') j--;
	if (j == 0) {
		/* fallback to timestamp */
		time_t now = time(NULL);
		struct tm tm_now;
		localtime_r(&now, &tm_now);
		j = (size_t)snprintf(out, out_size, "note_%04d%02d%02d_%02d%02d%02d",
		                      tm_now.tm_year + 1900, tm_now.tm_mon + 1, tm_now.tm_mday,
		                      tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec);
	}
	out[j] = '\0';
	/* Append .md */
	size_t remain = out_size - strlen(out) - 1;
	if (remain >= 3)
		strncat(out, ".md", remain);
}

/* Check if a path exists */
static int path_exists(const char *p)
{
	struct stat st;
	return stat(p, &st) == 0;
}

void notes_service_init(void)
{
	notes_index = malloc(INITIAL_NOTES_CAPACITY * sizeof(Note *));
	if (!notes_index)
		return;
	notes_count = 0;
	notes_capacity = INITIAL_NOTES_CAPACITY;

	struct stat st = {0};
	if (stat(NOTES_PATH, &st) == -1) {
		if (mkdir(NOTES_PATH, 0700) != 0) {
			perror("Failed to create notes directory");
			return;
		}
	}

	DIR *dir = opendir(NOTES_PATH);
	if (!dir) {
		perror("Could not open notes directory");
		return;
	}

	struct dirent *entry;
	while ((entry = readdir(dir)) != NULL) {
		if (entry->d_name[0] == '.')
			continue;
		const char *ext = strrchr(entry->d_name, '.');
		if (!ext || strcmp(ext, ".md") != 0)
			continue;

		char filepath[1024];
		snprintf(filepath, sizeof(filepath), "%s/%s", NOTES_PATH, entry->d_name);

		FILE *f = fopen(filepath, "r");
		if (!f) continue;

		/* Get file size */
		fseek(f, 0, SEEK_END);
		long file_size = ftell(f);
		fseek(f, 0, SEEK_SET);

		Note *new_note = malloc(sizeof(Note));
		if (!new_note) { fclose(f); continue; }

		new_note->filename = strdup(entry->d_name);
		new_note->title = title_from_filename(entry->d_name);

		/* Read entire file as content (no metadata header) */
		if (file_size > 0) {
			new_note->content = malloc((size_t)file_size + 1);
			if (new_note->content) {
				size_t read_bytes = fread(new_note->content, 1, (size_t)file_size, f);
				new_note->content[read_bytes] = '\0';
			} else {
				new_note->content = strdup("");
			}
		} else {
			new_note->content = strdup("");
		}

		fclose(f);

		if (ensure_capacity() != 0) {
			free(new_note->filename);
			free(new_note->title);
			free(new_note->content);
			free(new_note);
			continue;
		}
		insert_at_front(new_note);
	}

	closedir(dir);
}

Note *notes_service_create(const char *title, const char *content)
{
	Note *new_note = malloc(sizeof(Note));
	if (!new_note) return NULL;

	const char *use_title = (title && title[0] != '\0') ? title : "Untitled";

	char filename[256];
	make_safe_filename(use_title, filename, sizeof(filename));

	/* Handle duplicates: append _N if file exists */
	char filepath[1024];
	snprintf(filepath, sizeof(filepath), "%s/%s", NOTES_PATH, filename);
	if (path_exists(filepath)) {
		char base[256];
		snprintf(base, sizeof(base), "%s", filename);
		char *dot = strrchr(base, '.');
		if (dot) *dot = '\0';
		for (int suffix = 1; suffix < 1000; suffix++) {
			snprintf(filename, sizeof(filename), "%s_%d.md", base, suffix);
			snprintf(filepath, sizeof(filepath), "%s/%s", NOTES_PATH, filename);
			if (!path_exists(filepath)) break;
		}
	}

	if (path_exists(filepath)) { free(new_note); return NULL; }
	new_note->filename = strdup(filename);
	new_note->title = title_from_filename(filename);
	new_note->content = strdup(content ? content : "");

	if (!new_note->filename || !new_note->title || !new_note->content || ensure_capacity() != 0) {
		free(new_note->filename);
		free(new_note->title);
		free(new_note->content);
		free(new_note);
		return NULL;
	}
	/* Write content-only file BEFORE adding to the index. If the write
	   fails, the caller gets NULL back and the note never appears in the
	   list — that matches what they'd see on next boot anyway (since
	   nothing was on disk). Old behaviour silently inserted into the
	   in-memory index and the note would vanish on reboot. */
	snprintf(filepath, sizeof(filepath), "%s/%s", NOTES_PATH, new_note->filename);
	char temp[1100];
	FILE *f = file_store_begin(filepath, temp, sizeof(temp));
	int saved = 0;
	if (f) {
		fputs(new_note->content, f);
		saved = file_store_commit(f, temp, filepath, 1) == 0;
	}
	if (!saved) {
		free(new_note->filename);
		free(new_note->title);
		free(new_note->content);
		free(new_note);
		return NULL;
	}

	insert_at_front(new_note);
	return new_note;
}

size_t notes_service_note_count(void)
{
	return notes_count;
}

Note **notes_service_list_all(size_t *out_count)
{
	if (out_count) *out_count = notes_count;
	return notes_index;
}

int notes_service_delete_note(const Note *n)
{
	if (!n || notes_count == 0)
		return 1;
	for (size_t i = 0; i < notes_count; i++) {
		Note *note = notes_index[i];
		if (strcmp(note->filename, n->filename) == 0) {
			char filepath[1024];
			snprintf(filepath, sizeof(filepath), "%s/%s", NOTES_PATH, note->filename);
			if (unlink(filepath) != 0 && errno != ENOENT) return 1;

			free(note->filename);
			free(note->title);
			free(note->content);
			free(note);

			for (size_t j = i; j < notes_count - 1; j++)
				notes_index[j] = notes_index[j + 1];
			notes_count--;
			return 0;
		}
	}
	return 1;
}

int notes_service_update_note(Note *n)
{
	if (!n || notes_count == 0)
		return 1;

	for (size_t i = 0; i < notes_count; i++) {
		Note *note = notes_index[i];
		if (strcmp(note->filename, n->filename) == 0) {
			char *content = strdup(n->content ? n->content : "");
			if (!content) return 1;
			char filepath[1024], temp[1100];
			snprintf(filepath, sizeof(filepath), "%s/%s", NOTES_PATH, note->filename);
			FILE *f = file_store_begin(filepath, temp, sizeof(temp));
			if (!f) { free(content); return 1; }
			fputs(content, f);
			if (file_store_commit(f, temp, filepath, 0) != 0) {
				free(content);
				return 1;
			}
			free(note->content);
			note->content = content;
			for (size_t j = i; j > 0; j--) notes_index[j] = notes_index[j - 1];
			notes_index[0] = note;
			return 0;
		}
	}
	return 1;
}

int notes_service_rename_note(Note *n, const char *new_title)
{
	if (!n || !new_title || new_title[0] == '\0')
		return -1;

	/* Find the note in our index */
	int found = 0;
	for (size_t i = 0; i < notes_count; i++) {
		if (notes_index[i] == n || strcmp(notes_index[i]->filename, n->filename) == 0) {
			n = notes_index[i];
			found = 1;
			break;
		}
	}
	if (!found) return -1;

	/* Build new filename from title */
	char new_filename[256];
	make_safe_filename(new_title, new_filename, sizeof(new_filename));

	/* If same filename, just update the display title */
	if (strcmp(n->filename, new_filename) == 0) {
		free(n->title);
		n->title = title_from_filename(new_filename);
		return 0;
	}

	/* Handle duplicate filenames */
	char filepath[1024];
	snprintf(filepath, sizeof(filepath), "%s/%s", NOTES_PATH, new_filename);
	if (path_exists(filepath)) {
		char base[256];
		snprintf(base, sizeof(base), "%s", new_filename);
		char *dot = strrchr(base, '.');
		if (dot) *dot = '\0';
		for (int suffix = 1; suffix < 1000; suffix++) {
			snprintf(new_filename, sizeof(new_filename), "%s_%d.md", base, suffix);
			snprintf(filepath, sizeof(filepath), "%s/%s", NOTES_PATH, new_filename);
			if (!path_exists(filepath)) break;
		}
	}

	/* Rename on disk */
	char old_path[1024], new_path[1024];
	snprintf(old_path, sizeof(old_path), "%s/%s", NOTES_PATH, n->filename);
	snprintf(new_path, sizeof(new_path), "%s/%s", NOTES_PATH, new_filename);

	char *filename = strdup(new_filename);
	char *title = title_from_filename(new_filename);
	if (!filename || !title) { free(filename); free(title); return -1; }
	if (link(old_path, new_path) != 0) { free(filename); free(title); return -1; }
	if (unlink(old_path) != 0) {
		unlink(new_path); free(filename); free(title); return -1;
	}
	free(n->filename);
	free(n->title);
	n->filename = filename;
	n->title = title;
	return 0;
}

void notes_service_shutdown(void)
{
	if (!notes_index)
		return;
	for (size_t i = 0; i < notes_count; i++) {
		Note *n = notes_index[i];
		if (!n) continue;
		free(n->filename);
		free(n->title);
		free(n->content);
		free(n);
	}
	free(notes_index);
	notes_index = NULL;
	notes_count = 0;
	notes_capacity = 0;
}
