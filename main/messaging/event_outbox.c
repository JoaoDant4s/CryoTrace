#include "event_outbox.h"

#include <string.h>

void event_outbox_init(event_outbox_t *ob)
{
    memset(ob, 0, sizeof(*ob));
}

static int free_slot(const event_outbox_t *ob)
{
    for (int i = 0; i < OUTBOX_CAPACITY; i++) {
        if (ob->slots[i].state == OUTBOX_FREE) {
            return i;
        }
    }
    return -1;
}

// An anomaly whose recovery is also queued: same type and start, queued later.
static bool is_redundant(const event_outbox_t *ob, int i)
{
    const event_outbox_slot_t *a = &ob->slots[i];
    if (a->is_recovery) {
        return false;
    }
    for (int j = 0; j < OUTBOX_CAPACITY; j++) {
        const event_outbox_slot_t *r = &ob->slots[j];
        if (r->state != OUTBOX_FREE && r->is_recovery && r->anomaly_type == a->anomaly_type &&
            r->start_time == a->start_time && r->seq > a->seq) {
            return true;
        }
    }
    return false;
}

static int victim_slot(const event_outbox_t *ob)
{
    int oldest = -1, oldest_redundant = -1;
    for (int i = 0; i < OUTBOX_CAPACITY; i++) {
        const event_outbox_slot_t *s = &ob->slots[i];
        if (s->state == OUTBOX_FREE) {
            continue;
        }
        if (oldest < 0 || s->seq < ob->slots[oldest].seq) {
            oldest = i;
        }
        if (is_redundant(ob, i) && (oldest_redundant < 0 || s->seq < ob->slots[oldest_redundant].seq)) {
            oldest_redundant = i;
        }
    }
    return oldest_redundant >= 0 ? oldest_redundant : oldest;
}

int event_outbox_push(event_outbox_t *ob, const char *payload, size_t len, bool is_recovery,
                uint8_t anomaly_type, uint64_t start_time, int *evicted)
{
    if (evicted) {
        *evicted = -1;
    }
    if (len > OUTBOX_PAYLOAD_MAX) {
        return -1;
    }
    int i = free_slot(ob);
    if (i < 0) {
        i = victim_slot(ob);
        if (evicted) {
            *evicted = i;
        }
    }
    event_outbox_slot_t *s = &ob->slots[i];
    memset(s, 0, sizeof(*s));
    s->state = OUTBOX_PENDING;
    s->is_recovery = is_recovery;
    s->anomaly_type = anomaly_type;
    s->start_time = start_time;
    s->seq = ob->next_seq++;
    s->msg_id = -1;
    s->len = (uint16_t)len;
    memcpy(s->payload, payload, len);
    return i;
}

int event_outbox_next_pending(const event_outbox_t *ob)
{
    int best = -1;
    for (int i = 0; i < OUTBOX_CAPACITY; i++) {
        const event_outbox_slot_t *s = &ob->slots[i];
        if (s->state == OUTBOX_PENDING && (best < 0 || s->seq < ob->slots[best].seq)) {
            best = i;
        }
    }
    return best;
}

void event_outbox_mark_inflight(event_outbox_t *ob, int slot, int msg_id)
{
    ob->slots[slot].state = OUTBOX_INFLIGHT;
    ob->slots[slot].msg_id = msg_id;
}

int event_outbox_ack(event_outbox_t *ob, int msg_id)
{
    // Also matches requeued events: the MQTT client may still deliver a send
    // made before the link dropped.
    for (int i = 0; i < OUTBOX_CAPACITY; i++) {
        event_outbox_slot_t *s = &ob->slots[i];
        if (s->state != OUTBOX_FREE && s->msg_id >= 0 && s->msg_id == msg_id) {
            s->state = OUTBOX_FREE;
            return i;
        }
    }
    return -1;
}

void event_outbox_requeue_inflight(event_outbox_t *ob)
{
    for (int i = 0; i < OUTBOX_CAPACITY; i++) {
        if (ob->slots[i].state == OUTBOX_INFLIGHT) {
            ob->slots[i].state = OUTBOX_PENDING;
        }
    }
}

size_t event_outbox_count(const event_outbox_t *ob)
{
    size_t n = 0;
    for (int i = 0; i < OUTBOX_CAPACITY; i++) {
        n += ob->slots[i].state != OUTBOX_FREE;
    }
    return n;
}

void event_outbox_restore(event_outbox_t *ob)
{
    ob->next_seq = 0;
    for (int i = 0; i < OUTBOX_CAPACITY; i++) {
        event_outbox_slot_t *s = &ob->slots[i];
        if (s->state == OUTBOX_FREE) {
            continue;
        }
        // msg ids belong to the previous connection: never match them again.
        s->state = OUTBOX_PENDING;
        s->msg_id = -1;
        if (s->seq >= ob->next_seq) {
            ob->next_seq = s->seq + 1;
        }
    }
}
