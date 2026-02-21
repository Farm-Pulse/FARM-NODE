#ifndef ZMPT101B_H
#define ZMPT101B_H

#include <stdint.h>
#include "esp_err.h"

// Initialize the ADC for the 3 ZMPT101B sensors
esp_err_t zmpt_init(void);

// Read the True RMS voltage of all 3 phases
// Returns the voltage in Volts (e.g., 230.5)
esp_err_t zmpt_read_all(float *v_r, float *v_y, float *v_b);

#endif // ZMPT101B_H