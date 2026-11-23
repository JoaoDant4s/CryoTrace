#pragma once

#include "esp_err.h"
#include "event_outbox.h"

// Keeps the outbox in NVS (one key per slot), so signed events waiting for the
// broker survive a reboot or power loss. NVS must already be initialized.

// Loads the saved slots into ob (already initialized) and calls event_outbox_restore.
esp_err_t event_outbox_store_load(event_outbox_t *ob);

esp_err_t event_outbox_store_save(const event_outbox_t *ob, int slot);

esp_err_t event_outbox_store_erase(int slot);
