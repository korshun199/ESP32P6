idf_component_register(
    SRCS "main.c" "display_st7701.c"
    INCLUDE_DIRS "."
    REQUIRES esp_lcd esp_driver_gpio esp_hw_support
)
