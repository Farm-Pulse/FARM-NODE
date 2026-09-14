#ifndef DHT_TEMP_H
#define DHT_TEMP_H

#include "esp_err.h"

// Initialize the GPIO for the DHT22 sensor
void dht22_init(void);

// Read Temperature (Celsius) and Humidity (%)
esp_err_t dht22_read(float *temperature, float *humidity);

#endif // DHT_TEMP_H