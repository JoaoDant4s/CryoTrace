#pragma once

#include "esp_err.h"

// Network link selected in menuconfig (CryoTrace - Network > Network link):
// implemented by network_wifi.c or network_cellular.c, only one is built.
// Called by network.c once esp_netif and the default event loop exist.

// Brings the link up and waits for an IP address, up to a timeout suited to
// the link. Returns ESP_ERR_INVALID_STATE if the link is not configured, or
// another error if there is no IP in time.
esp_err_t network_link_start(void);

const char *network_link_name(void);
