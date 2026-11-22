#pragma once

#include <stdbool.h>

#include "esp_err.h"

// Network link chosen in menuconfig (Wi-Fi or cellular modem) + time sync
// over SNTP. Blocks until it gets an IP and syncs, or until the timeout
// expires; time resync keeps going in the background.
// NVS must already be initialized (Wi-Fi stores its calibration there).
esp_err_t network_start(void);

// true once NTP has set the wall clock.
bool network_time_synced(void);
