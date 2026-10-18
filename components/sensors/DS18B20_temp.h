#ifndef DS18B20_TEMP_H
#define DS18B20_TEMP_H

#include <stdint.h>
#include "esp_err.h"

// Initialize the 1-Wire GPIO pin for the DS18B20 soil temperature probe
void ds18b20_init(void);

// Adheres to the industrial standard naming convention for environmental polling[cite: 7]
esp_err_t fnRead_Soil_Temperature(float *temperature);

#endif // DS18B20_TEMP_H