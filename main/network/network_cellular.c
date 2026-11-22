// Cellular link: SIMCom SIM7080G (NB-IoT / LTE-M) on a UART, driven by
// esp_modem as a PPP network interface, so MQTT, TLS and SNTP run on top of it
// unchanged. Builds, but not yet tested on hardware (no modem available).

#include "network_link.h"

#include <string.h>

#include "driver/gpio.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_modem_api.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#define CONNECTED_BIT BIT0

// NB-IoT attach can take minutes on first registration.
#define ATTACH_TIMEOUT_MS (3 * 60 * 1000)
#define IP_TIMEOUT_MS 30000
#define POLL_MS 2000
#define SYNC_ATTEMPTS 10

// AT+CNMP=38: LTE only (no GSM fallback); LTE-M and NB-IoT are LTE.
#define NETWORK_MODE_LTE_ONLY 38

// AT+CMNB on SIMCom modules: 1 = Cat-M (LTE-M), 2 = NB-IoT, 3 = both.
#if CONFIG_CRYO_CELL_RAT_NBIOT
#define PREFERRED_MODE 2
#define RAT_NAME "NB-IoT"
#elif CONFIG_CRYO_CELL_RAT_LTEM
#define PREFERRED_MODE 1
#define RAT_NAME "LTE-M"
#else
#define PREFERRED_MODE 3
#define RAT_NAME "LTE-M or NB-IoT"
#endif

static const char *TAG = "cellular";

static EventGroupHandle_t s_events;
static esp_modem_dce_t *s_dce;

static void on_ip_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (id == IP_EVENT_PPP_GOT_IP) {
        ip_event_got_ip_t *ev = data;
        ESP_LOGI(TAG, "got IP: " IPSTR, IP2STR(&ev->ip_info.ip));
        xEventGroupSetBits(s_events, CONNECTED_BIT);
    } else if (id == IP_EVENT_PPP_LOST_IP) {
        ESP_LOGW(TAG, "lost IP");
        xEventGroupClearBits(s_events, CONNECTED_BIT);
    }
}

