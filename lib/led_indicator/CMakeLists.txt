idf_component_register(
    SRCS "led_indicator.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_driver_rmt
)
