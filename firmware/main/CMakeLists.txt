idf_component_register(SRCS "main.c" "invoke_ble.c" "invoke_round.c" "screens.c"
                            "st7735.c" "orientation.c" "lsm6ds3.c"
                        INCLUDE_DIRS "."
                        REQUIRES driver esp_driver_gpio esp_driver_spi esp_timer bt nvs_flash cjson)
