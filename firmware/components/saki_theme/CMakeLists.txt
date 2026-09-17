idf_component_register(
    SRCS "saki_theme.c" "themes/saki_stage/generated/saki_stage_assets.c"
    INCLUDE_DIRS "include" "themes/saki_stage/generated"
    REQUIRES lvgl nvs_flash saki_model
)
