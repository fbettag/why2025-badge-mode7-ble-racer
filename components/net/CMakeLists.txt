idf_component_register(
    SRCS "ble_service.c" "net_protocol.c"
    INCLUDE_DIRS "include"
    REQUIRES bt
    PRIV_REQUIRES nvs_flash
)