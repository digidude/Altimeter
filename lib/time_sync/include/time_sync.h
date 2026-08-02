#pragma once

// NTP time synchronization (SNTP client). Ported from the DK1-ESP32-C6
// project's lib/time_sync — same API, adapted to this project's style
// (no external config.h, no header-guard macros).
//
// Requires an active internet-reachable STA connection — this won't do
// anything useful over the SoftAP-only captive portal network. Callers
// are expected to start it once WiFi is actually connected (see main.c).

#include <esp_err.h>
#include <time.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TIME_SYNC_STATUS_NOT_STARTED = 0,
    TIME_SYNC_STATUS_STARTED,
    TIME_SYNC_STATUS_SYNCED,
    TIME_SYNC_STATUS_FAILED
} time_sync_status_t;

typedef struct {
    const char *ntp_server1;  // primary NTP server (e.g. "pool.ntp.org")
    const char *ntp_server2;  // secondary NTP server (optional, may be NULL)
    const char *ntp_server3;  // tertiary NTP server (optional, may be NULL)
    const char *timezone;     // POSIX TZ string, e.g. "UTC" or "PST8PDT,M3.2.0,M11.1.0"
    uint32_t sync_timeout_ms; // used by time_sync_wait_for_sync()'s caller, not enforced internally
} time_sync_config_t;

#define TIME_SYNC_CONFIG_DEFAULT() { \
    .ntp_server1 = "pool.ntp.org", \
    .ntp_server2 = "time.nist.gov", \
    .ntp_server3 = "time.google.com", \
    .timezone = "UTC", \
    .sync_timeout_ms = 10000 \
}

// config may be NULL to use TIME_SYNC_CONFIG_DEFAULT(). Safe to call
// more than once — a no-op if already initialized.
esp_err_t time_sync_init(const time_sync_config_t *config);

// Starts the SNTP client. Call after WiFi is actually connected — safe
// to call again on a later reconnect, a no-op if already started/synced.
esp_err_t time_sync_start(void);

esp_err_t time_sync_stop(void);

bool time_sync_is_synced(void);

time_sync_status_t time_sync_get_status(void);

// All of these return ESP_ERR_INVALID_STATE if time hasn't synced yet.
esp_err_t time_sync_get_utc_time(struct tm *tm);
esp_err_t time_sync_get_local_time(struct tm *tm);
esp_err_t time_sync_get_utc_timestamp(time_t *timestamp);

// buffer_size must be at least 25 bytes. Example: "2024-11-11T15:30:45Z"
esp_err_t time_sync_format_utc_iso8601(char *buffer, size_t buffer_size);

// buffer_size must be at least 30 bytes. Example: "Mon Nov 11 15:30:45 2024"
esp_err_t time_sync_format_local_string(char *buffer, size_t buffer_size);

// Blocks until synced or timeout_ms elapses (0 = wait forever).
esp_err_t time_sync_wait_for_sync(uint32_t timeout_ms);

esp_err_t time_sync_deinit(void);

#ifdef __cplusplus
}
#endif
