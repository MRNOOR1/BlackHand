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
size_t bluetooth_service_device_count(void);
const BtDevice *bluetooth_service_device_at(size_t index);

int bluetooth_service_connect(const char *mac);
int bluetooth_service_disconnect(const char *mac);
int bluetooth_service_remove(const char *mac);

#endif
