# Altimeter — Milestones

## Toolchain

PlatformIO, `framework = espidf` (native ESP-IDF, no Arduino
layer). Board: `esp32-c6-devkitc-1`.

## Rules for this project

- Nothing in Milestone 1+ gets built until Milestone 0
  is verified on real hardware against real failure modes, not just "it
  connected once on the bench."

## Milestones

- [Milestone 0 — Bring-It-Up](00-bring-up.md)
- [Milestone 1 — Connectivity](01-connectivity.md) (blocks all app features)
- [Milestone 1a — Network picker UX](01a-network-picker.md)
- [Milestone 1b — OTA-ready partition table](01b-ota-partitions.md)
- [Milestone 1c — Network time sync + timezone](01c-time-sync.md)
- [Milestone 2 — Core app behavior](02-core-app.md)
- [Known Issues / Parking Lot](known-issues.md)
