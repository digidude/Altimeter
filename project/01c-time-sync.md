[← Milestones overview](README.md)

# Milestone 1c — Network time sync + timezone

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
