/**
 * WS2812 RGB LED status indicator — native RMT implementation.
 *
 * Hardware notes:
 *   The ESP32-C6-DevKitC-1 onboard LED is a single WS2812 on GPIO 8.
 *   WS2812 uses a single-wire NRZ protocol that requires cycle-accurate
 *   timing (T0H ~400ns, T1H ~800ns) — it can't be driven with plain
 *   gpio_set_level(). This drives the protocol directly via the ESP-IDF
 *   RMT peripheral (driver/rmt_tx.h), a core driver always available in
 *   PlatformIO's bundled ESP-IDF, rather than pulling in the led_strip
 *   managed component for one pixel.
 *
 * WS2812 wire format:
 *   24 bits per LED in GRB order, MSB first.
 *   At 10 MHz RMT clock (100 ns/tick):
 *     bit-0 -> 4 ticks high (400 ns), 9 ticks low (900 ns)
 *     bit-1 -> 8 ticks high (800 ns), 5 ticks low (500 ns)
 *   Reset -> line low for >= 50us (any vTaskDelay covers this).
 */

#include "led_indicator.h"
#include "driver/rmt_tx.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdint.h>

static const char *TAG = "led_indicator";

#define LED_GPIO_PIN 8

// ---------------------------------------------------------------
// WS2812 RMT timing (10 MHz clock -> 100 ns per tick)
// ---------------------------------------------------------------
#define WS2812_RMT_RES_HZ 10000000u
#define WS2812_T0L 9 //  900 ns (spec min 850 ns)
#define WS2812_T0H 4 //  400 ns
#define WS2812_T1L 5 //  500 ns (spec min 450 ns)
#define WS2812_T1H 8 //  800 ns

#define LED_BRIGHTNESS 20 // ~8% — even 25% was uncomfortably bright at close range

// Blink cadences: BUSY is snappy ("working on it"), ATTENTION is
// deliberately calmer ("stalled, not urgent") so the two read as
// distinct at a glance rather than both just "blinking yellow."
#define LED_BUSY_BLINK_MS 400
#define LED_ATTENTION_BLINK_MS 1600

// WS2812 green channels run visually "hot" relative to red, so an
// even R/G mix reads as green, not yellow — G is deliberately cut
// well below R to compensate and land on a true warm yellow/amber.
// G had been cut all the way to 0, which is just red, not amber —
// this is a starting point, tune to taste on real hardware.
#define YELLOW_R 255
#define YELLOW_G 100
#define YELLOW_B 0

#define GREEN_R 0
#define GREEN_G 255
#define GREEN_B 0

// ---------------------------------------------------------------
// Module state
// ---------------------------------------------------------------
static rmt_channel_handle_t s_rmt_chan = NULL;
static rmt_encoder_handle_t s_encoder = NULL;
static TaskHandle_t s_task = NULL;
static volatile led_state_t s_state = LED_STATE_OFF;
static bool s_initialized = false;

// ---------------------------------------------------------------
// WS2812 low-level helpers
// ---------------------------------------------------------------

static inline uint8_t scale(uint8_t value, uint8_t brightness)
{
    return (uint8_t)((uint16_t)value * brightness / 255u);
}

// Transmits one WS2812 pixel. Data must be in GRB wire order — the
// WS2812 interprets the first byte it receives as Green regardless of
// intent. rmt_tx_wait_all_done() blocks until the peripheral finishes,
// so the caller can safely reuse grb[] right after this returns.
static void ws2812_write(uint8_t r, uint8_t g, uint8_t b)
{
    uint8_t grb[3] = { g, r, b };
    rmt_transmit_config_t tx_cfg = { .loop_count = 0 };
    rmt_transmit(s_rmt_chan, s_encoder, grb, sizeof(grb), &tx_cfg);
    rmt_tx_wait_all_done(s_rmt_chan, pdMS_TO_TICKS(100));
}

static void ws2812_off(void)
{
    ws2812_write(0, 0, 0);
}

static void set_pixel(uint8_t r, uint8_t g, uint8_t b)
{
    ws2812_write(scale(r, LED_BRIGHTNESS), scale(g, LED_BRIGHTNESS), scale(b, LED_BRIGHTNESS));
}

// ---------------------------------------------------------------
// Blink task
// ---------------------------------------------------------------

