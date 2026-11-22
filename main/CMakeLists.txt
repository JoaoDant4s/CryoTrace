set(srcs "main.c"
         "sensor/sensor.c"
         "sensor/sensor_sim.c"
         "monitoring/filter.c"
         "monitoring/fsm.c"
         "signing/signed_event.c"
         "signing/event_payload.c"
         "signing/device_identity.c"
         "network/network.c")

# Only the network link chosen in menuconfig is built.
if(CONFIG_CRYO_NET_CELLULAR)
    list(APPEND srcs "network/network_cellular.c")
else()
    list(APPEND srcs "network/network_wifi.c")
endif()

idf_component_register(SRCS ${srcs}
                    INCLUDE_DIRS "sensor" "monitoring" "signing" "network"
                    PRIV_REQUIRES esp_wifi esp_netif esp_event esp_timer esp_hw_support
                                  esp_driver_gpio bootloader_support nvs_flash trezor_crypto)
