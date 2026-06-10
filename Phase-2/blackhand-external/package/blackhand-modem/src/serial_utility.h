// create a .h for the serial utility functions
#ifndef SERIAL_UTILITY_H
#define SERIAL_UTILITY_H
#include <unistd.h>
#include <stdint.h>
#include <time.h>
#include <fcntl.h>
#include <termios.h>
#include <pthread.h>
#define BUFFERSIZE 1024

extern pthread_mutex_t response_mutex;
extern pthread_cond_t response_cond;
extern char response_buffer[BUFFERSIZE];
extern int response_ready;

int open_port();
/* Single sweep of the candidate list — no internal retry/sleep. Returns the
 * fd on success, -1 if no AT-responding port was found this pass. Used by the
 * background bring-up thread, which owns the retry cadence. */
int open_port_once(void);
int close_port(int fd);
int read_line(int fd, char *buf, int max, int timeout_ms);
const char *at_cmd(int fd, const char *cmd, int timeout_ms);
const char *modem_active_port(void);
/* Pin probing to one specific device path ("auto"/NULL/"" clears the pin and
 * restores the normal candidate sweep). Takes effect on the next probe. */
void serial_set_forced_port(const char *path);
const char *serial_get_forced_port(void);
#endif
