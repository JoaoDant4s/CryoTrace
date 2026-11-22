#include "device_identity.h"

#include <stdio.h>
#include <string.h>

#include "bootloader_random.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "nvs.h"

#include "memzero.h"

#define NVS_NAMESPACE "cryotrace"
#define NVS_KEY_PRIVATE "device_key"
#define NVS_KEY_COUNTER "counter"
#define NVS_KEY_BATCH "batch_id"

static const char *TAG = "identity";

static uint8_t s_private_key[32];
static uint8_t s_address[20];
static nvs_handle_t s_nvs;

// Read by the monitor task, written when a new batch arrives.
static uint8_t s_batch_id[32];
static portMUX_TYPE s_batch_lock = portMUX_INITIALIZER_UNLOCKED;

// Called before Wi-Fi starts, so the hardware RNG needs an explicit entropy
// source (see esp_random() docs); bootloader_random provides it.
static void generate_key(uint8_t key[32])
{
    bootloader_random_enable();
    do {
        esp_fill_random(key, 32);
    } while (!signed_event_address(key, s_address)); // rejects 0 and >= curve order
    bootloader_random_disable();
}

static esp_err_t load_or_create_key(void)
{
    size_t len = sizeof(s_private_key);
    esp_err_t err = nvs_get_blob(s_nvs, NVS_KEY_PRIVATE, s_private_key, &len);
    if (err == ESP_OK && len == sizeof(s_private_key)) {
        return signed_event_address(s_private_key, s_address) ? ESP_OK : ESP_ERR_INVALID_STATE;
    }
    if (err != ESP_ERR_NVS_NOT_FOUND) {
        return err == ESP_OK ? ESP_ERR_INVALID_SIZE : err;
    }

    ESP_LOGW(TAG, "no device key yet, generating one");
    generate_key(s_private_key);
    err = nvs_set_blob(s_nvs, NVS_KEY_PRIVATE, s_private_key, sizeof(s_private_key));
    if (err == ESP_OK) {
        err = nvs_commit(s_nvs);
    }
    return err;
}

static esp_err_t load_batch(const uint8_t initial_batch_id[32])
{
    size_t len = sizeof(s_batch_id);
    esp_err_t err = nvs_get_blob(s_nvs, NVS_KEY_BATCH, s_batch_id, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        memcpy(s_batch_id, initial_batch_id, sizeof(s_batch_id));
        return ESP_OK;
    }
    if (err == ESP_OK && len != sizeof(s_batch_id)) {
        return ESP_ERR_INVALID_SIZE;
    }
    return err;
}

esp_err_t device_identity_init(const uint8_t initial_batch_id[32])
{
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &s_nvs);
    if (err != ESP_OK) {
        return err;
    }
    err = load_batch(initial_batch_id);
    if (err != ESP_OK) {
        return err;
    }
    err = load_or_create_key();
    if (err != ESP_OK) {
        memzero(s_private_key, sizeof(s_private_key));
        return err;
    }

    char hex[41];
    for (int i = 0; i < 20; i++) {
        sprintf(&hex[2 * i], "%02x", s_address[i]);
    }
    ESP_LOGI(TAG, "device address (register in the contract): 0x%s", hex);
    return ESP_OK;
}

void device_identity_address(uint8_t address[20])
{
    memcpy(address, s_address, sizeof(s_address));
}

esp_err_t device_identity_next_counter(uint32_t *counter)
{
    uint32_t value = 0;
    esp_err_t err = nvs_get_u32(s_nvs, NVS_KEY_COUNTER, &value);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        return err;
    }
    err = nvs_set_u32(s_nvs, NVS_KEY_COUNTER, value + 1);
    if (err == ESP_OK) {
        err = nvs_commit(s_nvs);
    }
    if (err == ESP_OK) {
        *counter = value;
    }
    return err;
}

bool device_identity_sign(const uint8_t digest[32], uint8_t sig[SIGNED_EVENT_SIG_LEN])
{
    return signed_event_sign(s_private_key, digest, sig);
}

void device_identity_batch(uint8_t batch_id[32])
{
    taskENTER_CRITICAL(&s_batch_lock);
    memcpy(batch_id, s_batch_id, sizeof(s_batch_id));
    taskEXIT_CRITICAL(&s_batch_lock);
}

esp_err_t device_identity_set_batch(const uint8_t batch_id[32])
{
    esp_err_t err = nvs_set_blob(s_nvs, NVS_KEY_BATCH, batch_id, sizeof(s_batch_id));
    if (err == ESP_OK) {
        err = nvs_commit(s_nvs);
    }
    if (err != ESP_OK) {
        return err;
    }
    taskENTER_CRITICAL(&s_batch_lock);
    memcpy(s_batch_id, batch_id, sizeof(s_batch_id));
    taskEXIT_CRITICAL(&s_batch_lock);
    ESP_LOGI(TAG, "current batch updated");
    return ESP_OK;
}
