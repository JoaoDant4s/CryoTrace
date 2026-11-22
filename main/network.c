#include "network.h"

#include <string.h>
#include <time.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "sdkconfig.h"

#define CONNECTED_BIT BIT0

#define WIFI_TIMEOUT_MS 20000
#define NTP_TIMEOUT_MS 15000

// Any time before this means the clock is not set yet (no RTC, so the
// ESP32 starts from 1970 on every boot).
#define MIN_EPOCH 1767225600 // 2026-01-01T00:00:00Z

static const char *TAG = "network";

static EventGroupHandle_t s_events;

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_events, CONNECTED_BIT);
        ESP_LOGW(TAG, "Wi-Fi disconnected, retrying");
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = data;
        ESP_LOGI(TAG, "got IP: " IPSTR, IP2STR(&ev->ip_info.ip));
        xEventGroupSetBits(s_events, CONNECTED_BIT);
    }
}

static esp_err_t start_wifi(void)
{
    s_events = xEventGroupCreate();

    esp_netif_create_default_wifi_sta();
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL));

    wifi_config_t cfg = { 0 };
    strlcpy((char *)cfg.sta.ssid, CONFIG_CRYO_WIFI_SSID, sizeof(cfg.sta.ssid));
    strlcpy((char *)cfg.sta.password, CONFIG_CRYO_WIFI_PASSWORD, sizeof(cfg.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "connecting to \"%s\"...", CONFIG_CRYO_WIFI_SSID);
    EventBits_t bits = xEventGroupWaitBits(s_events, CONNECTED_BIT, pdFALSE, pdTRUE,
                                           pdMS_TO_TICKS(WIFI_TIMEOUT_MS));
    return (bits & CONNECTED_BIT) ? ESP_OK : ESP_ERR_TIMEOUT;
}

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

    if (strlen(CONFIG_CRYO_WIFI_SSID) == 0) {
        ESP_LOGW(TAG, "SSID not set (menuconfig > CryoTrace - Network); running without network");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = start_wifi();
    if (err != ESP_OK) {
        // Wi-Fi keeps retrying; SNTP is started anyway and syncs on its
        // own once the connection comes up.
        ESP_LOGW(TAG, "no Wi-Fi connection after %d s", WIFI_TIMEOUT_MS / 1000);
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
