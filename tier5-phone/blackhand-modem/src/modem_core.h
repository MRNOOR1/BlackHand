#pragma once

#include <stddef.h>
#include "cJSON.h"

/*
 * modem_core — high-level AT command operations.
 * Requires modem_uart_open() to have succeeded.
 * All functions return 0 on success, -1 on failure.
 */

/* Initialise modem: echo off, SMS text mode, enable URCs, register network. */
int modem_init(void);

/* ── SMS ────────────────────────────────────────────────────── */

/* Send an SMS.  Uses the two-step AT+CMGS / Ctrl-Z flow. */
int modem_sms_send(const char *number, const char *body);

/* List all messages stored on SIM.
 * Returns a new cJSON array; caller must cJSON_Delete it. */
cJSON *modem_sms_list(void);

/* Delete message with the given index from SIM storage. */
int modem_sms_delete(int msg_id);

/* ── Calls ──────────────────────────────────────────────────── */

/* Initiate an outgoing voice call.  ATD<number>; */
int modem_call_dial(const char *number);

/* Hang up any active or ringing call.  ATH */
int modem_call_hangup(void);

/* Answer an incoming call.  ATA */
int modem_call_answer(void);

/* Reject an incoming call without answering. */
int modem_call_reject(void);

/* Return active calls from AT+CLCC.
 * Returns a new cJSON array; caller must cJSON_Delete it. */
cJSON *modem_call_get_active(void);

/* ── GPS ────────────────────────────────────────────────────── */

/* Power on the integrated GPS engine. */
int modem_gps_enable(void);

/* Power off the GPS engine. */
int modem_gps_disable(void);

/* Get current GPS fix.  lat/lon in decimal degrees, alt in metres, speed km/h.
 * Returns 0 on a valid fix, -1 if no fix or GPS is off. */
int modem_gps_get_location(double *lat, double *lon,
                            double *alt, double *speed);

/* ── System ─────────────────────────────────────────────────── */

/* Read AT+CSQ signal strength.  *rssi is set to 0–31 (99 = no signal). */
int modem_get_signal_strength(int *rssi);

/* Read IMEI string into buf. */
int modem_get_imei(char *buf, size_t len);

/* Read current operator name/code into buf. */
int modem_get_operator(char *buf, size_t len);
