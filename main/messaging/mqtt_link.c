#include "mqtt_link.h"

#include <stdio.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "sdkconfig.h"

#include "batch_message.h"
#include "device_identity.h"

static const char *TAG = "mqtt";

static esp_mqtt_client_handle_t s_client;
static mqtt_link_cb_t s_cb;
static volatile bool s_connected;
static uint8_t s_box_id[32];
static char s_status_topic[64];
static char s_batch_topic[BATCH_TOPIC_SIZE];

static void on_batch_message(const char *data, int len)
{
    uint8_t batch[32], current[32];
    switch (batch_message_parse(data, (size_t)len, s_box_id, batch)) {
    case BATCH_MSG_OK:
        device_identity_batch(current);
        if (memcmp(batch, current, 32) == 0) {
            ESP_LOGI(TAG, "batch confirmed by the relayer");
            return;
        }
        if (device_identity_set_batch(batch) == ESP_OK) {
            ESP_LOGW(TAG, "new batch 0x%02x%02x%02x%02x… from the relayer: next events are signed with it",
                     batch[0], batch[1], batch[2], batch[3]);
        } else {
            ESP_LOGE(TAG, "could not store the new batch");
        }
        return;
    case BATCH_MSG_EMPTY:
        return;  // announcement cleared: keep the current batch
    case BATCH_MSG_OTHER_BOX:
        ESP_LOGW(TAG, "batch announcement for another box ignored");
        return;
    case BATCH_MSG_MALFORMED:
        ESP_LOGW(TAG, "malformed batch announcement ignored");
        return;
    }
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    esp_mqtt_event_handle_t ev = data;
    switch ((esp_mqtt_event_id_t)id) {
    case MQTT_EVENT_CONNECTED:
        s_connected = true;
        ESP_LOGI(TAG, "connected to %s", CONFIG_CRYO_MQTT_BROKER_URI);
        esp_mqtt_client_publish(s_client, s_status_topic, "online", 0, 1, 1);
        esp_mqtt_client_subscribe(s_client, s_batch_topic, 1);
        s_cb(MQTT_LINK_CONNECTED, 0);
        break;
    case MQTT_EVENT_DISCONNECTED:
        if (s_connected) {
            ESP_LOGW(TAG, "disconnected; reconnecting");
        }
        s_connected = false;
        s_cb(MQTT_LINK_DISCONNECTED, 0);
        break;
    case MQTT_EVENT_PUBLISHED:
        s_cb(MQTT_LINK_PUBLISHED, ev->msg_id);
        break;
    case MQTT_EVENT_DATA:
        // Announcements are small; a fragmented message is not one of ours.
        if (ev->current_data_offset == 0 && ev->data_len == ev->total_data_len &&
            ev->topic_len == (int)strlen(s_batch_topic) &&
            memcmp(ev->topic, s_batch_topic, ev->topic_len) == 0) {
            on_batch_message(ev->data, ev->data_len);
        }
        break;
    case MQTT_EVENT_ERROR:
        if (ev->error_handle && ev->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
            ESP_LOGE(TAG, "broker refused the connection (check user/password)");
        }
        break;
    default:
        break;
    }
}

esp_err_t mqtt_link_start(const uint8_t box_id[32], mqtt_link_cb_t cb)
{
    s_cb = cb;
    memcpy(s_box_id, box_id, 32);
    snprintf(s_status_topic, sizeof(s_status_topic), "cryotrace/%s/status", CONFIG_CRYO_BOX_NAME);
    batch_message_topic(box_id, s_batch_topic, sizeof(s_batch_topic));

    const esp_mqtt_client_config_t cfg = {
        .broker = {
            .address.uri = CONFIG_CRYO_MQTT_BROKER_URI,
            // Public CAs bundled with ESP-IDF (cloud brokers use public CAs).
            .verification.crt_bundle_attach = esp_crt_bundle_attach,
        },
        .credentials = {
            .client_id = "cryotrace-" CONFIG_CRYO_BOX_NAME,
            .username = CONFIG_CRYO_MQTT_USERNAME[0] ? CONFIG_CRYO_MQTT_USERNAME : NULL,
            .authentication.password = CONFIG_CRYO_MQTT_PASSWORD[0] ? CONFIG_CRYO_MQTT_PASSWORD : NULL,
        },
        .session = {
            .last_will = {
                .topic = s_status_topic,
                .msg = "offline",
                .qos = 1,
                .retain = 1,
            },
            // Keeps the batch subscription across reconnects.
            .disable_clean_session = true,
            .keepalive = CONFIG_CRYO_MQTT_KEEPALIVE_S,
        },
    };
    s_client = esp_mqtt_client_init(&cfg);
    if (!s_client) {
        return ESP_FAIL;
    }
    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, on_event, NULL);
    ESP_LOGI(TAG, "batch announcements on %s", s_batch_topic);
    return esp_mqtt_client_start(s_client);
}

bool mqtt_link_connected(void)
{
    return s_connected;
}

int mqtt_link_publish(const char *subtopic, const char *data, int len, int qos)
{
    if (!s_connected) {
        return -1;
    }
    char topic[64];
    snprintf(topic, sizeof(topic), "cryotrace/%s/%s", CONFIG_CRYO_BOX_NAME, subtopic);
    return esp_mqtt_client_publish(s_client, topic, data, len, qos, 0);
}
