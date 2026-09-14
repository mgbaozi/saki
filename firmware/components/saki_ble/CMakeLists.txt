idf_component_register(
    SRCS "saki_ble.c"
    INCLUDE_DIRS "include"
    REQUIRES bt esp_timer freertos saki_buttons
)
