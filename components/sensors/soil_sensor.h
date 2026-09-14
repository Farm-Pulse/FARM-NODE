#ifndef SOIL_SENSOR_H
#define SOIL_SENSOR_H

#include <stdint.h>
#include "esp_err.h"

/**
 * @brief Reads the soil moisture sensor on ADS1115 AIN3.
 * @param voltage Returns the raw analog voltage (e.g., 2.15V).
 * @param percentage Returns the mapped moisture level (0-100%).
 */
esp_err_t soil_sensor_read(float *voltage, uint8_t *percentage);

#endif // SOIL_SENSOR_H