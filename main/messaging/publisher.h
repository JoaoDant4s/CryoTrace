#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "fsm.h"

// Publisher task: turns FSM transitions into signed events and delivers them.
//
// For each event: takes the next counter (committed to NVS first), signs the
// EIP-712 digest with the device key, stores the JSON in the outbox (RAM +
// NVS) and publishes it with QoS 1 on cryotrace/<box_name>/anomaly. The event
// leaves the outbox only on PUBACK; while offline, events wait there and are
// sent oldest first when the link returns.
//
// Signing runs here, not in the sampling task, so sampling keeps its period.

esp_err_t publisher_start(const uint8_t box_id[32], const uint8_t domain_separator[32]);

// Hands an FSM transition to the publisher (non-blocking). Call from the
// monitoring task.
void publisher_submit(const fsm_event_t *ev);

// Logs free stack of the publisher task (bytes), for the memory metrics.
uint32_t publisher_stack_free(void);
