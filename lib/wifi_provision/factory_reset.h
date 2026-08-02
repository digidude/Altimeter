#pragma once

// Watches the devkit's BOOT button (GPIO9) in the background for the
// lifetime of the device. Holding it down for several seconds — at any
// time while the device is running, not just at power-on — erases
// saved WiFi credentials and reboots back into provisioning. This is
// the field-accessible equivalent of `pio run -t erase`.
//
// GPIO9 doubles as a chip strapping pin (pulling it low during the
// power-on/reset sequence puts the ROM bootloader into UART download
// mode), but that only matters in the brief window before app_main()
// runs. By the time this task starts, strapping has already been
// latched and GPIO9 is just a normal input — reading it low here has
// no effect on boot mode.
void factory_reset_start(void);
