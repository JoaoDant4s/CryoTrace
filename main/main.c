#include <math.h>
#include <stdio.h>
#include <sys/time.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#include "device_identity.h"
#include "event_payload.h"
#include "filter.h"
#include "fsm.h"
#include "network.h"
#include "sensor.h"
#include "signed_event.h"

#define EVENT_PAYLOAD_MAX 512

static const char *TAG = "cryotrace";

static uint8_t s_box_id[32];
static uint8_t s_domain_separator[32];
static bool s_identity_ready;

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

// Converts a monotonic clock instant into epoch time (ms, UTC).
// Returns 0 while NTP has not set the wall clock.
static int64_t to_epoch_ms(int64_t mono_ms)
{
    if (!network_time_synced()) {
        return 0;
    }
    struct timeval tv;
    gettimeofday(&tv, NULL);
    int64_t epoch_now = (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
    return epoch_now - (now_ms() - mono_ms);
}

// JSON has no NaN: an invalid reading becomes null.
static void temp_json(char *buf, size_t size, float t)
{
    if (isnan(t)) {
        snprintf(buf, size, "null");
    } else {
        snprintf(buf, size, "%.2f", t);
    }
}

// For now messages only go to the log, already in the format and on
// the topic they will be published to over MQTT.
static void publish_telemetry(float raw, float filtered, fsm_state_t state)
{
    char s_raw[16], s_filtered[16], payload[160];
    temp_json(s_raw, sizeof(s_raw), raw);
    temp_json(s_filtered, sizeof(s_filtered), filtered);
    snprintf(payload, sizeof(payload),
             "{\"ts\":%lld,\"temp\":%s,\"raw\":%s,\"state\":\"%s\"}",
             to_epoch_ms(now_ms()), s_filtered, s_raw, anomaly_fsm_state_name(state));
    ESP_LOGI(TAG, "coldchain/%s/telemetry %s", CONFIG_CRYO_BOX_ID, payload);
}

// Signs the FSM transition as the EIP-712 event recorded by the contract.
static void publish_event(const fsm_event_t *ev)
{
    uint32_t counter;
    if (!s_identity_ready || device_identity_next_counter(&counter) != ESP_OK) {
        ESP_LOGE(TAG, "event not published: device identity unavailable");
        return;
    }

    // The batch is read per event: the box may have been reloaded since boot.
    uint8_t batch_id[32];
    device_identity_batch(batch_id);

    signed_event_t signed_ev;
    event_payload_build(ev, s_box_id, batch_id, (uint64_t)to_epoch_ms(ev->start_ms),
                        (uint64_t)to_epoch_ms(ev->start_ms + ev->duration_ms), counter,
                        &signed_ev);

    uint8_t digest[32], sig[SIGNED_EVENT_SIG_LEN];
    int64_t t0 = esp_timer_get_time();
    signed_event_digest(s_domain_separator, &signed_ev, digest);
    bool signed_ok = device_identity_sign(digest, sig);
    int64_t sign_us = esp_timer_get_time() - t0;
    if (!signed_ok) {
        ESP_LOGE(TAG, "event not published: signing failed");
        return;
    }

    char payload[EVENT_PAYLOAD_MAX];
    if (event_payload_json(&signed_ev, sig, payload, sizeof(payload)) < 0) {
        ESP_LOGE(TAG, "event not published: payload too large");
        return;
    }
    ESP_LOGI(TAG, "%s %s (counter %lu), hash+sign took %lld us",
             ev->type == FSM_EVT_ANOMALY ? "ANOMALY" : "RECOVERY",
             anomaly_fsm_anomaly_name(ev->anomaly), (unsigned long)counter, sign_us);
    ESP_LOGW(TAG, "coldchain/%s/anomaly %s", CONFIG_CRYO_BOX_ID, payload);
}

static void init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

static void init_signing(void)
{
    signed_event_id(CONFIG_CRYO_BOX_ID, s_box_id);
    uint8_t initial_batch_id[32];
    signed_event_id(CONFIG_CRYO_BATCH_ID, initial_batch_id);

    uint8_t contract[20] = { 0 };
    if (!event_payload_parse_address(CONFIG_CRYO_CONTRACT_ADDRESS, contract)) {
        ESP_LOGW(TAG, "contract address not set (menuconfig > CryoTrace - Blockchain); "
                      "events will be signed for address 0x0 and rejected on-chain");
    }
    signed_event_domain_separator(CONFIG_CRYO_CHAIN_ID, contract, s_domain_separator);

    esp_err_t err = device_identity_init(initial_batch_id);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "device identity failed: %s", esp_err_to_name(err));
        return;
    }
    s_identity_ready = true;
}

static void monitor_task(void *arg)
{
    filter_avg_t filter;
    filter_init(&filter, CONFIG_CRYO_FILTER_WINDOW);

    fsm_t fsm;
    const fsm_config_t cfg = {
        .temp_min = CONFIG_CRYO_TEMP_MIN_DECI / 10.0f,
        .temp_max = CONFIG_CRYO_TEMP_MAX_DECI / 10.0f,
        .enter_time_ms = CONFIG_CRYO_ENTER_TIME_MS,
        .exit_time_ms = CONFIG_CRYO_EXIT_TIME_MS,
    };
    anomaly_fsm_init(&fsm, &cfg);

    ESP_LOGI(TAG, "range [%.1f, %.1f] °C, enter %lu ms, exit %lu ms, sample every %d ms",
             cfg.temp_min, cfg.temp_max, (unsigned long)cfg.enter_time_ms,
             (unsigned long)cfg.exit_time_ms, CONFIG_CRYO_SAMPLE_PERIOD_MS);

    const int samples_per_telemetry =
        CONFIG_CRYO_TELEMETRY_PERIOD_S * 1000 / CONFIG_CRYO_SAMPLE_PERIOD_MS;
    int counter = 0;
    TickType_t next_wake = xTaskGetTickCount();

    for (;;) {
        float raw = sensor_read();
        float filtered = NAN;
        if (isnan(raw)) {
            // Don't mix readings from before and after a failure.
            filter_reset(&filter);
        } else {
            filtered = filter_add(&filter, raw);
        }
        ESP_LOGD(TAG, "raw=%.2f filtered=%.2f", raw, filtered);

        fsm_event_t ev;
        if (anomaly_fsm_process(&fsm, filtered, now_ms(), &ev)) {
            publish_event(&ev);
        }

        if (++counter >= samples_per_telemetry) {
            counter = 0;
            publish_telemetry(raw, filtered, fsm.state);
        }

        vTaskDelayUntil(&next_wake, pdMS_TO_TICKS(CONFIG_CRYO_SAMPLE_PERIOD_MS));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "CryoTrace: box %s, batch %s", CONFIG_CRYO_BOX_ID, CONFIG_CRYO_BATCH_ID);

    init_nvs();
    // Before Wi-Fi: a first boot generates the device key from the hardware RNG.
    init_signing();
    sensor_init();
    // Monitoring does not depend on the network: it runs even without Wi-Fi/NTP.
    network_start();

    // Signing runs in this task for now; it moves to a publisher task with MQTT.
    xTaskCreate(monitor_task, "monitor", 8192, NULL, 5, NULL);
}
