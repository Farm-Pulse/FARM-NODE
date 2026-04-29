#include <stdio.h>
#include <math.h>
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_adc/adc_oneshot.h"

#include "zmpt101b.h"

static const char *TAG = "ZMPT101B";

// --- Safe LILYGO T3S3 v1.2 ADC2 Pins ---
#define ZMPT_ADC_UNIT      ADC_UNIT_2
#define ZMPT_CH_R          ADC_CHANNEL_1 // Physical GPIO 12 
#define ZMPT_CH_Y          ADC_CHANNEL_4 // Physical GPIO 15 
#define ZMPT_CH_B          ADC_CHANNEL_5 // Physical GPIO 16 

// --- Tuning Parameters ---
#define SAMPLE_WINDOW_US   40000  // 40ms = exactly two 50Hz cycles
#define NUM_SAMPLES        400    // Samples per phase per window
#define CALIBRATION_FACTOR 0.254f // Tune this with a multimeter later

static adc_oneshot_unit_handle_t adc2_handle;

esp_err_t zmpt_init(void) {
    ESP_LOGI(TAG, "Initializing ZMPT101B 3-Phase ADC on ADC_UNIT_2...");

    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ZMPT_ADC_UNIT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc2_handle));

    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12, // 12dB allows reading up to ~3.3V
    };
    
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc2_handle, ZMPT_CH_R, &config));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc2_handle, ZMPT_CH_Y, &config));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc2_handle, ZMPT_CH_B, &config));

    ESP_LOGI(TAG, "ZMPT101B ADC Initialized.");
    return ESP_OK;
}

// Internal helper to calculate True RMS for a single channel
static float get_rms_voltage(adc_channel_t channel) {
    uint64_t sum = 0;
    uint64_t sq_sum = 0;
    int raw_val = 0;
    
    uint32_t start_time = (uint32_t)esp_timer_get_time();
    int samples_taken = 0;

    // Sample rapidly for exactly 40ms
    while ((uint32_t)esp_timer_get_time() - start_time < SAMPLE_WINDOW_US) {
        
        // --- THE ARBITRATION FIX ---
        // ADC2 shares hardware with the Wi-Fi PHY. If Wi-Fi is actively transmitting, 
        // this read might timeout. We check for ESP_OK to ensure we only sum valid data.
        if (adc_oneshot_read(adc2_handle, channel, &raw_val) == ESP_OK) {
            sum += raw_val;
            sq_sum += ((uint64_t)raw_val * raw_val);
            samples_taken++;
        }
        
        // Small delay to yield to watchdog and spread out samples
        esp_rom_delay_us(50); 
    }

    if (samples_taken == 0) return 0.0f;

    // Math: Variance = Mean of Squares - Square of Mean
    float mean = (float)sum / samples_taken;
    float mean_of_squares = (float)sq_sum / samples_taken;
    float variance = mean_of_squares - (mean * mean);
    
    if (variance < 0) variance = 0; // Prevent NaN

    float rms_adc = sqrt(variance);
    
    // Convert ADC RMS to Real Volts
    return rms_adc * CALIBRATION_FACTOR;
}

esp_err_t zmpt_read_all(float *v_r, float *v_y, float *v_b) {
    // Read phases sequentially. 
    // Takes about ~120ms total (40ms per phase)
    *v_r = get_rms_voltage(ZMPT_CH_R);
    *v_y = get_rms_voltage(ZMPT_CH_Y);
    *v_b = get_rms_voltage(ZMPT_CH_B);
    
    return ESP_OK;
}