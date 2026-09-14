#include "DS18B20_temp.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "DS18B20";
#define DS_PIN GPIO_NUM_11 // Assign to your physical 1-Wire data pin

void ds18b20_init(void) {
    gpio_reset_pin(DS_PIN);
    ESP_LOGI(TAG, "DS18B20 Initialized on GPIO %d", DS_PIN);
}

// --- 1-Wire Bit-Banging Protocol ---
static bool ds18b20_reset(void) {
    bool presence = false;
    gpio_set_direction(DS_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(DS_PIN, 0);
    esp_rom_delay_us(480);
    
    gpio_set_direction(DS_PIN, GPIO_MODE_INPUT);
    esp_rom_delay_us(70);
    presence = !gpio_get_level(DS_PIN);
    esp_rom_delay_us(410);
    
    return presence;
}

static void ds18b20_write_bit(uint8_t bit) {
    gpio_set_direction(DS_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(DS_PIN, 0);
    esp_rom_delay_us(bit ? 5 : 60);
    gpio_set_direction(DS_PIN, GPIO_MODE_INPUT);
    esp_rom_delay_us(bit ? 55 : 5);
}

static uint8_t ds18b20_read_bit(void) {
    uint8_t bit = 0;
    gpio_set_direction(DS_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(DS_PIN, 0);
    esp_rom_delay_us(3);
    gpio_set_direction(DS_PIN, GPIO_MODE_INPUT);
    esp_rom_delay_us(10);
    bit = gpio_get_level(DS_PIN);
    esp_rom_delay_us(53);
    return bit;
}

static void ds18b20_write_byte(uint8_t data) {
    for (int i = 0; i < 8; i++) {
        ds18b20_write_bit(data & 0x01);
        data >>= 1;
    }
}

static uint8_t ds18b20_read_byte(void) {
    uint8_t data = 0;
    for (int i = 0; i < 8; i++) {
        data |= (ds18b20_read_bit() << i);
    }
    return data;
}

esp_err_t fnRead_Soil_Temperature(float *temperature) {
    if (!ds18b20_reset()) {
        ESP_LOGE(TAG, "DS18B20 Sensor not responding");
        return ESP_ERR_NOT_FOUND;
    }
    
    ds18b20_write_byte(0xCC); // Skip ROM
    ds18b20_write_byte(0x44); // Convert T
    
    // The DS18B20 requires a strict 750ms delay for a 12-bit thermal calculation[cite: 7]
    vTaskDelay(pdMS_TO_TICKS(750)); 
    
    if (!ds18b20_reset()) return ESP_ERR_NOT_FOUND;
    
    ds18b20_write_byte(0xCC); // Skip ROM
    ds18b20_write_byte(0xBE); // Read Scratchpad
    
    uint8_t temp_lsb = ds18b20_read_byte();
    uint8_t temp_msb = ds18b20_read_byte();
    
    int16_t raw_temp = (temp_msb << 8) | temp_lsb;
    *temperature = raw_temp / 16.0f; // Scale 12-bit integer to float
    
    return ESP_OK;
}