idf_component_register(SRCS "main.c"
                            "filter.c"
                            "fsm.c"
                            "sensor.c"
                            "sensor_sim.c"
                            "network.c"
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES esp_wifi esp_netif esp_event esp_timer nvs_flash)
