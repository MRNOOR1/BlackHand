#ifndef CONTACT_SERVICE_H
#define CONTACT_SERVICE_H

#include <stddef.h>

typedef struct
{
    char *id;            
    char *name;
    char *phone_number;
} Contact;

void contact_service_init(void);

Contact *contact_service_create(const char *name, const char *number);

int contact_service_update(Contact *c, const char *new_name, const char *new_number);

int contact_service_delete(const char *id);

const Contact **contact_service_list_all(size_t *count);

const Contact *contact_service_get_by_id(const char *id);

void contact_service_shutdown(void);

/* 1 = contacts.json exists but is unreadable; distinct from "0 contacts". */
int contact_service_data_corrupt(void);

/* White Wipe only. Returns -1 fail, 0 clean, 1 a corrupt file was erased. */
int contact_service_wipe_all(void);

#endif
