idf_component_register(
    SRCS "ads111x.c" "zmpt101b.c" "soil_sensor.c" "DHT_temp.c" "DS18B20_temp.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_timer
)