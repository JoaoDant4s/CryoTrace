#include "sensor.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#include "sensor_sim.h"

static const char *TAG = "sensor";

void sensor_init(void)
{
#if CONFIG_CRYO_SENSOR_SIMULATED
    ESP_LOGW(TAG, "using SIMULATED sensor (%d s cycle)", SENSOR_SIM_CYCLE_MS / 1000);
#endif
}

float sensor_read(void)
{
#if CONFIG_CRYO_SENSOR_SIMULATED
    return sensor_sim_temperature(esp_timer_get_time() / 1000);
#endif
}
