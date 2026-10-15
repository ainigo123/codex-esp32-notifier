idf_component_register(
    SRCS
        "app_main.c"
        "audio_notify.c"
    INCLUDE_DIRS "."
    REQUIRES
        driver
        esp_event
        esp_netif
        esp_wifi
        nvs_flash
)

