/**
 * @file ads111x.h
 * @brief ADS111x extrnal ADC driver code.
 * @author Shahid  
 * @date April 2026
 */

#ifndef ADS1115_H
#define ADS1115_H

#include <stdint.h>
#include "esp_err.h"

// Default I2C address when ADDR pin is tied to GND
#define ADS1115_ADDRESS_GND    0x48 

// Multiplexer (MUX) Configuration (Bits 14-12)
typedef enum {
    ADS1115_MUX_DIFF_0_1 = 0x00, // Differential P=AIN0, N=AIN1 (Default)
    ADS1115_MUX_DIFF_0_3 = 0x01, // Differential P=AIN0, N=AIN3
    ADS1115_MUX_DIFF_1_3 = 0x02, // Differential P=AIN1, N=AIN3
    ADS1115_MUX_DIFF_2_3 = 0x03, // Differential P=AIN2, N=AIN3
    ADS1115_MUX_SINGLE_0 = 0x04, // Single-ended AIN0
    ADS1115_MUX_SINGLE_1 = 0x05, // Single-ended AIN1
    ADS1115_MUX_SINGLE_2 = 0x06, // Single-ended AIN2
    ADS1115_MUX_SINGLE_3 = 0x07  // Single-ended AIN3
} ads1115_mux_t;

// Programmable Gain Amplifier (PGA) Configuration (Bits 11-9)
typedef enum {
    ADS1115_PGA_6_144V   = 0x00, // +/- 6.144V range
    ADS1115_PGA_4_096V   = 0x01, // +/- 4.096V range
    ADS1115_PGA_2_048V   = 0x02, // +/- 2.048V range (Default)
    ADS1115_PGA_1_024V   = 0x03, // +/- 1.024V range
    ADS1115_PGA_0_512V   = 0x04, // +/- 0.512V range
    ADS1115_PGA_0_256V   = 0x05  // +/- 0.256V range
} ads1115_pga_t;

// Data Rate (DR) Configuration (Bits 7-5)
typedef enum {
    ADS1115_DR_8SPS      = 0x00,
    ADS1115_DR_16SPS     = 0x01,
    ADS1115_DR_32SPS     = 0x02,
    ADS1115_DR_64SPS     = 0x03,
    ADS1115_DR_128SPS    = 0x04, // Default
    ADS1115_DR_250SPS    = 0x05,
    ADS1115_DR_475SPS    = 0x06,
    ADS1115_DR_860SPS    = 0x07
} ads1115_dr_t;

// Context structure to hold device config
typedef struct {
    uint8_t i2c_port;     // Usually I2C_NUM_0 or I2C_NUM_1
    uint8_t i2c_addr;     // Usually 0x48
} ads1115_t;

// --- API Functions ---

/**
 * @brief Initialize the ADS1115 device handle
 */
esp_err_t ads1115_init(ads1115_t *dev, uint8_t i2c_port, uint8_t i2c_addr);

/**
 * @brief Performs a single-shot read on the requested MUX channel
 * @param voltage_out Will contain the calculated real-world voltage
 */
esp_err_t ads1115_read_single_shot(ads1115_t *dev, ads1115_mux_t mux, ads1115_pga_t pga, float *voltage_out);

#endif // ADS1115_H