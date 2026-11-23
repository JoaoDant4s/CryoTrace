#include "event_outbox_store.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "outbox";
static const char *NAMESPACE = "outbox";

static void slot_key(int slot, char key[8])
{
    snprintf(key, 8, "s%02d", slot);
}

esp_err_t event_outbox_store_load(event_outbox_t *ob)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;  // nothing saved yet
    }
    if (err != ESP_OK) {
        return err;
    }
    int loaded = 0;
    for (int i = 0; i < OUTBOX_CAPACITY; i++) {
        char key[8];
        slot_key(i, key);
        size_t size = sizeof(event_outbox_slot_t);
        event_outbox_slot_t *s = &ob->slots[i];
        err = nvs_get_blob(h, key, s, &size);
        // A different size means a firmware with another layout: discard it.
        if (err != ESP_OK || size != sizeof(event_outbox_slot_t) || s->state == OUTBOX_FREE ||
            s->len > OUTBOX_PAYLOAD_MAX) {
            if (err == ESP_OK) {
                ESP_LOGW(TAG, "discarding unreadable saved event in slot %d", i);
            }
            memset(s, 0, sizeof(*s));
            continue;
        }
        loaded++;
    }
    nvs_close(h);
    event_outbox_restore(ob);
    if (loaded) {
        ESP_LOGW(TAG, "%d signed event(s) from before the reboot still to send", loaded);
    }
    return ESP_OK;
}

esp_err_t event_outbox_store_save(const event_outbox_t *ob, int slot)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    char key[8];
    slot_key(slot, key);
    err = nvs_set_blob(h, key, &ob->slots[slot], sizeof(event_outbox_slot_t));
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

esp_err_t event_outbox_store_erase(int slot)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    char key[8];
    slot_key(slot, key);
    err = nvs_erase_key(h, key);
    if (err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}
