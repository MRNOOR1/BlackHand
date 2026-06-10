// create the .h file  for the urc functions
#ifndef URC_H
#define URC_H
#include <stdio.h>
#include <string.h>
#include "serial_utility.h"

void urc_start(int fd);
void urc_stop(void);
void *urc_thread_func(void *arg);
int process_message(const char *msg);
void handle_ring(const char *line);
void handle_clip(const char *line);
void handle_cmt(const char *line);
void handle_no_carrier(const char *line);
void handle_busy(const char *line);
void handle_call_begin(const char *line);
void handle_call_end(const char *line);
void handle_missed_call(const char *line);
#endif
