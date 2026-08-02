#pragma once

#include "esp_err.h"

// Connectivity events this library reports through the optional status
// callback below. Deliberately generic to this module's own concerns
// (not LED colors, not any other subsystem) — mapping these onto
// anything visible (an LED, a log line, whatever) is the caller's job.
// See main.c for the actual mapping; this library has no LED dependency.
//
// WIFI_PROVISION_EVENT_BOOT_CLEAN     — fresh boot, no saved credentials
//                                        at all, entering AP/portal mode
// WIFI_PROVISION_EVENT_CONNECTING     — attempting a STA connection
//                                        (initial bounded retries, or a
//                                        later reconnect after a drop)
// WIFI_PROVISION_EVENT_CONNECT_FAILED — had credentials, exhausted the
//                                        bounded retries, falling back
//                                        to AP/portal mode
// WIFI_PROVISION_EVENT_CONNECTED      — STA connected and got an IP
typedef enum {
    WIFI_PROVISION_EVENT_BOOT_CLEAN,
    WIFI_PROVISION_EVENT_CONNECTING,
    WIFI_PROVISION_EVENT_CONNECT_FAILED,
    WIFI_PROVISION_EVENT_CONNECTED,
} wifi_provision_event_t;

typedef void (*wifi_provision_status_cb_t)(wifi_provision_event_t event);

// Call once from app_main(), after nvs_flash_init() has run.
// status_cb may be NULL if the caller doesn't want status notifications.
//
// Behavior:
//   - If credentials are saved in NVS, tries to connect STA with a
//     bounded number of retries. On success, returns and the device is
//     on the network with automatic reconnect-with-backoff running in
//     the background indefinitely (survives router reboots / temporary
//     drops without ever falling back to AP mode).
//   - If there are no saved credentials, or the bounded retries above
//     all fail (bad password, AP out of range, etc.), starts a SoftAP
//     named "ESP32-Setup-XXXX" with a captive portal. Submitting the
//     form saves credentials to NVS and reboots the device, which then
//     re-enters this same function and tries STA again.
//   - Also starts the factory-reset watcher (hold the devkit's BOOT
//     button for several seconds to erase credentials and re-enter
//     provisioning without a USB connection).
void wifi_provision_start(wifi_provision_status_cb_t status_cb);

// Erases saved WiFi credentials from NVS. Does not restart the device —
// call esp_restart() afterward if you want it to re-enter provisioning
// immediately.
esp_err_t wifi_provision_erase_credentials(void);

// Returns the timezone (POSIX TZ string, e.g. "UTC" or
// "PST8PDT,M3.2.0,M11.1.0") saved during provisioning, or "UTC" if none
// was ever saved. The returned pointer is backed by static storage —
// valid for the life of the program, safe to hold onto (e.g. pass
// straight into a time_sync_config_t).
const char *wifi_provision_get_timezone(void);

// Returns the mDNS hostname (device is reachable as http://<name>.local)
// saved during provisioning, or "altimeter" if none was ever saved.
// Sanitized at provisioning time to a valid DNS label (lowercase
// letters/digits/hyphens, no leading/trailing hyphen). Same
// static-storage lifetime guarantee as wifi_provision_get_timezone()
// above — safe to hold onto.
const char *wifi_provision_get_hostname(void);
