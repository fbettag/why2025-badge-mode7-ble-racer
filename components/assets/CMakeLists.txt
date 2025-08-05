idf_component_register(
    SRCS "asset_loader.c" "tile_converter.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES fatfs spiffs
)