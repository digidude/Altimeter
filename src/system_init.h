#pragma once

#include "esp_err.h"

// Runs the full boot sequence: NVS init, LED indicator, and WiFi
// provisioning (which also starts the factory-reset BOOT-button
// watcher). Also wires up the LED-state and NTP-time-sync reactions to
// WiFi connectivity events. Call once from app_main().
esp_err_t system_init(void);
