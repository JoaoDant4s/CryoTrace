// Host tests for the store-and-forward outbox of signed events.
// Run: make -C test/host

#include <stdio.h>
#include <string.h>

#include "event_outbox.h"

static int failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            printf("  FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
            failures++;                                                 \
        }                                                               \
    } while (0)

enum { BELOW = 0, ABOVE = 1, OFFLINE = 2 };

static int push(event_outbox_t *ob, const char *text, bool recovery, int type, uint64_t start, int *evicted)
{
    return event_outbox_push(ob, text, strlen(text), recovery, (uint8_t)type, start, evicted);
}

static const char *payload_of(const event_outbox_t *ob, int slot)
{
    return ob->slots[slot].payload;
}

static void test_fifo_and_ack(void)
{
    printf("sends oldest first; an event leaves only on PUBACK\n");
    event_outbox_t ob;
    event_outbox_init(&ob);
    int a = push(&ob, "a", false, ABOVE, 1000, NULL);
    int b = push(&ob, "b", true, ABOVE, 1000, NULL);
    CHECK(event_outbox_count(&ob) == 2);

    CHECK(event_outbox_next_pending(&ob) == a);
    event_outbox_mark_inflight(&ob, a, 7);
    CHECK(event_outbox_next_pending(&ob) == b);  // a is in flight, b is next
    event_outbox_mark_inflight(&ob, b, 8);
    CHECK(event_outbox_next_pending(&ob) == -1);

    CHECK(event_outbox_ack(&ob, 99) == -1);      // unknown id
    CHECK(event_outbox_ack(&ob, 8) == b);
    CHECK(event_outbox_count(&ob) == 1);
    CHECK(event_outbox_ack(&ob, 7) == a);
    CHECK(event_outbox_count(&ob) == 0);
    CHECK(event_outbox_ack(&ob, 7) == -1);       // duplicate PUBACK
}

static void test_requeue_on_disconnect(void)
{
    printf("unacknowledged events are resent after a reconnect\n");
    event_outbox_t ob;
    event_outbox_init(&ob);
    int a = push(&ob, "a", false, ABOVE, 1000, NULL);
    event_outbox_mark_inflight(&ob, a, 3);
    event_outbox_requeue_inflight(&ob);
    CHECK(event_outbox_next_pending(&ob) == a);
    // the client may still deliver the old send: its PUBACK frees the event
    CHECK(event_outbox_ack(&ob, 3) == a);
    CHECK(event_outbox_next_pending(&ob) == -1);
}

static void test_offline_accumulates(void)
{
    printf("events produced offline are kept in order\n");
    event_outbox_t ob;
    event_outbox_init(&ob);
    char text[16];
    for (int i = 0; i < OUTBOX_CAPACITY; i++) {
        snprintf(text, sizeof(text), "e%d", i);
        int evicted;
        push(&ob, text, i % 2, ABOVE, 1000 * (uint64_t)(i / 2 + 1), &evicted);
        CHECK(evicted == -1);
    }
    CHECK(event_outbox_count(&ob) == OUTBOX_CAPACITY);
    for (int i = 0; i < OUTBOX_CAPACITY; i++) {
        int s = event_outbox_next_pending(&ob);
        snprintf(text, sizeof(text), "e%d", i);
        CHECK(s >= 0 && strcmp(payload_of(&ob, s), text) == 0);
        event_outbox_mark_inflight(&ob, s, 100 + i);
    }
}

static void test_full_drops_redundant_anomaly_first(void)
{
    printf("when full, drops an anomaly whose recovery is queued, then the oldest\n");
    event_outbox_t ob;
    event_outbox_init(&ob);
    // e0: anomaly that never recovered (only evidence of that excursion)
    push(&ob, "lonely-anomaly", false, ABOVE, 500, NULL);
    // e1/e2: anomaly + its recovery (the recovery alone proves it)
    push(&ob, "paired-anomaly", false, BELOW, 2000, NULL);
    push(&ob, "paired-recovery", true, BELOW, 2000, NULL);
    for (int i = 3; i < OUTBOX_CAPACITY; i++) {
        push(&ob, "sensor-offline", false, OFFLINE, 9000 + (uint64_t)i, NULL);
    }

    int evicted;
    int slot = push(&ob, "new-1", false, ABOVE, 50000, &evicted);
    CHECK(evicted == slot);
    CHECK(event_outbox_count(&ob) == OUTBOX_CAPACITY);
    bool paired_anomaly_left = false, lonely_left = false;
    for (int i = 0; i < OUTBOX_CAPACITY; i++) {
        paired_anomaly_left |= strcmp(ob.slots[i].payload, "paired-anomaly") == 0;
        lonely_left |= strcmp(ob.slots[i].payload, "lonely-anomaly") == 0;
    }
    CHECK(!paired_anomaly_left);
    CHECK(lonely_left);

    // nothing redundant left: the oldest goes
    push(&ob, "new-2", false, ABOVE, 60000, &evicted);
    lonely_left = false;
    for (int i = 0; i < OUTBOX_CAPACITY; i++) {
        lonely_left |= strcmp(ob.slots[i].payload, "lonely-anomaly") == 0;
    }
    CHECK(!lonely_left);
    // and the new events still come out after the older ones
    CHECK(strcmp(payload_of(&ob, event_outbox_next_pending(&ob)), "paired-recovery") == 0);
}

static void test_payload_limit(void)
{
    printf("rejects a payload larger than a slot\n");
    event_outbox_t ob;
    event_outbox_init(&ob);
    static char big[OUTBOX_PAYLOAD_MAX + 1];
    memset(big, 'x', sizeof(big));
    CHECK(event_outbox_push(&ob, big, sizeof(big), false, ABOVE, 0, NULL) == -1);
    CHECK(event_outbox_push(&ob, big, OUTBOX_PAYLOAD_MAX, false, ABOVE, 0, NULL) >= 0);
}

static void test_restore_after_reboot(void)
{
    printf("events restored after a reboot are resent, order kept\n");
    event_outbox_t ob;
    event_outbox_init(&ob);
    int a = push(&ob, "a", false, ABOVE, 1000, NULL);
    int b = push(&ob, "b", true, ABOVE, 1000, NULL);
    event_outbox_mark_inflight(&ob, a, 5);

    event_outbox_t rebooted;
    memcpy(rebooted.slots, ob.slots, sizeof(ob.slots));  // as loaded from flash
    rebooted.next_seq = 0;
    event_outbox_restore(&rebooted);
    CHECK(event_outbox_count(&rebooted) == 2);
    CHECK(event_outbox_ack(&rebooted, 5) == -1);  // ids from before the reboot never match
    CHECK(event_outbox_next_pending(&rebooted) == a);
    int c = push(&rebooted, "c", false, BELOW, 2000, NULL);
    CHECK(rebooted.slots[c].seq > rebooted.slots[b].seq);
}

int main(void)
{
    test_fifo_and_ack();
    test_requeue_on_disconnect();
    test_offline_accumulates();
    test_full_drops_redundant_anomaly_first();
    test_payload_limit();
    test_restore_after_reboot();
    if (failures) {
        printf("%d check(s) FAILED\n", failures);
        return 1;
    }
    printf("all outbox tests passed\n");
    return 0;
}
