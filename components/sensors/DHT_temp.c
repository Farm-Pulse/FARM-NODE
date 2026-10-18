#include "DHT_temp.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "DHT22";
#define DHT_PIN GPIO_NUM_16

void dht22_init(void) {
    gpio_reset_pin(DHT_PIN);
    ESP_LOGI(TAG, "DHT22 Initialized on GPIO %d", DHT_PIN);
}

// Helper to wait for a specific pin state with a timeout
static int wait_for_state(int state, int timeout_us) {
    int time_waited = 0;
    while (gpio_get_level(DHT_PIN) != state) {
        if (time_waited > timeout_us) return -1;
        esp_rom_delay_us(1);
        time_waited++;
    }
    return time_waited;
}

esp_err_t dht22_read(float *temperature, float *humidity) {
    uint8_t data[5] = {0, 0, 0, 0, 0};

    // 1. Send Start Signal
    gpio_set_direction(DHT_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(DHT_PIN, 0);
    esp_rom_delay_us(20000); // 20ms low
    gpio_set_level(DHT_PIN, 1);
    esp_rom_delay_us(30);    // 30us high
    gpio_set_direction(DHT_PIN, GPIO_MODE_INPUT);

    // 2. Wait for sensor response
    if (wait_for_state(0, 80) == -1) return ESP_ERR_TIMEOUT;
    if (wait_for_state(1, 80) == -1) return ESP_ERR_TIMEOUT;
    if (wait_for_state(0, 80) == -1) return ESP_ERR_TIMEOUT;

    // 3. Read 40 bits (5 bytes)
    for (int i = 0; i < 40; i++) {
        if (wait_for_state(1, 80) == -1) return ESP_ERR_TIMEOUT;
        int pulse_length = wait_for_state(0, 80);
        if (pulse_length == -1) return ESP_ERR_TIMEOUT;
        
        // A pulse > 40us is a '1', otherwise '0'
        data[i / 8] <<= 1;
        if (pulse_length > 40) {
            data[i / 8] |= 1;
        }
    }

    // 4. Verify Checksum
    if (data[4] != ((data[0] + data[1] + data[2] + data[3]) & 0xFF)) {
        ESP_LOGE(TAG, "DHT22 Checksum Failed");
        return ESP_ERR_INVALID_CRC;
    }

    // 5. Calculate final values for DHT22
    *humidity = ((data[0] << 8) | data[1]) / 10.0f;
    *temperature = (((data[2] & 0x7F) << 8) | data[3]) / 10.0f;
    if (data[2] & 0x80) {
        *temperature *= -1.0f;
    }

    return ESP_OK;
}