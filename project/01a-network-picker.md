[← Milestones overview](README.md)

# Milestone 1a — Network picker UX

Scope: the setup page scans for nearby networks (device runs SoftAP +
STA together — `WIFI_MODE_APSTA`) and offers them as a dropdown, since
the exact SSID (case-sensitive) isn't always obvious to whoever's
provisioning the device. Still a free-text field underneath, so a
hidden or out-of-range network can be typed by hand. Also adds a "Show
password" toggle on the password field.

- [x] Setup page shows "Scanning for networks…" on load, then populates
      a dropdown/datalist of nearby SSIDs
- [x] "Show password" checkbox reveals/hides the typed password
- [x] Selecting a network from the dropdown (not typing it) and
      submitting connects successfully
- [ ] A hidden/out-of-range network not in the list can still be typed
      manually and submitted successfully
- [ ] Two APs broadcasting the same SSID (e.g. a mesh network) show up
      as one entry, not duplicated
- [ ] The AP + captive portal (HTTP form, DNS hijack) keep responding
      normally to a connected client while a scan is in progress —
      scanning blocks the httpd worker task for ~1-3s per request, so
      this is the one place coexistence could visibly stall
