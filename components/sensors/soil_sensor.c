#include "soil_sensor.h"
#include "ads111x.h"
#include "esp_log.h"

static const char *TAG = "SOIL_SENSOR";

// Import the ADS1115 hardware handle initialized in zmpt101b.c
extern ads1115_t ads_dev;

// --- Calibration Thresholds ---
// Tune these voltages by measuring the sensor in dry air vs. a glass of water
#define SOIL_VOLT_DRY   2.500f 
#define SOIL_VOLT_WET   1.000f

esp_err_t soil_sensor_read(float *voltage, uint8_t *percentage) {
    if (voltage == NULL || percentage == NULL) return ESP_ERR_INVALID_ARG;

    // Read Channel 3 using the teammate's +/- 4.096V PGA setting
    esp_err_t err = ads1115_read_single_shot(&ads_dev, ADS1115_MUX_SINGLE_3, ADS1115_PGA_4_096V, voltage);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Hardware fault: Failed to read AIN3");
        *voltage = 0.0f;
        *percentage = 0;
        return err;
    }

    // Mathematical mapping: Voltage to 0-100%
    if (*voltage >= SOIL_VOLT_DRY) {
        *percentage = 0;
    } else if (*voltage <= SOIL_VOLT_WET) {
        *percentage = 100;
    } else {
        *percentage = (uint8_t)(((SOIL_VOLT_DRY - *voltage) / (SOIL_VOLT_DRY - SOIL_VOLT_WET)) * 100.0f);
    }

    ESP_LOGD(TAG, "Soil Moisture: %.3f V | %d %%", *voltage, *percentage);
    return ESP_OK;
}