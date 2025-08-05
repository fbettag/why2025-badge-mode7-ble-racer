idf_component_register(
    SRCS "input.c" "keyboard.c" "imu.c"
    INCLUDE_DIRS "include"
    REQUIRES driver
    PRIV_REQUIRES esp_timer
)