idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES esp_hw_support esp_timer freertos heap nvs_flash saki_model saki_protocol saki_ui saki_usb
)

if(SAKI_BUILD_PROFILE STREQUAL "release" AND CONFIG_SAKI_UI_DEMO)
    message(FATAL_ERROR "CONFIG_SAKI_UI_DEMO is forbidden in a release build")
endif()

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    SAKI_FIRMWARE_VERSION=\"${SAKI_VERSION}\"
)
