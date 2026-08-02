#include "esp_log.h"
#include "mdns.h"
#include "nvs_flash.h"

#include "led_indicator.h"
#include "system_init.h"
#include "time_sync.h"
#include "wifi_provision.h"

static const char *TAG = "system_init";

// Translates wifi_provision's generic connectivity events into LED
// states and kicks off NTP time sync once actually connected. Lives
// here, not inside wifi_provision, so that library stays unaware the
// LED or time_sync exist at all — the same separation DK1-ESP32-C6 uses
// between wifi_manager and its own on_wifi_status() in system_init.c.
static void on_wifi_status(wifi_provision_event_t event)
{
    switch (event) {
        case WIFI_PROVISION_EVENT_BOOT_CLEAN:
            led_indicator_set_state(LED_STATE_IDLE); // solid yellow
            break;
        case WIFI_PROVISION_EVENT_CONNECTING:
            led_indicator_set_state(LED_STATE_BUSY); // fast yellow blink
            break;
        case WIFI_PROVISION_EVENT_CONNECT_FAILED:
            led_indicator_set_state(LED_STATE_ATTENTION); // slow yellow blink
            break;
        case WIFI_PROVISION_EVENT_CONNECTED:
            led_indicator_set_state(LED_STATE_READY); // solid green
            // time_sync_init()/start() are both no-ops if already
            // done, so this is safe to hit again on every reconnect.
            time_sync_config_t time_cfg = TIME_SYNC_CONFIG_DEFAULT();
            time_cfg.timezone = wifi_provision_get_timezone();
            time_sync_init(&time_cfg);
            time_sync_start();
            break;
    }
}

esp_err_t system_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    led_indicator_init();
    wifi_provision_start(on_wifi_status);

    // mDNS needs esp_netif_init()/esp_event_loop_create_default(), both
    // called inside wifi_provision_start() — must come after it returns.
    // Soft-fail: a broken hostname announcement shouldn't take down a
    // device that's otherwise reachable fine by IP.
    // Hostname defaults to "altimeter" but is user-configurable via the
    // captive portal (stored in NVS alongside WiFi creds/TZ) — see
    // wifi_provision_get_hostname().
    esp_err_t mdns_err = mdns_init();
    if (mdns_err == ESP_OK) {
        mdns_err = mdns_hostname_set(wifi_provision_get_hostname());
    }
    if (mdns_err != ESP_OK) {
        ESP_LOGW(TAG, "mDNS init failed: %s (still reachable by IP)", esp_err_to_name(mdns_err));
    }

    // From here on, connectivity is handled by wifi_provision.c in the
    // background (reconnect-with-backoff on drop, or SoftAP fallback if
    // the saved credentials didn't work).
    return ESP_OK;
}
