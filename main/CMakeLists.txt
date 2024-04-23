idf_component_register(SRCS "ils3000_demo.c"
                    REQUIRES bt
                    REQUIRES driver
                    REQUIRES esp_adc
                    REQUIRES nvs_flash
                    INCLUDE_DIRS "")
