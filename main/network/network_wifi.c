// Wi-Fi station link (prototype: e.g. a phone hotspot standing in for the
// cellular modem).

#include "network_link.h"

#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "sdkconfig.h"

#define CONNECTED_BIT BIT0
#define CONNECT_TIMEOUT_MS 20000

static const char *TAG = "wifi";

static EventGroupHandle_t s_events;

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_events, CONNECTED_BIT);
        ESP_LOGW(TAG, "disconnected, retrying");
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = data;
        ESP_LOGI(TAG, "got IP: " IPSTR, IP2STR(&ev->ip_info.ip));
        xEventGroupSetBits(s_events, CONNECTED_BIT);
    }
}

const char *network_link_name(void)
{
    return "Wi-Fi";
}

esp_err_t network_link_start(void)
{
    if (strlen(CONFIG_CRYO_WIFI_SSID) == 0) {
        ESP_LOGW(TAG, "SSID not set (menuconfig > CryoTrace - Network)");
        return ESP_ERR_INVALID_STATE;
    }

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
                                           pdMS_TO_TICKS(CONNECT_TIMEOUT_MS));
    // On timeout Wi-Fi keeps retrying in the background (see on_event).
    return (bits & CONNECTED_BIT) ? ESP_OK : ESP_ERR_TIMEOUT;
}
