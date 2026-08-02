[← Milestones overview](README.md)

# Known Issues / Parking Lot

- `save_post_handler` reboots via `esp_restart()` after saving — simple
  and reliable, but means provisioning always costs a reboot. Fine for
  now; could be replaced with a live STA/AP mode switch later if reboot
  time becomes annoying.
- ~~No mDNS / friendly hostname yet~~ — added: reachable as
  `http://<name>.local`, hostname defaults to `altimeter`,
  user-configurable via the captive portal form (stored in NVS
  alongside SSID/password/timezone).
- ~~No factory-reset trigger~~ — added: hold the devkit's BOOT button
  (GPIO9) for 8s at any time while the device is running to erase saved
  WiFi credentials and reboot into provisioning. See
  `lib/wifi_provision/factory_reset.c`. Verified on hardware
  2026-07-30 — button-hold correctly erases and re-enters provisioning,
  including while already in provisioning mode (idempotent, harmless).
