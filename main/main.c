#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"

#include "mac_layer.h"
#include "network_layer.h"
#include "farmpulse_defs.h"
#include "zmpt101b.h"
#include "farmpulse_config.h"
#include "farmnode_parser.h"

static const char *TAG = "APP_MAIN";

//#define MY_NODE_ID       CONFIG_FARMPULSE_NODE_ID
//#define IS_GATEWAY       (system_config.node_id == 0)

// --- Configurable Timeouts (in seconds) ---
#define CONFIG_ND_BEACON_INTERVAL   30  // Broadcast Discovery every 30s
#define CONFIG_HEARTBEAT_INTERVAL   100 // Send Alive Status every 5 Minutes
#define CONFIG_TELEMETRY_INTERVAL   300 // Send Sensor Data every 10 Minutes
#define RELAY_PIN   48


/**
 * @brief Industrial RTOS Super Loop for the FarmNode.
 *        Manages periodic Network Maintenance and Background Telemetry.
 */
void farmnode_application_task(void *arg) {
    ESP_LOGI(TAG, "FarmNode RTOS Task Started.");

    // Initialize precise RTOS tick tracking
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t loop_frequency = pdMS_TO_TICKS(1000); // 1-Second Master Tick

    // Independent Counters for Network Maintenance
    uint32_t sec_until_nd_beacon = CONFIG_ND_BEACON_INTERVAL;
    uint32_t sec_until_heartbeat = CONFIG_HEARTBEAT_INTERVAL;
    uint32_t sec_until_telemetry = CONFIG_TELEMETRY_INTERVAL;

    while (1) {
        // Block until exactly 1000ms has passed since the last unblock
        vTaskDelayUntil(&last_wake_time, loop_frequency);

        // ---------------------------------------------------------
        // 1. Neighbor Discovery (ND) Beacon Maintenance
        // ---------------------------------------------------------
        if (--sec_until_nd_beacon == 0) {
            fnSend_ND_Beacon();
            sec_until_nd_beacon = CONFIG_ND_BEACON_INTERVAL; // Reset timer
        }

        // ---------------------------------------------------------
        // 2. Network Heartbeat Maintenance (Alive Status)
        // ---------------------------------------------------------
        if (--sec_until_heartbeat == 0) {
            fnSend_Heartbeat();
            sec_until_heartbeat = CONFIG_HEARTBEAT_INTERVAL; // Reset timer
        }

        // ---------------------------------------------------------
        // 3. Periodic Sensor Telemetry
        // ---------------------------------------------------------
        if (--sec_until_telemetry == 0) {
            ESP_LOGI(TAG, "Periodic Trigger: Pushing 3-Phase Data to Gateway");
            fnSend_Sensor_Telemetry(0); // 0 is Gateway ID
            sec_until_telemetry = CONFIG_TELEMETRY_INTERVAL; // Reset timer
        }
    }
}


// FarmNode main application
void app_main(void) {
    //Versioning
    const esp_app_desc_t *app_desc = esp_app_get_description();
    
    ESP_LOGI(TAG, "FarmPulse Firmware Version: %s", app_desc->version);
    ESP_LOGI(TAG, "Project Name: %s", app_desc->project_name);

    //Initailise the NVS-Flash
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_flash_init();
    }

    farmpulse_config_init();
    
    if (system_config.node_id != 1) {
        ESP_LOGW(TAG, "Setting Node ID to 1...");
        farmpulse_save_node_id(1);
    }
    
    zmpt_init();

    ESP_LOGI(TAG, "==========================================");
    ESP_LOGI(TAG, "   FARMPULSE PHASE 5 - Node ID: %d", system_config.node_id);
    ESP_LOGI(TAG, "==========================================");

    // --- INITIALIZE PHYSICAL HARDWARE ---
    gpio_reset_pin(RELAY_PIN);
    gpio_set_direction(RELAY_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(RELAY_PIN, 0); // Ensure motor is OFF on boot

    mac_init();     
    network_init(); 
    network_register_cb(app_packet_handler);
    
    xTaskCreate(farmnode_application_task, "node_app_task", 4096, NULL, 5, NULL);
}