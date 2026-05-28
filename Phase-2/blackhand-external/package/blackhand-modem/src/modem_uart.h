#pragma once

#include <stddef.h>

/*
 * modem_uart — thread-safe serial port driver for /dev/ttyUSB2.
 *
 * One background reader thread reads from the port continuously and routes
 * lines to either the pending command response or the URC queue.
 * Call modem_uart_open() before any other function.
 */

/* Open the serial port, configure 115200 8N1, start the reader thread.
 * Returns 0 on success, -1 on failure. */
int modem_uart_open(const char *port);

/*
 * Send an AT command and wait synchronously for the final response.
 *
 * cmd        — command without trailing CR (e.g. "AT+CMGF=1")
 * response   — buffer that receives body lines before the final OK/ERROR;
 *              pass NULL/0 if you only care about success/failure
 * resp_size  — size of response buffer
 * timeout_ms — milliseconds to wait before returning failure
 *
 * Returns  0  if modem replied OK
 *         -1  if replied ERROR / timed out / port not open
 */
int at_command(const char *cmd, char *response, size_t resp_size, int timeout_ms);

/*
 * Two-step SMS send that handles the AT+CMGS "> " prompt.
 *
 * Internally: sends AT+CMGS="<number>"\r, sleeps 300 ms for the ">" prompt,
 * then sends <body>\x1A and waits up to 15 s for OK/ERROR.
 *
 * Returns 0 on success, -1 on failure.
 */
int modem_uart_sms_send(const char *number, const char *body);

/* Write raw bytes directly to the serial fd (no framing).
 * Returns 0 on success, -1 on failure. */
int modem_uart_write_raw(const unsigned char *data, size_t len);

/*
 * Non-blocking URC poll.  Copies the oldest queued URC into urc_buf and
 * returns 1.  Returns 0 if the queue is empty.
 */
int modem_uart_poll_urc(char *urc_buf, size_t urc_size);

/* Close the port and join the reader thread. */
void modem_uart_close(void);
