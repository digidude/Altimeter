#pragma once

/**
 * WS2812 RGB LED status indicator for ESP32-C6 (onboard LED, GPIO 8).
 *
 * This library knows nothing about WiFi, provisioning, or any other
 * subsystem — it only knows how to display four generic states. Callers
 * map their own domain events onto these states (see main.c), so this
 * library stays reusable for whatever gets added next.
 *
 * An internal FreeRTOS task owns the blink loop; led_indicator_set_state()
 * just posts a target state and returns, so it's safe to call from any
 * task or ISR context.
 *
 * LED_STATE_OFF       — dark
 * LED_STATE_IDLE      — solid yellow      — waiting for input
 * LED_STATE_BUSY      — fast yellow blink — actively working
 * LED_STATE_ATTENTION — slow yellow blink — needs attention / degraded
 * LED_STATE_READY     — solid green       — nominal
 */

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LED_STATE_OFF = 0,
    LED_STATE_IDLE,
    LED_STATE_BUSY,
    LED_STATE_ATTENTION,
    LED_STATE_READY,
} led_state_t;

// Starts the blink task. Safe to call before WiFi init — only needs the
// RMT peripheral. LED starts dark (LED_STATE_OFF) until the first
// led_indicator_set_state() call.
esp_err_t led_indicator_init(void);

// Thread-safe; may be called from any task. Takes effect within one
// blink half-period for the non-solid states, immediately for solid
// ones (the notification wakes the task from its indefinite wait).
esp_err_t led_indicator_set_state(led_state_t state);

// Stops the blink task and releases the RMT channel, turning the LED
// off first.
esp_err_t led_indicator_deinit(void);

#ifdef __cplusplus
}
#endif
