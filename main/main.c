#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "mac_layer.h"
#include "network_layer.h"
#include "farmpulse_defs.h"

static const char *TAG = "APP_MAIN";

#define MY_NODE_ID       CONFIG_FARMPULSE_NODE_ID
#define IS_GATEWAY       (CONFIG_FARMPULSE_NODE_ID == 0)

// --- Task: Sensor / Business Logic ---
void application_task(void *arg) {
    uint8_t count = 0;
    
    while (1) {
        // Wait 5 seconds
        vTaskDelay(pdMS_TO_TICKS(5000));

        // --- GATEWAY LOGIC (ID 0) ---
        if (IS_GATEWAY) {
            // Gateway sends a BROADCAST Heartbeat
            // This allows Nodes to "hear" the Gateway and add it to their Neighbor Table
            uint8_t status_payload[4] = {0xAA, 0xBB, 0xCC, count++};
            
            ESP_LOGI(TAG, "Gateway: Sending Discovery Broadcast...");
            network_send(0xFF, PKT_TYPE_STATUS, status_payload, 4);
        }
        
        // --- NODE LOGIC (ID > 0) ---
        else {
            ESP_LOGI(TAG, "Node %d: Reading Sensors...", MY_NODE_ID);
            
            uint8_t sensor_payload[4];
            sensor_payload[0] = 85; // Humidity
            sensor_payload[1] = 40; // Soil Moisture
            sensor_payload[2] = 1;  // Motor Status
            sensor_payload[3] = count++;

            ESP_LOGI(TAG, "Node %d: Sending Data to Gateway...", MY_NODE_ID);
            
            // Try to send to Gateway (ID 0)
            bool success = network_send(0, PKT_TYPE_DATA, sensor_payload, 4);
            
            if (success) {
                ESP_LOGI(TAG, "Packet Transmitted.");
            } else {
                ESP_LOGW(TAG, "Tx Failed (No Route). Waiting for Gateway Heartbeat...");
            }
        }
    }
}

void app_main(void) {
    // 1. Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "==========================================");
    ESP_LOGI(TAG, "   FARMPULSE BOOTING - Node ID: %d", MY_NODE_ID);
    ESP_LOGI(TAG, "==========================================");

    // 2. Initialize the RF Stack
    mac_init();     
    network_init(); 
    
    // 3. Start Application
    xTaskCreate(application_task, "app_task", 4096, NULL, 5, NULL);
}