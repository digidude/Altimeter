#pragma once

#include <stdint.h>
#include "esp_err.h"

// Starts a minimal DNS server on UDP/53 that answers EVERY A-record
// query with `ip_addr_be` (IPv4 address in network byte order, e.g.
// esp_netif_ip_info_t.ip.addr). This is the trick that makes phones and
// laptops pop up the "Sign in to network" captive portal prompt when
// they join the device's SoftAP: they ask DNS for some canary hostname,
// we always answer with our own IP, they load our page over HTTP.
//
// Runs in its own FreeRTOS task. Call captive_dns_stop() to tear it
// down (e.g. once provisioning succeeds, if you're not just rebooting).
esp_err_t captive_dns_start(uint32_t ip_addr_be);
void captive_dns_stop(void);
