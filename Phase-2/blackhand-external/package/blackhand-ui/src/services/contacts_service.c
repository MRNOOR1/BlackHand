#include "contacts_service.h"
#include "../config.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define INITIAL_CONTACT_CAPACITY 16
#define MAX_LINE_LENGTH 256

static Contact **contact_list = NULL;
static size_t contact_count = 0;
static size_t contact_capacity = 0;
static const char *CONTACTS_PATH = APP_PATH_CONTACTS_DIR;

static void free_contact(Contact *c)
{
	if (!c)
		return;

	free(c->id);
	free(c->name);
	free(c->phone_number);
	free(c);
}

static int write_contact_file(const Contact *c)
{
	if (!c || !c->id)
		return -1;

	char filepath[1024];
	snprintf(filepath, sizeof(filepath), "%s/%s", CONTACTS_PATH, c->id);

	FILE *f = fopen(filepath, "w");
	if (!f)
	{
		fprintf(stderr, "Failed to open contact file for write: %s\n", filepath);
		return -1;
	}

	fprintf(f, "Name: %s\n", c->name ? c->name : "");
	fprintf(f, "Phone: %s\n", c->phone_number ? c->phone_number : "");
	fclose(f);

	return 0;
}

static int generate_contact_id(char *buffer, size_t buffer_len)
{
	if (!buffer || buffer_len == 0)
		return -1;

	time_t now = time(NULL);
	struct tm tm_now;
	if (!localtime_r(&now, &tm_now))
		return -1;

	for (size_t attempt = 0; attempt < 1000; attempt++)
	{
		snprintf(buffer,
				 buffer_len,
				 "%04d%02d%02d%02d%02d%02d_%zu.txt",
				 tm_now.tm_year + 1900,
				 tm_now.tm_mon + 1,
				 tm_now.tm_mday,
				 tm_now.tm_hour,
				 tm_now.tm_min,
				 tm_now.tm_sec,
				 attempt);

		if (!contact_service_get_by_id(buffer))
			return 0;
	}

	return -1;
}

static int ensure_capacity(void)
{
	if (contact_count < contact_capacity)
		return 0;

	size_t new_capacity = (contact_capacity == 0) ? INITIAL_CONTACT_CAPACITY : contact_capacity * 2;
	Contact **new_contact = realloc(contact_list, new_capacity * sizeof(Contact *));
	if (!new_contact)
	{
		fprintf(stderr, "Failed to resize Contacts array\n");
		return -1;
	}
	contact_list = new_contact;
	contact_capacity = new_capacity;
	return 0;
}

/* Insert a Contact at index 0 (newest-first) */
static void insert_at_front(Contact *Contact)
{
	for (size_t j = contact_count; j > 0; j--)
		contact_list[j] = contact_list[j - 1];
	contact_list[0] = Contact;
	contact_count++;
}

