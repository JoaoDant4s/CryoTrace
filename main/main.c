#include <math.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_system.h"
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
#include "publisher.h"
#include "sensor.h"
#include "signed_event.h"

static const char *TAG = "cryotrace";

static uint8_t s_box_id[32];
static uint8_t s_domain_separator[32];
static bool s_identity_ready;

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

// Resource usage, for the edge metrics (free heap, stack margins).
static void log_resources(void)
{
    ESP_LOGI(TAG, "heap free %lu B (min %lu B), stack free: monitor %lu B, publisher %lu B",
             (unsigned long)esp_get_free_heap_size(), (unsigned long)esp_get_minimum_free_heap_size(),
             (unsigned long)uxTaskGetStackHighWaterMark(NULL), (unsigned long)publisher_stack_free());
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
    signed_event_id(CONFIG_CRYO_BOX_NAME, s_box_id);
    uint8_t initial_batch_id[32];
    signed_event_id(CONFIG_CRYO_BATCH_NAME, initial_batch_id);

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

// Reading filter chosen in menuconfig (median by default, see analysis/).
#if CONFIG_CRYO_FILTER_MEDIAN
static filter_median_t s_filter;
static void reading_filter_init(void) { filter_median_init(&s_filter, CONFIG_CRYO_FILTER_WINDOW); }
static void reading_filter_reset(void) { filter_median_reset(&s_filter); }
static float reading_filter_add(float x) { return filter_median_add(&s_filter, x); }
#define FILTER_NAME "median"
#else
static filter_avg_t s_filter;
static void reading_filter_init(void) { filter_init(&s_filter, CONFIG_CRYO_FILTER_WINDOW); }
static void reading_filter_reset(void) { filter_reset(&s_filter); }
static float reading_filter_add(float x) { return filter_add(&s_filter, x); }
#define FILTER_NAME "moving average"
#endif

static void monitor_task(void *arg)
{
    reading_filter_init();

    fsm_t fsm;
    const fsm_config_t cfg = {
        .temp_min = CONFIG_CRYO_TEMP_MIN_DECI / 10.0f,
        .temp_max = CONFIG_CRYO_TEMP_MAX_DECI / 10.0f,
        .enter_time_ms = CONFIG_CRYO_ENTER_TIME_MS,
        .exit_time_ms = CONFIG_CRYO_EXIT_TIME_MS,
    };
    anomaly_fsm_init(&fsm, &cfg);

    ESP_LOGI(TAG, "range [%.1f, %.1f] °C, enter %lu ms, exit %lu ms, sample every %d ms, %s filter N=%d",
             cfg.temp_min, cfg.temp_max, (unsigned long)cfg.enter_time_ms,
             (unsigned long)cfg.exit_time_ms, CONFIG_CRYO_SAMPLE_PERIOD_MS, FILTER_NAME,
             CONFIG_CRYO_FILTER_WINDOW);

    const int samples_per_minute = 60000 / CONFIG_CRYO_SAMPLE_PERIOD_MS;
    int resource_count = 0;
    TickType_t next_wake = xTaskGetTickCount();

    for (;;) {
        float raw = sensor_read();
        float filtered = NAN;
        if (isnan(raw)) {
            // Don't mix readings from before and after a failure.
            reading_filter_reset();
        } else {
            filtered = reading_filter_add(raw);
        }
        ESP_LOGD(TAG, "raw=%.2f filtered=%.2f", raw, filtered);

        fsm_event_t ev;
        if (anomaly_fsm_process(&fsm, filtered, now_ms(), &ev)) {
            ESP_LOGW(TAG, "%s %s", ev.type == FSM_EVT_ANOMALY ? "ANOMALY" : "RECOVERY",
                     anomaly_fsm_anomaly_name(ev.anomaly));
            // Signing and delivery happen in the publisher task.
            publisher_submit(&ev);
        }

        if (++resource_count >= samples_per_minute) {
            resource_count = 0;
            log_resources();
        }

        vTaskDelayUntil(&next_wake, pdMS_TO_TICKS(CONFIG_CRYO_SAMPLE_PERIOD_MS));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "CryoTrace: box %s, batch %s", CONFIG_CRYO_BOX_NAME, CONFIG_CRYO_BATCH_NAME);

    init_nvs();
    // Before Wi-Fi: a first boot generates the device key from the hardware RNG.
    init_signing();
    sensor_init();
    // Monitoring does not depend on the network: it runs even without a link or
    // NTP, and the publisher keeps the signed events until the broker has them.
    network_start();
    if (s_identity_ready) {
        esp_err_t err = publisher_start(s_box_id, s_domain_separator);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "publisher not started: %s", esp_err_to_name(err));
        }
    } else {
        ESP_LOGE(TAG, "no device identity: events will not be signed or published");
    }

    xTaskCreate(monitor_task, "monitor", 4096, NULL, 5, NULL);
}
