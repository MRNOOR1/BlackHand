#ifndef BLUETOOTH_SERVICE_H
#define BLUETOOTH_SERVICE_H

#include <stddef.h>

#define BT_MAX_DEVICES 64

typedef struct {
    char mac[24];
    char name[96];
    int connected;
    int paired;
    int trusted;
    int audio_capable;
} BtDevice;

void bluetooth_service_init(void);
void bluetooth_service_shutdown(void);

int bluetooth_service_is_available(void);
int bluetooth_service_set_power(int on);
int bluetooth_service_get_power(void);

int bluetooth_service_refresh_devices(void);
int bluetooth_service_refresh_device_async(const char *mac);
void bluetooth_service_load_paired(void);
int bluetooth_service_scan_start(void);
void bluetooth_service_scan_stop(void);
int bluetooth_service_scan_is_running(void);
size_t bluetooth_service_device_count(void);
/* Copies the device at `index` into *out. Returns 1 on success, 0 if index
 * is out of range (out is left untouched). Copies out rather than handing
 * back an internal pointer so callers on other threads never dereference
 * shared storage without the lock that protects it. */
int bluetooth_service_device_at(size_t index, BtDevice *out);

int bluetooth_service_connect(const char *mac);
int bluetooth_service_connect_async(const char *mac);
int bluetooth_service_connect_is_running(void);
int bluetooth_service_refresh_device(const char *mac);
int bluetooth_service_disconnect(const char *mac);
int bluetooth_service_remove(const char *mac);

void bluetooth_service_get_connected_mac(char *out, size_t sz);

int mac_is_safe(const char *mac);

#endif
