idf_component_register(SRCS "main.c"
                            "filter.c"
                            "fsm.c"
                            "sensor.c"
                            "sensor_sim.c"
                            "network.c"
                            "signed_event.c"
                            "event_payload.c"
                            "device_identity.c"
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES esp_wifi esp_netif esp_event esp_timer esp_hw_support
                                  bootloader_support nvs_flash trezor_crypto)
