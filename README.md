# Altimeter — WiFi Provisioning (Milestone 1)

Native ESP-IDF (no Arduino layer), built through PlatformIO.
Board: `esp32-c6-devkitc-1`

WARNING: AI Slop Coded using Claude. This is a slop project to learn
Claude and ESP32's. Building is small incremental blocks of working 
functionality.

Fork and thrash on your own, or wait around till I add more code.

Open to suggestions, enhanements, and bug reports

---

## Layout

```
platformio.ini          board/framework config
CMakeLists.txt          root ESP-IDF project cmake — adds lib/ to
                         EXTRA_COMPONENT_DIRS (appended, not overwritten,
                         since PlatformIO injects its own entry there too)
src/
  CMakeLists.txt         glob-registers everything in src/, PRIV_REQUIRES
                         the wifi_provision/led_indicator/time_sync
                         components
  main.c                 app_main: logs boot, calls system_init()
  system_init.h/.c        boot sequence (NVS, LED, WiFi provisioning) and
                         the WiFi-status → LED-state / time-sync wiring —
                         app-specific orchestration, not a reusable
                         component, so it lives here rather than lib/
lib/
  wifi_provision/        ESP-IDF component (own CMakeLists.txt)
    CMakeLists.txt
    include/
      wifi_provision.h   public API — the only header outside the
                         component includes
    wifi_provision.c     state machine: NVS creds, STA connect + retry/
                         backoff, SoftAP fallback, HTTP handlers
                         (including /scan for the network picker)
    dns_server.h/.c      raw DNS hijack so captive portal prompts trigger
    provision.html       setup page source (no external assets) — plain
                         HTML/CSS/JS, converted into a C string header
                         at CMake configure time, not hand-escaped
    factory_reset.h/.c   BOOT-button (GPIO9) hold-to-erase watcher
  led_indicator/          WS2812 status LED, generic state names — no
                         WiFi/protocol knowledge of its own
  time_sync/              NTP client (ported from DK1-ESP32-C6), also
                         fully generic — started by system_init.c on a
                         WiFi-connected event, not called from within
                         wifi_provision
sdkconfig.defaults       small config tweaks (main task stack size,
                         flash size, OTA rollback)
platformio.ini           board/framework config — also sets
                         board_build.partitions for the OTA-ready
                         two-slot layout
project/                 milestone tracker / definition of done, one
                         file per milestone — see project/README.md
docs/                    end-user documentation (MkDocs) — provisioning
                         and factory-reset instructions
```

`include/`, `test/`, `.vscode/`, `.gitignore` are the standard
PlatformIO scaffold — untouched.

---

## Build / Flash

```
pio run                       # build
pio run -t upload             # flash
pio run -t upload -t monitor  # flash + open serial monitor
```

The project already has a build cache under `.pio/build/` from the
initial empty `app_main() {}` stub. Since the source set changed
(several new files), run a clean build once before trusting the output:

```
pio run -t clean
pio run
```

To erase saved WiFi credentials and force a fresh provisioning cycle:

```
pio run -t erase
```

(Full flash erase — also wipes any app data you add later. Fine for now
since there isn't any yet.)

Note: `sdkconfig.defaults` only affects a *freshly generated* sdkconfig.
Since a `sdkconfig.esp32-c6-devkitc-1` already exists in this project
from the earlier stub build, the stack-size bump in `sdkconfig.defaults`
won't apply until you either delete that generated file or run
`pio run -t menuconfig` and set it by hand. If you don't hit stack
overflow warnings, don't worry about it.

---

## How it works

1. On boot, `wifi_provision_start()` checks NVS for saved SSID/password.
2. **No creds, or saved creds fail to connect (bounded retries):** device
   starts a SoftAP named `ESP32-Setup-XXXX` and a captive portal (running
   `WIFI_MODE_APSTA` so it can scan while the portal stays up). Connecting
   a phone/laptop to that network should trigger the OS's automatic "Sign
   in to network" prompt, backed by a DNS server that answers every
   lookup with the device's own IP. If the prompt doesn't auto-open,
   visit `http://192.168.4.1` manually. The setup page scans on load and
   offers nearby SSIDs as a dropdown (still free-text underneath, for
   hidden/out-of-range networks), plus a "Show password" toggle.
3. Submitting the form saves credentials to NVS and reboots the device.
4. **Creds work:** device connects and stays connected. If WiFi drops
   afterward (router reboot, temporary interference), it retries with
   exponential backoff (1s → 60s cap) indefinitely — it does **not** fall
   back to AP mode once it's been stably connected this boot. That
   fallback only happens for *boot-time* connection failures, on the
   theory that a mid-session drop is transient but a boot-time failure
   might mean the password's wrong or the network's gone.

## Testing against Milestone 1 (see project/01-connectivity.md)

Run these in order, on real hardware, before touching any app feature:

1. `pio run -t erase && pio run -t upload -t monitor` — confirm it boots
   straight into AP mode with no saved creds.
2. Join `ESP32-Setup-XXXX` from a phone, confirm the sign-in prompt pops
   up automatically, submit real credentials, confirm it reboots and
   connects (watch the serial monitor for "got IP").
3. Power-cycle the board (unplug/replug, not just reset) — confirm it
   reconnects on its own with no re-provisioning.
4. Reboot your WiFi router while the device is running — confirm it
   reconnects once the router's back up (check monitor for backoff/retry
   logs, then "got IP" again).
5. `pio run -t erase`, re-provision with a **wrong password on purpose**
   — confirm it gives up after the bounded retries and drops back into
   AP/captive portal instead of hanging silently.

Only check items off in the milestone docs (`project/`) once you've actually run these —
not once the code looks right.

## Known rough edges (tracked in project/known-issues.md)

- Saving credentials always triggers a full reboot.
- ~~No mDNS/hostname yet.~~ — added: reachable as `http://<name>.local`,
  hostname defaults to `altimeter`, user-configurable via the captive
  portal form (stored in NVS alongside SSID/password/timezone). Ping 
  `http://altimeter.local` will return the IP address. Doesn't not (yet?)
  have an httpd service running full time. Only during provisioning.
