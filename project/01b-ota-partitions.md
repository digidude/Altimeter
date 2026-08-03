[← Milestones overview](README.md)

# Milestone 1b — OTA-ready partition table

## Scope: 

Enable Over-The-Air (OTA) updates -- Switched from a single 1MB `factory`  app partition to a two-slot OTA layout (`ota_0`/`ota_1`, 1700K each, plus `otadata`) via `board_build.partitions = partitions_two_ota_large.csv` in `platformio.ini` (framework-bundled CSV, no project-local file needed), and `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` in `sdkconfig.defaults`. This is groundwork only — no actual OTA update client exists yet.

Definition of done — all of these need an actual test, not a read-through:

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
