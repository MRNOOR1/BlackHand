#include "notes_service.h"
#include "../config.h"
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

	size_t j = 0;
	int last_underscore = 0;
	for (size_t i = 0; title[i] != '\0' && j + 5 < out_size; i++) {
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
		if (mkdir(NOTES_PATH, 0755) != 0) {
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

	new_note->filename = strdup(filename);
	new_note->title = title_from_filename(filename);
	new_note->content = strdup(content ? content : "");

	if (ensure_capacity() != 0) {
		free(new_note->filename);
		free(new_note->title);
		free(new_note->content);
		free(new_note);
		return NULL;
	}
	insert_at_front(new_note);

	/* Write content-only file */
	snprintf(filepath, sizeof(filepath), "%s/%s", NOTES_PATH, new_note->filename);
	FILE *f = fopen(filepath, "w");
	if (f) {
		fprintf(f, "%s", new_note->content);
		fclose(f);
	}

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
			remove(filepath);

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
			if (n != note) {
				free(note->content);
				note->content = strdup(n->content ? n->content : "");
			}

			/* Move to front (most recently modified) */
			if (i != 0) {
				Note *tmp = note;
				for (size_t j = i; j > 0; j--)
					notes_index[j] = notes_index[j - 1];
				notes_index[0] = tmp;
			}

			/* Write content-only file */
			char filepath[1024];
			snprintf(filepath, sizeof(filepath), "%s/%s", NOTES_PATH, note->filename);
			FILE *f = fopen(filepath, "w");
			if (f) {
				fprintf(f, "%s", note->content);
				fclose(f);
			}
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

	if (rename(old_path, new_path) != 0)
		return -1;

	/* Update in-memory */
	free(n->filename);
	free(n->title);
	n->filename = strdup(new_filename);
	n->title = title_from_filename(new_filename);

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
