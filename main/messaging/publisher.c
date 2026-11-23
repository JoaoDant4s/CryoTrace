#include "publisher.h"

#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "device_identity.h"
#include "event_payload.h"
#include "mqtt_link.h"
#include "network.h"
#include "event_outbox.h"
#include "event_outbox_store.h"
#include "signed_event.h"

static const char *TAG = "publisher";

typedef enum {
    MSG_FSM_EVENT,
    MSG_LINK_UP,
    MSG_LINK_DOWN,
    MSG_PUBACK,
} msg_type_t;

typedef struct {
    msg_type_t type;
    union {
        fsm_event_t fsm;
        int msg_id;
    };
} msg_t;

static QueueHandle_t s_queue;
static TaskHandle_t s_task;
static event_outbox_t s_outbox;  // owned by the publisher task only
static uint8_t s_box_id[32];
static uint8_t s_domain_separator[32];
static bool s_link_up;

// Per slot, not persisted: for the delivery latency metric.
static int64_t s_queued_us[OUTBOX_CAPACITY];
static uint32_t s_counter[OUTBOX_CAPACITY];

static void post(const msg_t *m)
{
    if (xQueueSend(s_queue, m, 0) != pdTRUE) {
        ESP_LOGE(TAG, "queue full, message %d dropped", m->type);
    }
}

static void on_link_event(mqtt_link_event_t event, int msg_id)
{
    msg_t m = { .msg_id = msg_id };
    m.type = event == MQTT_LINK_CONNECTED ? MSG_LINK_UP
           : event == MQTT_LINK_DISCONNECTED ? MSG_LINK_DOWN
                                             : MSG_PUBACK;
    post(&m);
}

// Signs the FSM transition as the EIP-712 event the contract records and
// queues it in the outbox.
static void sign_and_queue(const fsm_event_t *ev)
{
    uint32_t counter;
    if (device_identity_next_counter(&counter) != ESP_OK) {
        ESP_LOGE(TAG, "event lost: device identity unavailable");
        return;
    }
    // The batch is read per event: the relayer may have announced a new one.
    uint8_t batch_id[32];
    device_identity_batch(batch_id);

    signed_event_t signed_ev;
    event_payload_build(ev, s_box_id, batch_id, (uint64_t)network_epoch_ms(ev->start_ms),
                        (uint64_t)network_epoch_ms(ev->start_ms + ev->duration_ms), counter,
                        &signed_ev);

    uint8_t digest[32], sig[SIGNED_EVENT_SIG_LEN];
    int64_t t0 = esp_timer_get_time();
    signed_event_digest(s_domain_separator, &signed_ev, digest);
    bool signed_ok = device_identity_sign(digest, sig);
    int64_t sign_us = esp_timer_get_time() - t0;
    if (!signed_ok) {
        ESP_LOGE(TAG, "event lost: signing failed");
        return;
    }

    char payload[OUTBOX_PAYLOAD_MAX];
    int len = event_payload_json(&signed_ev, sig, payload, sizeof(payload));
    if (len < 0) {
        ESP_LOGE(TAG, "event lost: payload too large");
        return;
    }

    int evicted;
    int slot = event_outbox_push(&s_outbox, payload, (size_t)len, signed_ev.kind == 1, signed_ev.anomaly_type,
                           signed_ev.start_time_ms, &evicted);
    if (evicted >= 0) {
        ESP_LOGE(TAG, "outbox full: dropped event counter %lu to keep the newest",
                 (unsigned long)s_counter[evicted]);
    }
    s_queued_us[slot] = esp_timer_get_time();
    s_counter[slot] = counter;
    esp_err_t err = event_outbox_store_save(&s_outbox, slot);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "event kept in RAM only (NVS: %s)", esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "%s %s signed (counter %lu, hash+sign %lld us), %u in outbox",
             ev->type == FSM_EVT_ANOMALY ? "ANOMALY" : "RECOVERY", anomaly_fsm_anomaly_name(ev->anomaly),
             (unsigned long)counter, sign_us, (unsigned)event_outbox_count(&s_outbox));
    ESP_LOGD(TAG, "%.*s", len, payload);
}

static void on_puback(int msg_id)
{
    int slot = event_outbox_ack(&s_outbox, msg_id);
    if (slot < 0) {
        return;  // duplicate PUBACK, or for a send replaced after a reconnect
    }
    event_outbox_store_erase(slot);
    ESP_LOGI(TAG, "event counter %lu delivered to the broker in %lld ms, %u left in outbox",
             (unsigned long)s_counter[slot], (esp_timer_get_time() - s_queued_us[slot]) / 1000,
             (unsigned)event_outbox_count(&s_outbox));
}

// Sends every pending event while the link is up.
static void flush(void)
{
    int slot;
    while (s_link_up && (slot = event_outbox_next_pending(&s_outbox)) >= 0) {
        const event_outbox_slot_t *s = &s_outbox.slots[slot];
        int msg_id = mqtt_link_publish("anomaly", s->payload, s->len, 1);
        if (msg_id < 0) {
            return;  // link dropped mid-way: retried on the next connect or tick
        }
        event_outbox_mark_inflight(&s_outbox, slot, msg_id);
    }
}

static void publisher_task(void *arg)
{
    for (;;) {
        msg_t m;
        // Wake up now and then to retry sends that failed without a disconnect event.
        if (xQueueReceive(s_queue, &m, pdMS_TO_TICKS(10000)) == pdTRUE) {
            switch (m.type) {
            case MSG_FSM_EVENT:
                sign_and_queue(&m.fsm);
                break;
            case MSG_LINK_UP:
                s_link_up = true;
                if (event_outbox_count(&s_outbox)) {
                    ESP_LOGI(TAG, "link up: sending %u queued event(s)", (unsigned)event_outbox_count(&s_outbox));
                }
                break;
            case MSG_LINK_DOWN:
                s_link_up = false;
                event_outbox_requeue_inflight(&s_outbox);
                break;
            case MSG_PUBACK:
                on_puback(m.msg_id);
                break;
            }
        }
        flush();
    }
}

esp_err_t publisher_start(const uint8_t box_id[32], const uint8_t domain_separator[32])
{
    memcpy(s_box_id, box_id, 32);
    memcpy(s_domain_separator, domain_separator, 32);
    event_outbox_init(&s_outbox);
    esp_err_t err = event_outbox_store_load(&s_outbox);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "saved events not loaded: %s", esp_err_to_name(err));
    }
    int64_t now = esp_timer_get_time();
    for (int i = 0; i < OUTBOX_CAPACITY; i++) {
        s_queued_us[i] = now;
    }

    s_queue = xQueueCreate(32, sizeof(msg_t));
    // Signing (secp256k1) needs a deep stack.
    if (!s_queue || xTaskCreate(publisher_task, "publisher", 8192, NULL, 4, &s_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return mqtt_link_start(box_id, on_link_event);
}

void publisher_submit(const fsm_event_t *ev)
{
    msg_t m = { .type = MSG_FSM_EVENT, .fsm = *ev };
    post(&m);
}

uint32_t publisher_stack_free(void)
{
    return s_task ? uxTaskGetStackHighWaterMark(s_task) : 0;
}
