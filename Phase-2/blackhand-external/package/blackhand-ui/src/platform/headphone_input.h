#ifndef HEADPHONE_INPUT_H
#define HEADPHONE_INPUT_H

/*
 * headphone_input.h
 *
 * Handles inline control buttons on wired headphones / USB-C earphones.
 *
 * Scans /dev/input/eventX for a device that reports KEY_VOLUMEUP,
 * KEY_VOLUMEDOWN, or KEY_PLAYPAUSE (USB HID usage "Consumer Control").
 *
 * Runs a background thread — call headphone_input_init() once at startup
 * and headphone_input_shutdown() on exit.
 *
 * Hot-plug: if no device is found at init, the thread keeps re-scanning
 * every 2 seconds until one appears (or until shutdown is called).
 */

void headphone_input_init(void);
void headphone_input_shutdown(void);

#endif /* HEADPHONE_INPUT_H */
