# Altimeter — Milestones

## Toolchain:

PlatformIO, `framework = espidf` (native ESP-IDF, no Arduino
layer). Board: `esp32-c6-devkitc-1`.

## Rules for this project:

- Nothing in Milestone 2+ gets built until Milestone 1
is verified on real hardware against real failure modes, not just "it
connected once on the bench."
- 

## Milestone 0 — Bring-up

- [ ] `pio run` builds clean
- [ ] `pio run -t upload -t monitor` flashes and prints boot logs

## Milestone 1 — Connectivity (blocks all app features)

Scope: device with no saved credentials boots into a SoftAP + captive
portal, accepts SSID/password from a phone or laptop, saves them to NVS,
connects, and reconnects reliably afterward. This is implemented in
`lib/wifi_provision/` (its own ESP-IDF component — see README.md
Layout).

Definition of done — all of these need an actual test, not a read-through:

- [x] Fresh device (erased flash) boots directly into AP mode, SSID
      `ESP32-Setup-XXXX` is visible
- [x] Connecting a phone/laptop to that AP triggers the OS's
      "sign in to network" captive portal prompt automatically
- [x] Submitting the form saves credentials and the device reboots into
      the target network
- [x] Power-cycle the device (unplug/replug) — it reconnects using saved
      credentials without re-provisioning
- [ ] Reboot the WiFi router while the device is running — device
      reconnects on its own once the router is back (steady-state
      reconnect, no fallback to AP mode)
- [x] Provision with a deliberately wrong password — device falls back
      to AP/captive portal instead of retrying forever silently
- [ ] Leave device running for an extended idle period — confirm no
      memory leak / crash from the httpd + DNS server having been torn
      down after provisioning

## Milestone 1a — Network picker UX

Scope: the setup page scans for nearby networks (device runs SoftAP +
STA together — `WIFI_MODE_APSTA`) and offers them as a dropdown, since
the exact SSID (case-sensitive) isn't always obvious to whoever's
provisioning the device. Still a free-text field underneath, so a
hidden or out-of-range network can be typed by hand. Also adds a "Show
password" toggle on the password field.

- [x] Setup page shows "Scanning for networks…" on load, then populates
      a dropdown/datalist of nearby SSIDs
- [x] "Show password" checkbox reveals/hides the typed password
- [X] Selecting a network from the dropdown (not typing it) and
      submitting connects successfully
- [ ] A hidden/out-of-range network not in the list can still be typed
      manually and submitted successfully
- [ ] Two APs broadcasting the same SSID (e.g. a mesh network) show up
      as one entry, not duplicated
- [ ] The AP + captive portal (HTTP form, DNS hijack) keep responding
      normally to a connected client while a scan is in progress —
      scanning blocks the httpd worker task for ~1-3s per request, so
      this is the one place coexistence could visibly stall

## Milestone 1b — OTA-ready partition table

Scope: switched from a single 1MB `factory` app partition to a two-slot
OTA layout (`ota_0`/`ota_1`, 1700K each, plus `otadata`) via
`board_build.partitions = partitions_two_ota_large.csv` in
`platformio.ini` (framework-bundled CSV, no project-local file needed),
and `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` in `sdkconfig.defaults`.
This is groundwork only — no actual OTA update client exists yet.

- [x] Full chip erase + fresh build/upload boots correctly from `ota_0`
      (verified via boot log: partition table shows nvs/otadata/
      phy_init/ota_0/ota_1; "Loaded app from partition at offset
      0x20000"). All existing features (WiFi provisioning, LED, factory
      reset) work unchanged on the new layout. 2026-08-02.
- [ ] Not done: an actual OTA client (e.g. `esp_https_ota`) to fetch and
      apply an update.
- [ ] Not done: a call to `esp_ota_mark_app_valid_cancel_rollback()`
      once the running image is confirmed healthy. Without it, the
      first real OTA update performed later will silently roll back to
      the previous version on its second boot — rollback is enabled but
      nothing ever confirms an update as good. Must ship together with
      whichever OTA client gets built, not be forgotten as a follow-up.

## Milestone 1c — Network time sync + timezone

Scope: NTP time sync, ported from the DK1-ESP32-C6 project's
`lib/time_sync/` (same API; adapted to drop the header-guard/config.h
conventions that project uses). Lives in `lib/time_sync/`, fully
independent of WiFi/wifi_provision — `system_init.c`'s `on_wifi_status()`
starts it on `WIFI_PROVISION_EVENT_CONNECTED`, the same decoupled
pattern already used for the LED. Three public NTP servers:
pool.ntp.org, time.nist.gov, time.google.com.

The captive portal form also collects a timezone (a `<select>` of
friendly names — UTC + common US zones + UK/Central Europe — mapped to
POSIX TZ strings server-side, not a free-text field someone has to get
exactly right), saved to the same `wifi_cfg` NVS namespace as SSID/
password. `wifi_provision_get_timezone()` exposes it; defaults to
"UTC" if never set. This keeps `wifi_provision` as the one thing that
owns the provisioning form rather than adding a separate settings
module for one field.

- [x] Time syncs within a couple seconds of WiFi connecting (verified
      on hardware 2026-08-02: `got IP` → synced → correct UTC
      timestamp/date logged, e.g. `1785704448` → `2026-08-02 21:00:48`)
- [x] Timezone selected in the captive portal persists across reboot
      and is applied to time sync (verified on hardware 2026-08-02:
      saved `PST8PDT,M3.2.0,M11.1.0`, next boot logged `timezone set
      to: PST8PDT,M3.2.0,M11.1.0` and synced time printed as
      `2026-08-02 14:21:33 PDT`, correctly offset from UTC)
- [ ] Not verified: resync behavior after a WiFi drop/reconnect
      (`time_sync_start()` is idempotent and gets called again on every
      `CONNECTED` event, but this hasn't been tested against a real
      drop — only against the initial connection)

## Milestone 2 — Core app behavior

- [ ] TBD once M1 is verified. Add real feature list here.

## Known Issues / Parking Lot

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
