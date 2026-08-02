#include "esp_log.h"

#include "system_init.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "boot");

    esp_err_t err = system_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "system_init failed: %s", esp_err_to_name(err));
    }

    // App-level features start here once M1 is verified — see PROJECT.md.
}
