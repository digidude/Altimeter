[← Milestones overview](README.md)

# Milestone 1 — Connectivity (blocks all app features)

Scope: device with no saved credentials 

- boots into a SoftAP + captive portal
- accepts SSID/password from a phone or laptop
- saves them to NVS (non-volidal storage)
- connects to wifi
- reconnects reliably if there is signal loss 
  
This is all implemented in `lib/wifi_provision/` 
(its own ESP-IDF component — see README.md Layout).

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
