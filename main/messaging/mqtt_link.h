#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

// MQTT over TLS to the broker (menuconfig > CryoTrace - MQTT).
//
// - Status: cryotrace/<box_name>/status is "online" (retained) while connected; the
//   broker sets it to "offline" if the box drops (last will).
// - Batch: subscribes to cryotrace/0x<boxId>/batch; a valid announcement from
//   the relayer becomes the batch the box signs its events with (NVS).
// Reconnects by itself when the network comes back.

typedef enum {
    MQTT_LINK_CONNECTED,
    MQTT_LINK_DISCONNECTED,
    MQTT_LINK_PUBLISHED,  // PUBACK for msg_id (QoS 1)
} mqtt_link_event_t;

// Called from the MQTT client task: must not block.
typedef void (*mqtt_link_cb_t)(mqtt_link_event_t event, int msg_id);

esp_err_t mqtt_link_start(const uint8_t box_id[32], mqtt_link_cb_t cb);

bool mqtt_link_connected(void);

// Publishes on cryotrace/<box_name>/<subtopic>. Returns the message id (0 for QoS 0),
// or -1 if not connected or the send failed.
int mqtt_link_publish(const char *subtopic, const char *data, int len, int qos);
