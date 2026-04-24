idf_component_register(
    SRCS "ads111x.c" "zmpt101b.c"
    INCLUDE_DIRS "."
    REQUIRES esp_adc driver esp_timer
)