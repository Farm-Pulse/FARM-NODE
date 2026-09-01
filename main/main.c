#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
//#include "esp_app_desc.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"

#include "mac_layer.h"
#include "network_layer.h"
#include "farmpulse_defs.h"
#include "zmpt101b.h"

static const char *TAG = "APP_MAIN";

#define MY_NODE_ID       CONFIG_FARMPULSE_NODE_ID
#define IS_GATEWAY       (CONFIG_FARMPULSE_NODE_ID == 0)

// --- Configurable Timeouts (in seconds) ---
#define CONFIG_ND_BEACON_INTERVAL   30  // Broadcast Discovery every 30s
#define CONFIG_HEARTBEAT_INTERVAL   100 // Send Alive Status every 5 Minutes
#define CONFIG_TELEMETRY_INTERVAL   300 // Send Sensor Data every 10 Minutes
#define RELAY_PIN   48

static uint8_t current_motor_state = 0; 


static void fnSend_ND_Beacon(void) {
    uint8_t nd_payload[8];
    nd_payload[0] = STATUS_ND_BEACON; // e.g., 0x03
    nd_payload[1] = MY_NODE_ID;       
    // Pack other required ND data (Firmware version, PAN ID, etc.)
    
    // 0xFF is the Broadcast Address
    network_send(0xFF, PKT_TYPE_STATUS, nd_payload, sizeof(nd_payload));
    ESP_LOGD(TAG, "Broadcasted ND Beacon.");
}


static void fnSend_Sensor_Telemetry(uint8_t target_id) {
    sensor_telemetry_t tele;
    
    // Read physical sensors
    float vr = 0, vy = 0, vb = 0; 
    zmpt_read_all(&vr, &vy, &vb);
    
    tele.voltage_R = (uint16_t)vr;
    tele.voltage_Y = (uint16_t)vy;
    tele.voltage_B = (uint16_t)vb;
    tele.current_R = 152; // Simulated 15.2A
    tele.current_Y = 148;
    tele.current_B = 155;
    tele.power_active = (tele.voltage_R * (tele.current_R / 10)) + 
                        (tele.voltage_Y * (tele.current_Y / 10)) + 
                        (tele.voltage_B * (tele.current_B / 10));
                        
    tele.frequency    = 500; // 50.0 Hz
    tele.power_factor = 980; // 0.98 PF
    tele.motor_status = current_motor_state;
    tele.fault_mask   = 0x00;
    tele.supply_voltage = 3300; 
    
    // Transmit requested data back to Gateway
    network_send(target_id, PKT_TYPE_DATA, (uint8_t*)&tele, sizeof(sensor_telemetry_t));
    ESP_LOGI(TAG, "EXEC: Telemetry Data dispatched to Node %d", target_id);
}


/**
 * @brief Builds and transmits the Periodic Normal Heartbeat (0x02).
 *        Informs the Gateway of node health, motor state, and routing neighbors.
 */
static void fnSend_Heartbeat(void) {
    uint8_t hb_payload[32]; // Buffer large enough for HB + Neighbors
    uint8_t index = 0;
    
    // 1. Pack the standard HB data based on the .md specification
    hb_payload[index++] = STATUS_NORMAL_HB;   // Sub-Status: 0x02
    hb_payload[index++] = 65;                 // Simulated RSSI (-65 dBm, absolute value)
    hb_payload[index++] = 1;                  // Simulated Hop Count
    hb_payload[index++] = current_motor_state;// 0x01 = ON, 0x00 = OFF
    
    // 2. Fetch Neighbor Table (Simulated for now based on Acevin logic)
    // In production, you will pull this from your NBT (Neighbor Table) array
    uint8_t simulated_neighbor_count = 2;
    uint8_t simulated_neighbors[2]   = {14, 15}; // Nodes 14 and 15 are in vicinity
    
    hb_payload[index++] = simulated_neighbor_count; 
    
    for(uint8_t i = 0; i < simulated_neighbor_count; i++) {
        hb_payload[index++] = simulated_neighbors[i];
    }
    
    // 3. Blast the Status packet to the Gateway (ID: 0)
    network_send(0, PKT_TYPE_STATUS, hb_payload, index);
    ESP_LOGI(TAG, "Transmitted Normal HB to Gateway. (Length: %d bytes)", index);
}


/**
 * @brief Responds to a Gateway GET request for the motor status.
 * @param target_id The Node ID to send the response back to (usually 0 for Gateway).
 */
static void fnSend_Motor_State_Resp(uint8_t target_id) {
    uint8_t resp_payload[5];
    
    resp_payload[0] = CMD_TYPE_CONFIG;    // 0x05
    resp_payload[1] = _TYPE_CMD_RESPONSE; // 0x02 (Direction: Response)
    resp_payload[2] = ACTION_GET;         // 0x02 (Answering a GET action)
    resp_payload[3] = PARAM_LORA_MOTOR_CTRL;   // 0x02
    resp_payload[4] = current_motor_state;// Data: 0 or 1
    
    network_send(target_id, PKT_TYPE_CMD, resp_payload, 5);
    ESP_LOGI(TAG, "EXEC: Motor State (%d) Response dispatched to Gateway.", current_motor_state);
}


