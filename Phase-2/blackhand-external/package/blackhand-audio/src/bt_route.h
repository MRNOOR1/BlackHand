#ifndef BT_ROUTE_H
#define BT_ROUTE_H

#include <stddef.h>

/*
 * bt_route — daemon-owned routing decisions.
 *
 * The audio daemon queries bluez at the start of every play_start /
 * record_start to decide which ALSA device to open. The UI never
 * passes MAC addresses across the IPC boundary.
 */

/* If a BT audio sink (A2DP) is connected, fills out_mac with its
   address (e.g. "AA:BB:CC:DD:EE:FF") and returns 1. Otherwise 0.
   out_mac must be at least 18 bytes. */
int bt_route_find_a2dp_sink(char *out_mac, size_t mac_size);

/* If a BT headset with HFP/HSP (i.e. mic + speaker) is connected,
   fills out_mac and returns 1. Otherwise 0. */
int bt_route_find_hfp_mic(char *out_mac, size_t mac_size);

#endif
