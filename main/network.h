#pragma once

#include <stdbool.h>

#include "esp_err.h"

// Wi-Fi (station) + time sync over SNTP.
// Blocks until it gets an IP and syncs, or until the timeout expires.
// After that, reconnection and resync keep going in the background.
// NVS must already be initialized (Wi-Fi stores its calibration there).
esp_err_t network_start(void);

// true once NTP has set the wall clock.
bool network_time_synced(void);
