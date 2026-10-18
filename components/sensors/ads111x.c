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
#include "esp_rom_sys.h"
#include "ads111x.h"

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
    buffer[1] = (value >> 8) & 0xFF; 
    buffer[2] = value & 0xFF;        
    
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
    uint16_t config = 0x0000;
    
    config |= (1 << 15);                // OS: Trigger single-conversion
    config |= (mux << 12);              // MUX: Set channel
    config |= (pga << 9);               // PGA: Set gain
    config |= (1 << 8);                 // MODE: Single-Shot
    config |= (ADS1115_DR_860SPS << 5); // DR: 860 SPS (High-speed for AC sampling)
    config |= 0x0003;                   // COMP: Disable comparator mode

    esp_err_t err = i2c_write_register(dev, REG_POINTER_CONFIG, config);
    if (err != ESP_OK) return err;

    // High-Speed Hardware Polling (Non-RTOS Blocking)
    // At 860 SPS, conversion takes ~1.2ms. We poll the OS bit every 100us.
    uint16_t current_config = 0;
    for (int i = 0; i < 20; i++) {
        esp_rom_delay_us(100); // Bare-metal microsecond delay
        
        i2c_read_register(dev, REG_POINTER_CONFIG, &current_config);
        if (current_config & (1 << 15)) {
            break; // OS bit flipped to 1, conversion complete
        }
    }

    uint16_t raw_adc = 0;
    err = i2c_read_register(dev, REG_POINTER_CONVERSION, &raw_adc);
    if (err != ESP_OK) return err;

    int16_t signed_adc = (int16_t)raw_adc;
    *voltage_out = signed_adc * PGA_VOLTAGE_MAPPING[pga];

    return ESP_OK;
}