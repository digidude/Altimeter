#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "factory_reset.h"
#include "wifi_provision.h"

static const char *TAG = "factory_reset";

#define RESET_BUTTON_GPIO GPIO_NUM_9 // "BOOT" button on the devkit, active-low
#define RESET_HOLD_MS 8000           // long enough to rule out an accidental bump
#define POLL_INTERVAL_MS 100

static void factory_reset_task(void *arg)
{
    int64_t held_since_ms = -1;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));

        bool pressed = gpio_get_level(RESET_BUTTON_GPIO) == 0;
        int64_t now_ms = esp_timer_get_time() / 1000;

        if (!pressed) {
            held_since_ms = -1;
            continue;
        }
        if (held_since_ms < 0) {
            held_since_ms = now_ms;
            continue;
        }
        if (now_ms - held_since_ms >= RESET_HOLD_MS) {
            ESP_LOGW(TAG, "BOOT button held %dms, erasing WiFi credentials and restarting",
                     RESET_HOLD_MS);
            esp_err_t err = wifi_provision_erase_credentials();
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "failed to erase credentials: %s", esp_err_to_name(err));
            }
            vTaskDelay(pdMS_TO_TICKS(200)); // let the log line flush over USB-CDC
            esp_restart();
        }
    }
}

void factory_reset_start(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << RESET_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE, // belt-and-suspenders; the devkit already has one
    };
    gpio_config(&cfg);

    xTaskCreate(factory_reset_task, "factory_reset", 2560, NULL, 5, NULL);
}
