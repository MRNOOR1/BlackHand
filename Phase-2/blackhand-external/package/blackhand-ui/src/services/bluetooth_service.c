#include "bluetooth_service.h"

/*
 * Bluetooth implementation is intentionally stubbed for now.
 * The UI remains in place so a proper implementation can be added later.
 */

void bluetooth_service_init(void) {
}

void bluetooth_service_shutdown(void) {
}

int bluetooth_service_is_available(void) {
    return 0;
}

int bluetooth_service_set_power(int on) {
    (void)on;
    return -1;
}

int bluetooth_service_get_power(void) {
    return 0;
}

int bluetooth_service_refresh_devices(void) {
    return -1;
}

size_t bluetooth_service_device_count(void) {
    return 0;
}

const BtDevice *bluetooth_service_device_at(size_t index) {
    (void)index;
    return NULL;
}

int bluetooth_service_connect(const char *mac) {
    (void)mac;
    return -1;
}

int bluetooth_service_disconnect(const char *mac) {
    (void)mac;
    return -1;
}

int bluetooth_service_remove(const char *mac) {
    (void)mac;
    return -1;
}
