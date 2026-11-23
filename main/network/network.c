#include "network.h"

#include <sys/time.h>
#include <time.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#include "network_link.h"

#define NTP_TIMEOUT_MS 15000

// Any time before this means the clock is not set yet (no RTC, so the
// ESP32 starts from 1970 on every boot).
#define MIN_EPOCH 1767225600 // 2026-01-01T00:00:00Z

static const char *TAG = "network";

static esp_err_t start_sntp(void)
{
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_CRYO_NTP_SERVER);
    ESP_ERROR_CHECK(esp_netif_sntp_init(&cfg));

    ESP_LOGI(TAG, "syncing time with %s...", CONFIG_CRYO_NTP_SERVER);
    esp_err_t err = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(NTP_TIMEOUT_MS));
    if (err == ESP_OK) {
        time_t now = time(NULL);
        struct tm utc;
        char text[32];
        gmtime_r(&now, &utc);
        strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%SZ", &utc);
        ESP_LOGI(TAG, "time synced: %s", text);
    }
    return err;
}

esp_err_t network_start(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_LOGI(TAG, "network link: %s", network_link_name());
    esp_err_t err = network_link_start();
    if (err == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "link not configured (menuconfig > CryoTrace - Network); running without network");
        return err;
    }
    if (err != ESP_OK) {
        // SNTP is started anyway and syncs on its own if the link comes up.
        ESP_LOGW(TAG, "no IP yet: %s", esp_err_to_name(err));
    }

    esp_err_t ntp_err = start_sntp();
    if (ntp_err != ESP_OK) {
        ESP_LOGW(TAG, "time not synced yet; events will carry ts=0");
    }
    return err != ESP_OK ? err : ntp_err;
}

bool network_time_synced(void)
{
    return time(NULL) >= MIN_EPOCH;
}

int64_t network_epoch_ms(int64_t mono_ms)
{
    if (!network_time_synced()) {
        return 0;
    }
    struct timeval tv;
    gettimeofday(&tv, NULL);
    int64_t epoch_now = (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
    return epoch_now - (esp_timer_get_time() / 1000 - mono_ms);
}
