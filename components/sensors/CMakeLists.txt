idf_component_register(
    SRCS "ads111x.c" "zmpt101b.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_timer
)