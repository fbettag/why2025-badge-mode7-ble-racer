idf_component_register(
    SRCS "display.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_lcd
    PRIV_REQUIRES spi_flash
)