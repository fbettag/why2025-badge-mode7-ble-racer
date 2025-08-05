idf_component_register(
    SRCS "math.c" "physics.c"
    INCLUDE_DIRS "."
    REQUIRES display utils
    PRIV_REQUIRES driver esp_lcd
)