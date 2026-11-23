#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Store-and-forward queue of signed events waiting for the broker's PUBACK.
//
// An event leaves the outbox only when the broker acknowledges it, so events
// produced while the box is offline (no coverage in transit) are sent when the
// link comes back, oldest first. Delivery is at-least-once: after a reconnect
// an event may be sent twice, which is harmless because the relayer and the
// contract deduplicate by EIP-712 digest.
//
// When full, the outbox drops first an anomaly whose recovery is also queued
// (the recovery carries the start and end times, so it alone proves the
// excursion), then the oldest event.
//
// Plain C (no ESP-IDF, no locking): owned by a single task. Persisted by the
// caller (see event_outbox_store.h).

#ifndef OUTBOX_CAPACITY
#define OUTBOX_CAPACITY 16
#endif
#define OUTBOX_PAYLOAD_MAX 512

typedef enum {
    OUTBOX_FREE = 0,
    OUTBOX_PENDING,   // waiting to be sent
    OUTBOX_INFLIGHT,  // sent, waiting for PUBACK
} event_outbox_state_t;

typedef struct {
    uint8_t state;         // event_outbox_state_t
    uint8_t is_recovery;   // event kind (RECOVERY = 1)
    uint8_t anomaly_type;
    uint8_t reserved;
    uint32_t seq;          // insertion order
    uint64_t start_time;   // event startTime (epoch ms), to pair anomaly and recovery
    int32_t msg_id;        // MQTT message id of the last send, -1 = never sent
    uint16_t len;
    char payload[OUTBOX_PAYLOAD_MAX];
} event_outbox_slot_t;

typedef struct {
    event_outbox_slot_t slots[OUTBOX_CAPACITY];
    uint32_t next_seq;
} event_outbox_t;

void event_outbox_init(event_outbox_t *ob);

// Queues an event. Returns the slot used, or -1 if the payload is too long.
// If the outbox was full, *evicted (if not NULL) gets the slot whose event was
// dropped to make room (the same slot is reused); otherwise -1.
int event_outbox_push(event_outbox_t *ob, const char *payload, size_t len, bool is_recovery,
                uint8_t anomaly_type, uint64_t start_time, int *evicted);

// Oldest event waiting to be sent, or -1.
int event_outbox_next_pending(const event_outbox_t *ob);

void event_outbox_mark_inflight(event_outbox_t *ob, int slot, int msg_id);

// PUBACK for msg_id: frees its slot and returns it, or -1 if unknown.
int event_outbox_ack(event_outbox_t *ob, int msg_id);

// Link lost: events sent but not acknowledged go back to pending.
void event_outbox_requeue_inflight(event_outbox_t *ob);

size_t event_outbox_count(const event_outbox_t *ob);

// Adopts slots loaded from storage (e.g. after a reboot): they become pending
// and the sequence continues after the highest one found.
void event_outbox_restore(event_outbox_t *ob);
