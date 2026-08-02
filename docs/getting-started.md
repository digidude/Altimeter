# Getting Started

Follow these steps to connect a new (or [factory-reset](factory-reset.md))
device to your WiFi network.

## 1. Power on the device

On first boot, or any time it has no saved network, the status LED
shows **solid amber**.

## 2. Connect to the device's setup network

On your phone or laptop, open WiFi settings and join the network named:

```
ESP32-Setup-XXXX
```

(the last four characters are unique to your device). It's an open
network — no password needed.

## 3. Open the setup page

Most phones and laptops automatically pop up a "Sign in to network" or
"Join" page within a few seconds of connecting. If it doesn't appear,
open a browser and go to:

```
http://192.168.4.1
```

## 4. Fill in the form

- **Network name (SSID)** — pick your WiFi network from the list, or
  type it manually if it's hidden or out of range
- **Password**
- **Device name** — defaults to `altimeter`. Once connected, the
  device is reachable at `http://<device name>.local`. Letters,
  numbers, and hyphens only.
- **Timezone**

Tap **Save & Connect**.

## 5. Wait for it to connect

The device reboots and joins your network. Watch the status LED:

| LED | Meaning |
|---|---|
| Blinking amber (fast) | Connecting |
| Solid green | Connected |
| Blinking amber (slow) | Couldn't connect — double-check the password. The device falls back to the `ESP32-Setup-XXXX` setup network so you can try again. |

Once the LED is solid green, the device is on your network and
reachable at `http://<device name>.local`.
