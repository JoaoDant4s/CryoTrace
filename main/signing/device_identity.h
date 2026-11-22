#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "signed_event.h"

// Device signing key, event counter and current batch, kept in NVS.
// The key is generated on the device at first boot and never leaves it; only
// its Ethereum address is logged, to be registered in the contract.
// NVS must be initialized before calling device_identity_init().

// initial_batch_id: batch used until one is set with device_identity_set_batch().
esp_err_t device_identity_init(const uint8_t initial_batch_id[32]);

void device_identity_address(uint8_t address[20]);

// Reserves the next counter value. The increment is committed to flash before
// returning, so a value is never reused across reboots.
esp_err_t device_identity_next_counter(uint32_t *counter);

bool device_identity_sign(const uint8_t digest[32], uint8_t sig[SIGNED_EVENT_SIG_LEN]);

// Batch the box currently carries (keccak256 of the batch id). A box carries
// one batch at a time; the next one is set when the box is reloaded, without
// reflashing. The contract is the source of truth (registerBatch): events
// signed for a batch not assigned to this box are rejected on-chain.
void device_identity_batch(uint8_t batch_id[32]);

// Persists the new current batch in NVS. Safe to call from another task.
esp_err_t device_identity_set_batch(const uint8_t batch_id[32]);
