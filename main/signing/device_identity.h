#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "signed_event.h"

// Device signing key and event counter, kept in NVS.
// The key is generated on the device at first boot and never leaves it; only
// its Ethereum address is logged, to be registered in the contract.
// NVS must be initialized before calling device_identity_init().

esp_err_t device_identity_init(void);

void device_identity_address(uint8_t address[20]);

// Reserves the next counter value. The increment is committed to flash before
// returning, so a value is never reused across reboots.
esp_err_t device_identity_next_counter(uint32_t *counter);

bool device_identity_sign(const uint8_t digest[32], uint8_t sig[SIGNED_EVENT_SIG_LEN]);
