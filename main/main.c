#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"

#include "mac_layer.h"
#include "network_layer.h"
#include "farmpulse_defs.h"

static const char *TAG = "APP_MAIN";

#define MY_NODE_ID       CONFIG_FARMPULSE_NODE_ID
#define IS_GATEWAY       (CONFIG_FARMPULSE_NODE_ID == 0)

// --- Application State ---
static uint8_t motor_state = 0; // 0=OFF, 1=ON

// --- Callback: Handle Incoming Data ---
void app_packet_handler(uint8_t src_id, uint8_t type, uint8_t *data, uint8_t len) {
    
    // CASE 1: Gateway Receiving Sensor Data
    if (IS_GATEWAY && type == PKT_TYPE_DATA) {
        if (len == sizeof(sensor_data_t)) {
            sensor_data_t *s = (sensor_data_t *)data;
            ESP_LOGI(TAG, "--- 3-PHASE DATA FROM NODE %d ---", src_id);
            ESP_LOGI(TAG, "   Voltage: R=%d V, Y=%d V, B=%d V", s->voltage_R, s->voltage_Y, s->voltage_B);
            ESP_LOGI(TAG, "   Current: R=%d A, Y=%d A, B=%d A (x10)", s->current_R, s->current_Y, s->current_B);
            ESP_LOGI(TAG, "   Power:   %ld Watts", s->power_active);
            ESP_LOGI(TAG, "   Motor:   %s", s->motor_status ? "ON" : "OFF");
            ESP_LOGI(TAG, "------------------------------------");
        }
    }

    // CASE 2: Node Receiving Command
    else if (!IS_GATEWAY && type == PKT_TYPE_CMD) {
        uint8_t cmd = data[0];
        if (cmd == CMD_MOTOR_ON) {
            motor_state = 1;
            ESP_LOGW(TAG, "COMMAND RECEIVED: MOTOR ON [RELAY HIGH]");
            // gpio_set_level(RELAY_PIN, 1); 
        } 
        else if (cmd == CMD_MOTOR_OFF) {
            motor_state = 0;
            ESP_LOGW(TAG, "COMMAND RECEIVED: MOTOR OFF [RELAY LOW]");
            // gpio_set_level(RELAY_PIN, 0);
        }
    }
}

// --- Task: Business Logic ---
void application_task(void *arg) {
    uint32_t counter = 0;
    
    while (1) {
        // Interval: 10 Seconds
        vTaskDelay(pdMS_TO_TICKS(10000));

        // --- GATEWAY LOGIC ---
        if (IS_GATEWAY) {
            // Every 3rd cycle (30 secs), toggle the motor on Node 1
            if (counter % 3 == 0) {
                uint8_t cmd = (motor_state == 0) ? CMD_MOTOR_ON : CMD_MOTOR_OFF;
                motor_state = !motor_state; // Toggle local state for demo
                
                ESP_LOGI(TAG, "Gateway: Sending CMD_MOTOR_%s to Node 1", cmd ? "ON" : "OFF");
                network_send(1, PKT_TYPE_CMD, &cmd, 1);
            } 
            else {
                // Heartbeat
                ESP_LOGI(TAG, "Gateway: Sending Discovery Broadcast...");
                uint8_t dummy = 0;
                network_send(0xFF, PKT_TYPE_STATUS, &dummy, 1);
            }
        }
        
        // --- NODE LOGIC ---
        else {
            // Simulate Reading Sensors
            sensor_data_t my_data;
            my_data.voltage_R = 230 + (counter % 10);
            my_data.voltage_Y = 228;
            my_data.voltage_B = 232;
            my_data.current_R = 15 + (motor_state * 50); // Higher current if motor is ON
            my_data.current_Y = 14 + (motor_state * 50);
            my_data.current_B = 16 + (motor_state * 50);
            my_data.power_active = 1500 + (motor_state * 5000);
            my_data.motor_status = motor_state;

            ESP_LOGI(TAG, "Node %d: Sending 3-Phase Data (Motor: %s)...", 
                     MY_NODE_ID, motor_state ? "ON" : "OFF");
            
            bool success = network_send(0, PKT_TYPE_DATA, (uint8_t*)&my_data, sizeof(sensor_data_t));
            if (!success) ESP_LOGW(TAG, "Tx Failed.");
        }
        counter++;
    }
}

void app_main(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "==========================================");
    ESP_LOGI(TAG, "   FARMPULSE PHASE-3 - Node ID: %d", MY_NODE_ID);
    ESP_LOGI(TAG, "==========================================");

    mac_init();     
    network_init(); 
    
    // Register our App Callback to receive data from Network Layer
    network_register_cb(app_packet_handler);
    
    xTaskCreate(application_task, "app_task", 4096, NULL, 5, NULL);
}