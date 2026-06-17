idf_component_register(
    SRCS
        "main.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        lora nvs_flash driver esp_timer esp_app_format sensors
) 