void contact_service_init(void)
{
	contact_list = malloc(sizeof(Contact *) * INITIAL_CONTACT_CAPACITY);
	if (!contact_list)
	{
		perror("cannot allocate memory for contact list");
		return;
	}

	contact_capacity = INITIAL_CONTACT_CAPACITY;
	contact_count = 0;

	struct stat st = {0};
	if (stat(CONTACTS_PATH, &st) == -1)
	{
		if (mkdir(CONTACTS_PATH, 0755) != 0)
		{
			perror("cannot create a folder for contacts");
			free(contact_list);
			contact_list = NULL;
			contact_capacity = 0;
			return;
		}
	}

	DIR *dir = opendir(CONTACTS_PATH);
	if (!dir)
	{
		perror("cannot open the contact folder");
		free(contact_list);
		contact_list = NULL;
		contact_capacity = 0;
		return;
	}

	struct dirent *entry;
	while ((entry = readdir(dir)) != NULL)
	{
		if (entry->d_name[0] == '.')
			continue;

		const char *ext = strrchr(entry->d_name, '.');
		if (!ext || strcmp(ext, ".txt") != 0)
			continue;

		char filepath[1024];
		snprintf(filepath, sizeof(filepath), "%s/%s", CONTACTS_PATH, entry->d_name);

		FILE *f = fopen(filepath, "r");
		if (!f)
		{
			perror("cannot open contact file");
			continue;
		}

		Contact *new_contact = malloc(sizeof(Contact));
		if (!new_contact)
		{
			perror("cannot allocate memory for contact");
			fclose(f);
			continue;
		}

		new_contact->id = NULL;
		new_contact->name = NULL;
		new_contact->phone_number = NULL;

		new_contact->id = strdup(entry->d_name);
		if (!new_contact->id)
		{
			free(new_contact);
			fclose(f);
			continue;
		}

		char line[MAX_LINE_LENGTH];

		if (fgets(line, sizeof(line), f))
		{
			line[strcspn(line, "\r\n")] = '\0';
			if (strncmp(line, "Name: ", 6) == 0)
			{
				new_contact->name = strdup(line + 6);
			}
		}

		if (fgets(line, sizeof(line), f))
		{
			line[strcspn(line, "\r\n")] = '\0';
			if (strncmp(line, "Phone: ", 7) == 0)
			{
				new_contact->phone_number = strdup(line + 7);
			}
		}

		if (!new_contact->name)
			new_contact->name = strdup("");

		if (!new_contact->phone_number)
			new_contact->phone_number = strdup("");

		if (!new_contact->name || !new_contact->phone_number)
		{
			free(new_contact->id);
			free(new_contact->name);
			free(new_contact->phone_number);
			free(new_contact);
			fclose(f);
			continue;
		}

		fclose(f);

		if (ensure_capacity() != 0)
		{
			free(new_contact->id);
			free(new_contact->name);
			free(new_contact->phone_number);
			free(new_contact);
			continue;
		}

		insert_at_front(new_contact);
	}

	closedir(dir);
}

Contact *contact_service_create(const char *name, const char *number)
{
	if (ensure_capacity() != 0)
		return NULL;

	Contact *new_contact = malloc(sizeof(Contact));
	if (!new_contact)
	{
		fprintf(stderr, "Failed to allocate contact\n");
		return NULL;
	}

	char id[128];
	if (generate_contact_id(id, sizeof(id)) != 0)
	{
		free(new_contact);
		fprintf(stderr, "Failed to generate contact id\n");
		return NULL;
	}

	new_contact->id = strdup(id);
	new_contact->name = strdup(name ? name : "");
	new_contact->phone_number = strdup(number ? number : "");

	if (!new_contact->id || !new_contact->name || !new_contact->phone_number)
	{
		free_contact(new_contact);
		return NULL;
	}

	insert_at_front(new_contact);
	write_contact_file(new_contact);

	return new_contact;
}

int contact_service_update(Contact *c, const char *new_name, const char *new_number)
{
	if (!c)
	{
		fprintf(stderr, "Contact does not exist\n");
		return -1;
	}

	char *updated_name = strdup(new_name ? new_name : "");
	char *updated_number = strdup(new_number ? new_number : "");
	if (!updated_name || !updated_number)
	{
		free(updated_name);
		free(updated_number);
		return -1;
	}

	free(c->name);
	free(c->phone_number);
	c->name = updated_name;
	c->phone_number = updated_number;

	write_contact_file(c);

	return 0;
}

int contact_service_delete(const char *id)
{
	if (!id)
	{
		fprintf(stderr, "Cannot delete contact without id\n");
		return -1;
	}

	for (size_t i = 0; i < contact_count; i++)
	{
		Contact *c = contact_list[i];
		if (strcmp(c->id, id) != 0)
			continue;

		char filepath[1024];
		snprintf(filepath, sizeof(filepath), "%s/%s", CONTACTS_PATH, c->id);
		remove(filepath);

		free_contact(c);

		for (size_t j = i; j < contact_count - 1; j++)
			contact_list[j] = contact_list[j + 1];

		contact_count--;
		return 0;
	}

	return -1;
}

const Contact **contact_service_list_all(size_t *count)
{
	if (count)
		*count = contact_count;

	return (const Contact **)contact_list;
}

const Contact *contact_service_get_by_id(const char *id)
{
	if (!id)
		return NULL;

	for (size_t i = 0; i < contact_count; i++)
	{
		Contact *c = contact_list[i];
		if (strcmp(c->id, id) == 0)
			return c;
	}

	return NULL;
}

void contact_service_shutdown(void)
{
	if (!contact_list)
		return;

	for (size_t i = 0; i < contact_count; i++)
		free_contact(contact_list[i]);

	free(contact_list);
	contact_list = NULL;
	contact_count = 0;
	contact_capacity = 0;
}
