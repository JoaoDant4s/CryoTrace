#include <math.h>
#include <stdio.h>
#include <sys/time.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "filter.h"
#include "fsm.h"
#include "network.h"
#include "sensor.h"

static const char *TAG = "cryotrace";

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
             to_epoch_ms(now_ms()), s_filtered, s_raw, fsm_state_name(state));
    ESP_LOGI(TAG, "coldchain/%s/telemetry %s", CONFIG_CRYO_BOX_ID, payload);
}

static void publish_event(const fsm_event_t *ev)
{
    char s_temp[16], s_peak[16], payload[256];
    temp_json(s_temp, sizeof(s_temp), ev->temperature);
    temp_json(s_peak, sizeof(s_peak), ev->peak);

    if (ev->type == FSM_EVT_ANOMALY) {
        snprintf(payload, sizeof(payload),
                 "{\"event\":\"anomaly\",\"batch\":\"%s\",\"ts\":%lld,\"temp\":%s,"
                 "\"peak\":%s,\"type\":%d}",
                 CONFIG_CRYO_BATCH_ID, to_epoch_ms(ev->start_ms), s_temp, s_peak,
                 ev->anomaly);
    } else {
        snprintf(payload, sizeof(payload),
                 "{\"event\":\"recovery\",\"batch\":\"%s\",\"ts\":%lld,\"ts_end\":%lld,"
                 "\"duration_ms\":%lld,\"temp\":%s,\"peak\":%s,\"type\":%d}",
                 CONFIG_CRYO_BATCH_ID, to_epoch_ms(ev->start_ms),
                 to_epoch_ms(ev->start_ms + ev->duration_ms), ev->duration_ms, s_temp,
                 s_peak, ev->anomaly);
    }
    ESP_LOGW(TAG, "coldchain/%s/anomaly %s", CONFIG_CRYO_BOX_ID, payload);
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
    fsm_init(&fsm, &cfg);

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
        if (fsm_process(&fsm, filtered, now_ms(), &ev)) {
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

    sensor_init();
    // Monitoring does not depend on the network: it runs even without Wi-Fi/NTP.
    network_start();

    xTaskCreate(monitor_task, "monitor", 4096, NULL, 5, NULL);
}
