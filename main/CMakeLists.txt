idf_component_register(SRCS "main.c" "lsm6ds3.c"
                        INCLUDE_DIRS "."
                        REQUIRES driver esp_driver_gpio)