static void led_task(void *arg)
{
    bool blink_phase = true;
    led_state_t last_state = LED_STATE_OFF;

    while (1) {
        led_state_t state = s_state;
        TickType_t wait;

        if (state != last_state) {
            blink_phase = true;
            last_state = state;
        }

        switch (state) {
            case LED_STATE_IDLE:
                set_pixel(YELLOW_R, YELLOW_G, YELLOW_B);
                wait = portMAX_DELAY;
                break;

            case LED_STATE_BUSY:
                if (blink_phase) {
                    set_pixel(YELLOW_R, YELLOW_G, YELLOW_B);
                } else {
                    ws2812_off();
                }
                blink_phase = !blink_phase;
                wait = pdMS_TO_TICKS(LED_BUSY_BLINK_MS / 2);
                break;

            case LED_STATE_ATTENTION:
                if (blink_phase) {
                    set_pixel(YELLOW_R, YELLOW_G, YELLOW_B);
                } else {
                    ws2812_off();
                }
                blink_phase = !blink_phase;
                wait = pdMS_TO_TICKS(LED_ATTENTION_BLINK_MS / 2);
                break;

            case LED_STATE_READY:
                set_pixel(GREEN_R, GREEN_G, GREEN_B);
                wait = portMAX_DELAY;
                break;

            case LED_STATE_OFF:
            default:
                ws2812_off();
                wait = portMAX_DELAY;
                break;
        }

        ulTaskNotifyTake(pdTRUE, wait);
    }
}

// ---------------------------------------------------------------
// Public API
// ---------------------------------------------------------------

esp_err_t led_indicator_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "already initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // mem_block_symbols = 64 comfortably holds the 24 RMT symbols needed
    // for one WS2812 frame (24 bits x 2 levels).
    rmt_tx_channel_config_t chan_cfg = {
        .gpio_num = LED_GPIO_PIN,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = WS2812_RMT_RES_HZ,
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
    };
    esp_err_t ret = rmt_new_tx_channel(&chan_cfg, &s_rmt_chan);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "rmt_new_tx_channel failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // msb_first = 1 matches the WS2812 wire protocol (D7 transmitted first).
    rmt_bytes_encoder_config_t enc_cfg = {
        .bit0 = { .duration0 = WS2812_T0H, .level0 = 1, .duration1 = WS2812_T0L, .level1 = 0 },
        .bit1 = { .duration0 = WS2812_T1H, .level0 = 1, .duration1 = WS2812_T1L, .level1 = 0 },
        .flags.msb_first = 1,
    };
    ret = rmt_new_bytes_encoder(&enc_cfg, &s_encoder);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "rmt_new_bytes_encoder failed: %s", esp_err_to_name(ret));
        rmt_del_channel(s_rmt_chan);
        s_rmt_chan = NULL;
        return ret;
    }

    ret = rmt_enable(s_rmt_chan);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "rmt_enable failed: %s", esp_err_to_name(ret));
        rmt_del_encoder(s_encoder);
        s_encoder = NULL;
        rmt_del_channel(s_rmt_chan);
        s_rmt_chan = NULL;
        return ret;
    }

    ws2812_off();

    // 2KB stack is plenty — the task only calls ws2812_write() and waits.
    // Priority 3 keeps it responsive without competing with the WiFi
    // driver task (priority 23) or the httpd/DNS tasks.
    BaseType_t ok = xTaskCreate(led_task, "led_indicator", 2048, NULL, 3, &s_task);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "failed to create led_task");
        rmt_disable(s_rmt_chan);
        rmt_del_encoder(s_encoder);
        s_encoder = NULL;
        rmt_del_channel(s_rmt_chan);
        s_rmt_chan = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "LED indicator ready on GPIO %d", LED_GPIO_PIN);
    return ESP_OK;
}

esp_err_t led_indicator_set_state(led_state_t state)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    s_state = state;
    xTaskNotifyGive(s_task);
    return ESP_OK;
}

esp_err_t led_indicator_deinit(void)
{
    if (!s_initialized) {
        return ESP_OK;
    }

    s_state = LED_STATE_OFF;
    xTaskNotifyGive(s_task);
    vTaskDelay(pdMS_TO_TICKS(50)); // let the task run its final OFF iteration
    vTaskDelete(s_task);
    s_task = NULL;

    rmt_disable(s_rmt_chan);
    rmt_del_encoder(s_encoder);
    s_encoder = NULL;
    rmt_del_channel(s_rmt_chan);
    s_rmt_chan = NULL;

    s_initialized = false;
    ESP_LOGI(TAG, "LED indicator deinitialized");
    return ESP_OK;
}
