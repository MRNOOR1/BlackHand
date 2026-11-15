#ifndef BLACKHAND_DRAFT_SERVICE_H
#define BLACKHAND_DRAFT_SERVICE_H
#include <stddef.h>
/* One pending SMS; callers must resolve it before switching recipients. */
int draft_service_load(char *number, size_t number_size, char *body, size_t body_size);
int draft_service_save(const char *number, const char *body);
#endif
