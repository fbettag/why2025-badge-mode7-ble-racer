idf_component_register(SRCS "main.c" "game_loop.c" "app.c"
                    INCLUDE_DIRS "."
                    REQUIRES display input game utils
                    PRIV_REQUIRES esp_timer)