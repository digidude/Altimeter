// NTP time synchronization implementation. Ported from DK1-ESP32-C6's
// lib/time_sync/time_sync.c.

#include "time_sync.h"
#include <esp_log.h>
#include <esp_sntp.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

static const char *TAG = "time_sync";

#define TIME_SYNC_STARTED_BIT BIT0
#define TIME_SYNC_SYNCED_BIT BIT1
#define TIME_SYNC_FAILED_BIT BIT2

static time_sync_config_t s_config;
static time_sync_status_t s_status = TIME_SYNC_STATUS_NOT_STARTED;
static EventGroupHandle_t s_time_event_group = NULL;
static bool s_initialized = false;
static int s_server_count = 0;

static void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "time synchronized, UTC: %lld seconds since epoch", (long long)tv->tv_sec);

    s_status = TIME_SYNC_STATUS_SYNCED;

    if (s_time_event_group) {
        xEventGroupSetBits(s_time_event_group, TIME_SYNC_SYNCED_BIT);
    }

    // Timezone is set by time_sync_init() via setenv("TZ", ...).
    struct tm local_timeinfo;
    time_t now = tv->tv_sec;
    localtime_r(&now, &local_timeinfo);

    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S %Z", &local_timeinfo);
    ESP_LOGI(TAG, "synchronized time: %s", time_str);
}

esp_err_t time_sync_init(const time_sync_config_t *config)
{
    if (s_initialized) {
        return ESP_OK;
    }

    if (config) {
        s_config = *config;
    } else {
        time_sync_config_t default_config = TIME_SYNC_CONFIG_DEFAULT();
        s_config = default_config;
    }

    s_time_event_group = xEventGroupCreate();
    if (!s_time_event_group) {
        ESP_LOGE(TAG, "failed to create time sync event group");
        return ESP_ERR_NO_MEM;
    }

    const char *tz = (s_config.timezone && strlen(s_config.timezone) > 0) ? s_config.timezone : "UTC";
    setenv("TZ", tz, 1);
    tzset();
    ESP_LOGI(TAG, "timezone set to: %s", tz);

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);

    s_server_count = 0;
    if (s_config.ntp_server1) {
        esp_sntp_setservername(s_server_count++, s_config.ntp_server1);
    }
    if (s_config.ntp_server2) {
        esp_sntp_setservername(s_server_count++, s_config.ntp_server2);
    }
    if (s_config.ntp_server3) {
        esp_sntp_setservername(s_server_count++, s_config.ntp_server3);
    }

    esp_sntp_set_sync_mode(SNTP_SYNC_MODE_SMOOTH);
    esp_sntp_set_time_sync_notification_cb(time_sync_notification_cb);

    s_initialized = true;
    ESP_LOGI(TAG, "time sync initialized with %d NTP server(s)", s_server_count);

    return ESP_OK;
}

esp_err_t time_sync_start(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "time sync not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_status == TIME_SYNC_STATUS_STARTED || s_status == TIME_SYNC_STATUS_SYNCED) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "starting NTP time synchronization (%s, %s, %s)",
             s_config.ntp_server1 ? s_config.ntp_server1 : "none",
             s_config.ntp_server2 ? s_config.ntp_server2 : "none",
             s_config.ntp_server3 ? s_config.ntp_server3 : "none");

    esp_sntp_init();

    s_status = TIME_SYNC_STATUS_STARTED;
    if (s_time_event_group) {
        xEventGroupSetBits(s_time_event_group, TIME_SYNC_STARTED_BIT);
        xEventGroupClearBits(s_time_event_group, TIME_SYNC_SYNCED_BIT | TIME_SYNC_FAILED_BIT);
    }

    return ESP_OK;
}

esp_err_t time_sync_stop(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (esp_sntp_enabled()) {
        esp_sntp_stop();
    }
    s_status = TIME_SYNC_STATUS_NOT_STARTED;

    if (s_time_event_group) {
        xEventGroupClearBits(s_time_event_group, TIME_SYNC_STARTED_BIT | TIME_SYNC_SYNCED_BIT | TIME_SYNC_FAILED_BIT);
    }

    return ESP_OK;
}

esp_err_t time_sync_deinit(void)
{
    if (!s_initialized) {
        return ESP_OK;
    }

    time_sync_stop();

    if (s_time_event_group) {
        vEventGroupDelete(s_time_event_group);
        s_time_event_group = NULL;
    }

    s_initialized = false;
    s_status = TIME_SYNC_STATUS_NOT_STARTED;
    return ESP_OK;
}

bool time_sync_is_synced(void)
{
    return s_status == TIME_SYNC_STATUS_SYNCED;
}

time_sync_status_t time_sync_get_status(void)
{
    return s_status;
}

esp_err_t time_sync_get_utc_time(struct tm *tm)
{
    if (!tm) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!time_sync_is_synced()) {
        return ESP_ERR_INVALID_STATE;
    }

    time_t now;
    time(&now);
    gmtime_r(&now, tm);
    return ESP_OK;
}

esp_err_t time_sync_get_local_time(struct tm *tm)
{
    if (!tm) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!time_sync_is_synced()) {
        return ESP_ERR_INVALID_STATE;
    }

    time_t now;
    time(&now);
    localtime_r(&now, tm);
    return ESP_OK;
}

esp_err_t time_sync_get_utc_timestamp(time_t *timestamp)
{
    if (!timestamp) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!time_sync_is_synced()) {
        return ESP_ERR_INVALID_STATE;
    }

    time(timestamp);
    return ESP_OK;
}

esp_err_t time_sync_format_utc_iso8601(char *buffer, size_t buffer_size)
{
    if (!buffer || buffer_size < 25) {
        return ESP_ERR_INVALID_ARG;
    }

    struct tm timeinfo;
    esp_err_t err = time_sync_get_utc_time(&timeinfo);
    if (err != ESP_OK) {
        return err;
    }

    int ret = strftime(buffer, buffer_size, "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
    if (ret == 0) {
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

esp_err_t time_sync_format_local_string(char *buffer, size_t buffer_size)
{
    if (!buffer || buffer_size < 30) {
        return ESP_ERR_INVALID_ARG;
    }

    struct tm timeinfo;
    esp_err_t err = time_sync_get_local_time(&timeinfo);
    if (err != ESP_OK) {
        return err;
    }

    int ret = strftime(buffer, buffer_size, "%a %b %d %H:%M:%S %Y", &timeinfo);
    if (ret == 0) {
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

esp_err_t time_sync_wait_for_sync(uint32_t timeout_ms)
{
    if (!s_initialized || !s_time_event_group) {
        return ESP_ERR_INVALID_STATE;
    }

    if (time_sync_is_synced()) {
        return ESP_OK;
    }

    TickType_t timeout_ticks = timeout_ms == 0 ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);

    EventBits_t bits = xEventGroupWaitBits(
        s_time_event_group,
        TIME_SYNC_SYNCED_BIT | TIME_SYNC_FAILED_BIT,
        pdFALSE,
        pdFALSE,
        timeout_ticks);

    if (bits & TIME_SYNC_SYNCED_BIT) {
        return ESP_OK;
    }
    ESP_LOGW(TAG, "time synchronization timeout after %lu ms", (unsigned long)timeout_ms);
    return ESP_ERR_TIMEOUT;
}
