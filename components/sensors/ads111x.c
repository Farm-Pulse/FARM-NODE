/**
 * @file ads111x.c
 * @brief ADS111x extrnal ADC driver code.
 * @author Shahid  
 * @date April 2026
 */

#include <stdio.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"
#include "ads1115.h"

static const char *TAG = "ADS1115";

// Internal Register Addresses
#define REG_POINTER_CONVERSION 0x00
#define REG_POINTER_CONFIG     0x01

// Maps PGA enum to actual Voltage Multipliers (LSB Resolution)
static const float PGA_VOLTAGE_MAPPING[] = {
    6.144f / 32768.0f, // 0: +/- 6.144V -> 187.5 uV/bit
    4.096f / 32768.0f, // 1: +/- 4.096V -> 125 uV/bit
    2.048f / 32768.0f, // 2: +/- 2.048V -> 62.5 uV/bit
    1.024f / 32768.0f, // 3: +/- 1.024V -> 31.25 uV/bit
    0.512f / 32768.0f, // 4: +/- 0.512V -> 15.625 uV/bit
    0.256f / 32768.0f  // 5: +/- 0.256V -> 7.8125 uV/bit
};

// --- Low Level I2C Helpers ---

static esp_err_t i2c_write_register(ads1115_t *dev, uint8_t reg, uint16_t value) {
    uint8_t buffer[3];
    buffer[0] = reg;
    buffer[1] = (value >> 8) & 0xFF; // MSB
    buffer[2] = value & 0xFF;        // LSB
    
    return i2c_master_write_to_device(dev->i2c_port, dev->i2c_addr, buffer, 3, pdMS_TO_TICKS(100));
}

static esp_err_t i2c_read_register(ads1115_t *dev, uint8_t reg, uint16_t *value) {
    uint8_t buffer[2] = {0};
    
    esp_err_t err = i2c_master_write_read_device(dev->i2c_port, dev->i2c_addr, &reg, 1, buffer, 2, pdMS_TO_TICKS(100));
    if (err == ESP_OK) {
        *value = (buffer[0] << 8) | buffer[1];
    }
    return err;
}

// --- High Level API ---

esp_err_t ads1115_init(ads1115_t *dev, uint8_t i2c_port, uint8_t i2c_addr) {
    dev->i2c_port = i2c_port;
    dev->i2c_addr = i2c_addr;
    
    // Quick ping to check if the device responds
    uint16_t dummy;
    esp_err_t err = i2c_read_register(dev, REG_POINTER_CONFIG, &dummy);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize ADS1115 at address 0x%02X", i2c_addr);
        return err;
    }
    
    ESP_LOGI(TAG, "ADS1115 successfully found at 0x%02X", i2c_addr);
    return ESP_OK;
}

esp_err_t ads1115_read_single_shot(ads1115_t *dev, ads1115_mux_t mux, ads1115_pga_t pga, float *voltage_out) {
    // 1. Construct the Configuration Register
    uint16_t config = 0x0000;
    
    config |= (1 << 15);            // OS: Set to 1 to start a single-conversion
    config |= (mux << 12);          // MUX: Set the channel(s)
    config |= (pga << 9);           // PGA: Set the gain
    config |= (1 << 8);             // MODE: Set to 1 for Single-Shot (Power down mode)
    config |= (ADS1115_DR_128SPS << 5); // DR: 128 samples per second
    config |= 0x0003;               // COMP: Disable comparator mode (Bits 4-0 = 11)

    // 2. Write Config to trigger conversion
    esp_err_t err = i2c_write_register(dev, REG_POINTER_CONFIG, config);
    if (err != ESP_OK) return err;

    // 3. Wait for conversion to complete
    // At 128 SPS, 1 conversion takes ~7.8ms. We wait 10ms to be safe.
    vTaskDelay(pdMS_TO_TICKS(10));

    // 4. Poll the OS bit to guarantee completion (Optional but robust)
    uint16_t current_config = 0;
    for (int i = 0; i < 5; i++) {
        i2c_read_register(dev, REG_POINTER_CONFIG, &current_config);
        if (current_config & (1 << 15)) {
            break; // OS bit is 1, conversion is done!
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }

    // 5. Read the Conversion Register
    uint16_t raw_adc = 0;
    err = i2c_read_register(dev, REG_POINTER_CONVERSION, &raw_adc);
    if (err != ESP_OK) return err;

    // 6. Math: Convert 16-bit 2's complement to Voltage
    // The ADS1115 outputs a signed 16-bit integer (-32768 to +32767)
    int16_t signed_adc = (int16_t)raw_adc;
    
    // Multiply by the specific LSB resolution of the chosen PGA setting
    *voltage_out = signed_adc * PGA_VOLTAGE_MAPPING[pga];

    return ESP_OK;
}