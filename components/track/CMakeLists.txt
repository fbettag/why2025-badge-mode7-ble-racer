idf_component_register(
    SRCS "track_loader.c" "track_format.c" "track_cache.c"
    INCLUDE_DIRS "include"
    REQUIRES game utils display
)