static void fnSet_Motor_Relay(uint8_t target_id, uint8_t motor_action) {
    if (motor_action == 1) {
        current_motor_state = 1;
        gpio_set_level(RELAY_PIN, 1);
        ESP_LOGI(TAG, "EXEC: Motor turned ON");
    } else if (motor_action == 0) {
        current_motor_state = 0;
        gpio_set_level(RELAY_PIN, 0);
        ESP_LOGI(TAG, "EXEC: Motor turned OFF");
    }

    // Prepare and send the RESP frame back to the requester (Gateway)
    uint8_t resp_payload[5];
    resp_payload[0] = CMD_TYPE_CONFIG;
    resp_payload[1] = _TYPE_CMD_RESPONSE;
    resp_payload[2] = ACTION_SET;
    resp_payload[3] = PARAM_LORA_MOTOR_CTRL;
    resp_payload[4] = current_motor_state;

    network_send(target_id, PKT_TYPE_CMD, resp_payload, 5);
    ESP_LOGI(TAG, "EXEC: Motor State (%d) Response dispatched to Gateway.", current_motor_state);
}

/**
 * @brief Instantly transmits a critical hardware fault or warning to the Gateway.
 * @param alarm_code Enum defining the specific failure (e.g., ALARM_OVERCURRENT)
 * @param severity 0x01 for Warning, 0x02 for Critical/Auto-Trip
 * @param fault_value The physical reading that triggered the fault (e.g., high current)
 */
void fnTrigger_Alarm(alarm_code_t alarm_code, uint8_t severity, uint32_t fault_value) {
    uint8_t alarm_payload[6];
    
    alarm_payload[0] = (uint8_t)alarm_code;
    alarm_payload[1] = severity;
    
    // Fast pointer casting to pack the 32-bit integer into the 4-byte array slot 
    // (Standard Acevin technique for ESP32/ARM Little-Endian packing)
    *(uint32_t*)&alarm_payload[2] = fault_value;
    
    // Blast it to the Gateway immediately, bypassing any RTOS delays
    network_send(0, PKT_TYPE_FAULT, alarm_payload, 6);
    
    ESP_LOGE(TAG, ">>> CRITICAL ALARM DISPATCHED: Code 0x%02X, Sev: %d, Val: %lu <<<", 
             alarm_code, severity, fault_value);
}


void app_packet_handler(uint8_t src_id, uint8_t type, uint8_t *msg, uint8_t len) {
    
    // Level 1: Switch by Base Packet Type
    switch (type) {
        
        case PKT_TYPE_CMD:
        {
            // Level 2: Switch by Command Category (Offset 0)
            switch (msg[0]) {
                
                case CMD_TYPE_CONFIG: // 0x05
                {
                    // Level 3: Verify Direction (Offset 1)
                    if (msg[1] == _TYPE_SEND_CMD) {
                        
                        // Level 4: Switch by Action Code (Offset 2)
                        switch (msg[2]) {
                            
                            case ACTION_SET: // SET_CONFIG (0x01)
                            {
                                // Level 5: Switch by Parameter ID (Offset 3)
                                switch (msg[3]) {
                                    case PARAM_LORA_MOTOR_CTRL:
                                        ESP_LOGI(TAG, "RX: SET Motor Control");
                                        fnSet_Motor_Relay(src_id, msg[4]); // msg[4] holds the ON/OFF payload
                                        break;
                                        
                                    case PARAM_LORA_CONFIG:
                                        ESP_LOGI(TAG, "RX: SET RF Config (Future implementation)");
                                        break;
                                        
                                    default:
                                        ESP_LOGW(TAG, "Unknown SET Parameter: 0x%02X", msg[3]);
                                        break;
                                }
                            }
                            break; // End ACTION_SET

                            case ACTION_GET: // GET_CONFIG (0x02)
                            {
                                // Level 5: Switch by Parameter ID (Offset 3)
                                switch (msg[3]) {
                                    case PARAM_METER_DATA_REQ:
                                        ESP_LOGI(TAG, "RX: GET Sensor Telemetry Request");
                                        fnSend_Sensor_Telemetry(src_id);
                                        break;
                                        
                                    case PARAM_LORA_MOTOR_CTRL:
                                        ESP_LOGI(TAG, "RX: GET Motor State Request");
                                        fnSend_Motor_State_Resp(src_id);
                                        break;
                                        
                                    default:
                                        ESP_LOGW(TAG, "Unknown GET Parameter: 0x%02X", msg[3]);
                                        break;
                                }
                            }
                            break; // End ACTION_GET
                            
                            default:
                                ESP_LOGW(TAG, "Unknown Command Action: 0x%02X", msg[2]);
                                break;
                        }
                    }
                }
                break; // End CMD_TYPE_CONFIG
                
                case CMD_TYPE_OTA_REQ: // 0x06 (Phase-2 preparation)
                    ESP_LOGI(TAG, "RX: OTA Request received. Handled in OTA module.");
                    // fnOTA_Process_Request(src_id, msg, len);
                    break;
                    
                default:
                    ESP_LOGW(TAG, "Unknown Command Category: 0x%02X", msg[0]);
                    break;
            }
        }
        break; // End PKT_TYPE_CMD

        case PKT_TYPE_STATUS:
            // Handle Heartbeats & Alarms
            break;

        default:
            // Ignore Data packets or unexpected types at the Node level
            break;
    }
}


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
    //const esp_app_desc_t *app_desc = esp_app_get_description();
    
    //ESP_LOGI(TAG, "FarmPulse Firmware Version: %s", app_desc->version);
    //ESP_LOGI(TAG, "Project Name: %s", app_desc->project_name);

    //Initailise the NVS-Flash
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_flash_init();
    }

    zmpt_init();

    ESP_LOGI(TAG, "==========================================");
    ESP_LOGI(TAG, "   FARMPULSE PHASE 5 - Node ID: %d", MY_NODE_ID);
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