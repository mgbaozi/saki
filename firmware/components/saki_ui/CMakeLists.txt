idf_component_register(
    SRCS "saki_ui.c" "fonts/saki_font_cjk_16.c"
    INCLUDE_DIRS "include" "fonts"
    REQUIRES esp_timer lvgl saki_model saki_ui_policy vendor_box3_bsp
)
