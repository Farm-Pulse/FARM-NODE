/**
 * @file zmpt101b.c
 * @brief ZMPT101B module 3-Phase reading
 * @author Shahid  
 * @date March 2026
 */

#include <stdio.h>
#include <math.h>
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"
#include "ads111x.h"
#include "zmpt101b.h"

static const char *TAG = "ZMPT101B";

// --- I2C Master Bus Configuration ---
#define I2C_MASTER_PORT              I2C_NUM_0
#define I2C_MASTER_SDA_IO            43         // LilyGO T3S3 SDA Pin
#define I2C_MASTER_SCL_IO            44         // LilyGO T3S3 SCL Pin
#define I2C_MASTER_FREQ_HZ           400000     // 400kHz Fast Mode
#define I2C_MASTER_TX_BUF_DISABLE    0
#define I2C_MASTER_RX_BUF_DISABLE    0

// --- ADS1115 Channel Mapping (Single-Ended) ---
#define ADS_CH_PHASE_R               ADS1115_MUX_SINGLE_0
#define ADS_CH_PHASE_Y               ADS1115_MUX_SINGLE_1
#define ADS_CH_PHASE_B               ADS1115_MUX_SINGLE_2

// --- Sampling Window & Calibration ---
#define SAMPLE_WINDOW_US             40000      // 40ms = 2 full 50Hz AC cycles
#define CALIBRATION_FACTOR           576.47f     // Voltage scaling factor

ads1115_t ads_dev;
static bool is_ads_ready = false;

/**
 * @brief Initializes the ESP-IDF I2C Master driver and pings the ADS1115.
 */
esp_err_t zmpt_init(void) {
    ESP_LOGI(TAG, "Initializing I2C Master on SDA: %d, SCL: %d...", I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO);

    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
        .clk_flags = 0
    };

    esp_err_t err = i2c_param_config(I2C_MASTER_PORT, &conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C param config failed: %s", esp_err_to_name(err));
        return err;
    }

    err = i2c_driver_install(I2C_MASTER_PORT, conf.mode, I2C_MASTER_RX_BUF_DISABLE, I2C_MASTER_TX_BUF_DISABLE, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C driver install failed: %s", esp_err_to_name(err));
        return err;
    }

    // Initialize ADS1115 on the configured I2C bus
    err = ads1115_init(&ads_dev, I2C_MASTER_PORT, ADS1115_ADDRESS_GND);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ADS1115 communication initialization failed!");
        is_ads_ready = false;
        return err;
    }

    is_ads_ready = true;
    ESP_LOGI(TAG, "ZMPT101B ADC subsystem initialized successfully.");
    return ESP_OK;
}

/**
 * @brief Computes True RMS voltage across a 40ms sampling window for a given ADC channel.
 */
static float get_rms_voltage(ads1115_mux_t channel) {
    if (!is_ads_ready) {
        return 0.0f;
    }

    double sum = 0.0;
    double sq_sum = 0.0;
    float voltage = 0.0f;
    uint32_t samples_taken = 0;

    int64_t start_time = esp_timer_get_time();

    // Sample across exactly two complete 50Hz cycles (40ms)
    while ((esp_timer_get_time() - start_time) < SAMPLE_WINDOW_US) {
        if (ads1115_read_single_shot(&ads_dev, channel, ADS1115_PGA_4_096V, &voltage) == ESP_OK) {
            sum += voltage;
            sq_sum += ((double)voltage * voltage);
            samples_taken++;
        }
    }

    if (samples_taken == 0) {
        return 0.0f;
    }

    // Variance = Mean of Squares - Square of Mean (removes sensor DC offset)
    double mean = sum / samples_taken;
    double mean_of_squares = sq_sum / samples_taken;
    double variance = mean_of_squares - (mean * mean);

    if (variance < 0.0) {
        variance = 0.0;
    }

    float rms_adc_volts = sqrt(variance);

    // Scale ADC analog output to AC mains voltage
    return rms_adc_volts * CALIBRATION_FACTOR;
}

/**
 * @brief Sequentially samples all 3 phases (R, Y, B) and yields to FreeRTOS between channels.
 */
esp_err_t zmpt_read_all(float *v_r, float *v_y, float *v_b) {
    if (!is_ads_ready) {
        *v_r = 0.0f;
        *v_y = 0.0f;
        *v_b = 0.0f;
        return ESP_FAIL;
    }

    // Phase R
    *v_r = get_rms_voltage(ADS_CH_PHASE_R);
    vTaskDelay(pdMS_TO_TICKS(2)); // Feed watchdog & let RTOS context switch

    // Phase Y
    *v_y = get_rms_voltage(ADS_CH_PHASE_Y);
    vTaskDelay(pdMS_TO_TICKS(2));

    // Phase B
    *v_b = get_rms_voltage(ADS_CH_PHASE_B);
    vTaskDelay(pdMS_TO_TICKS(2));

    return ESP_OK;
}