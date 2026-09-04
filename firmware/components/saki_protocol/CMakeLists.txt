idf_component_register(
    SRCS "saki_protocol.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_timer json saki_model
)
