#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

// Network link chosen in menuconfig (Wi-Fi or cellular modem) + time sync
// over SNTP. Blocks until it gets an IP and syncs, or until the timeout
// expires; time resync keeps going in the background.
// NVS must already be initialized (Wi-Fi stores its calibration there).
esp_err_t network_start(void);

// true once NTP has set the wall clock.
bool network_time_synced(void);

// Converts an instant of the monotonic clock (esp_timer, ms) into epoch time
// (ms, UTC). Returns 0 while NTP has not set the wall clock.
int64_t network_epoch_ms(int64_t mono_ms);