static bool modem_responds(void)
{
    for (int i = 0; i < SYNC_ATTEMPTS; i++) {
        if (esp_modem_sync(s_dce) == ESP_OK) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    return false;
}

// The same PWRKEY pulse turns the SIM7080G on or off, so it is only sent
// when the modem does not answer. Polarity depends on the board wiring.
static void pulse_power_key(void)
{
#if CONFIG_CRYO_CELL_PWRKEY_GPIO >= 0
#if CONFIG_CRYO_CELL_PWRKEY_ACTIVE_HIGH
    const int active = 1;
#else
    const int active = 0;
#endif
    gpio_reset_pin(CONFIG_CRYO_CELL_PWRKEY_GPIO);
    gpio_set_direction(CONFIG_CRYO_CELL_PWRKEY_GPIO, GPIO_MODE_OUTPUT);
    ESP_LOGI(TAG, "pulsing PWRKEY (GPIO %d)", CONFIG_CRYO_CELL_PWRKEY_GPIO);
    gpio_set_level(CONFIG_CRYO_CELL_PWRKEY_GPIO, active);
    vTaskDelay(pdMS_TO_TICKS(1100));
    gpio_set_level(CONFIG_CRYO_CELL_PWRKEY_GPIO, !active);
    vTaskDelay(pdMS_TO_TICKS(5000)); // boot time before AT commands
#endif
}

static esp_err_t unlock_sim(void)
{
    bool pin_ok = false;
    if (esp_modem_read_pin(s_dce, &pin_ok) != ESP_OK) {
        ESP_LOGE(TAG, "cannot read SIM state (SIM inserted?)");
        return ESP_ERR_NOT_FOUND;
    }
    if (pin_ok) {
        return ESP_OK;
    }
    if (strlen(CONFIG_CRYO_CELL_SIM_PIN) == 0 ||
        esp_modem_set_pin(s_dce, CONFIG_CRYO_CELL_SIM_PIN) != ESP_OK) {
        ESP_LOGE(TAG, "SIM is locked and the PIN is missing or wrong");
        return ESP_ERR_INVALID_STATE;
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
    return ESP_OK;
}

static esp_err_t wait_for_attach(void)
{
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(ATTACH_TIMEOUT_MS);
    while (xTaskGetTickCount() < deadline) {
        int rssi = 99, ber = 99, attached = 0;
        esp_modem_get_signal_quality(s_dce, &rssi, &ber);
        esp_modem_get_network_attachment_state(s_dce, &attached);
        ESP_LOGI(TAG, "signal rssi=%d (0-31, 99 = unknown), attached=%d", rssi, attached);
        if (attached) {
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
    return ESP_ERR_TIMEOUT;
}

const char *network_link_name(void)
{
    return "cellular (" RAT_NAME ")";
}

esp_err_t network_link_start(void)
{
    s_events = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, on_ip_event, NULL));

    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_PPP();
    esp_netif_t *netif = esp_netif_new(&netif_cfg);

    esp_modem_dte_config_t dte_cfg = ESP_MODEM_DTE_DEFAULT_CONFIG();
    dte_cfg.uart_config.tx_io_num = CONFIG_CRYO_CELL_UART_TX;
    dte_cfg.uart_config.rx_io_num = CONFIG_CRYO_CELL_UART_RX;
    dte_cfg.uart_config.rts_io_num = UART_PIN_NO_CHANGE; // no hardware flow control
    dte_cfg.uart_config.cts_io_num = UART_PIN_NO_CHANGE;
    dte_cfg.uart_config.baud_rate = CONFIG_CRYO_CELL_UART_BAUD;
    esp_modem_dce_config_t dce_cfg = ESP_MODEM_DCE_DEFAULT_CONFIG(CONFIG_CRYO_CELL_APN);

    s_dce = esp_modem_new_dev(ESP_MODEM_DCE_SIM7070, &dte_cfg, &dce_cfg, netif);
    if (s_dce == NULL) {
        ESP_LOGE(TAG, "cannot create the modem device");
        return ESP_FAIL;
    }

    if (!modem_responds()) {
        pulse_power_key();
        if (!modem_responds()) {
            ESP_LOGE(TAG, "modem does not answer on UART TX=%d RX=%d",
                     CONFIG_CRYO_CELL_UART_TX, CONFIG_CRYO_CELL_UART_RX);
            return ESP_ERR_NOT_FOUND;
        }
    }

    esp_err_t err = unlock_sim();
    if (err != ESP_OK) {
        return err;
    }

    if (esp_modem_set_network_mode(s_dce, NETWORK_MODE_LTE_ONLY) != ESP_OK ||
        esp_modem_set_preferred_mode(s_dce, PREFERRED_MODE) != ESP_OK) {
        ESP_LOGW(TAG, "could not select %s; keeping the modem's setting", RAT_NAME);
    }
    ESP_LOGI(TAG, "registering on %s, APN \"%s\"...", RAT_NAME, CONFIG_CRYO_CELL_APN);

    err = wait_for_attach();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "not attached after %d s", ATTACH_TIMEOUT_MS / 1000);
        return err;
    }

    if (esp_modem_set_mode(s_dce, ESP_MODEM_MODE_DATA) != ESP_OK) {
        ESP_LOGE(TAG, "cannot switch the modem to data (PPP) mode");
        return ESP_FAIL;
    }
    EventBits_t bits = xEventGroupWaitBits(s_events, CONNECTED_BIT, pdFALSE, pdTRUE,
                                           pdMS_TO_TICKS(IP_TIMEOUT_MS));
    return (bits & CONNECTED_BIT) ? ESP_OK : ESP_ERR_TIMEOUT;
}
