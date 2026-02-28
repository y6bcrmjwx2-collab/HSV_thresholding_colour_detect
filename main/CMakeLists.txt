idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_psram
                    EMBED_FILES "lutino_brightlight_rgb565.raw")
