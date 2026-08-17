idf_component_register(SRCS "st7735.c" "main.c" "lsm6ds3.c"
                        INCLUDE_DIRS "."
                        REQUIRES driver esp_driver_gpio esp_driver_spi)
