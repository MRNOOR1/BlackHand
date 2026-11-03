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
/* Closes fd while holding at_cmd_mutex, so a close() can never race an
 * in-flight at_cmd()/at_cmd_sms() on the same fd value (fd-reuse hazard).
 * Use this instead of close_port() when tearing down the AT port while other
 * threads may still be calling at_cmd()/at_cmd_sms() on it. See the .c file. */
int close_port_locked(int fd);
int read_line(int fd, char *buf, int max, int timeout_ms);
const char *at_cmd(int fd, const char *cmd, int timeout_ms);
/* Full AT+CMGS transaction (prompt→body→Ctrl-Z→wait) under one at_cmd_mutex
 * hold, so a concurrent poll can't inject into the SMS body. Returns the final
 * modem response ("+CMGS:" = sent, "+CMS ERROR" = failed, "" = no prompt). */
const char *at_cmd_sms(int fd, const char *number, const char *message,
                       int prompt_timeout_ms, int body_timeout_ms);
const char *modem_active_port(void);
/* Pin probing to one specific device path ("auto"/NULL/"" clears the pin and
 * restores the normal candidate sweep). Takes effect on the next probe. */
void serial_set_forced_port(const char *path);
const char *serial_get_forced_port(void);
#endif
