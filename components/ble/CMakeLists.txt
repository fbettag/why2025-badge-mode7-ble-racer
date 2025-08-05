idf_component_register(
    SRCS "ble.c" "gatt.c" "protocol.c" "lobby.c"
    INCLUDE_DIRS "include"
    REQUIRES bt game utils
)