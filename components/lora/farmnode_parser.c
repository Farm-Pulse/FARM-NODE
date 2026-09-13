#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "network_layer.h"
#include "farmpulse_defs.h"
#include "farmpulse_config.h"
#include "farmnode_parser.h"
#include "zmpt101b.h"

#define RELAY_PIN 48

static const char *TAG = "LoRa_PARSER";
static uint8_t current_motor_state = 0; 
static float cached_vr = 0.0f;
static float cached_vy = 0.0f;
static float cached_vb = 0.0f;
static bool phase_r_ok = true;
static bool phase_y_ok = true;
static bool phase_b_ok = true;
static uint8_t active_fault_mask = 0x00; // Bit 0: R, Bit 1: Y, Bit 2: B

// 2. Add the unified background hardware poller
void fnPoll_Sensors_Background(void) {
    if (zmpt_read_all(&cached_vr, &cached_vy, &cached_vb) == ESP_OK) {
        bool r_ok = (cached_vr >= 100.0f);
        bool y_ok = (cached_vy >= 100.0f);
        bool b_ok = (cached_vb >= 100.0f);
        bool state_changed = false;

        // Total Grid Failure Check
        if (!r_ok && !y_ok && !b_ok && (phase_r_ok || phase_y_ok || phase_b_ok)) {
            fnTrigger_Alarm(0x01, 0x02, (0 << 16) | 0); // Phase ID 0 = All
            state_changed = true;
        } 
        else {
            // Individual Phase Checks
            if (r_ok != phase_r_ok) {
                uint32_t val = (1 << 16) | (uint16_t)cached_vr;
                fnTrigger_Alarm(r_ok ? 0x02 : 0x01, r_ok ? 0x01 : 0x02, val);
                state_changed = true;
            }
            if (y_ok != phase_y_ok) {
                uint32_t val = (2 << 16) | (uint16_t)cached_vy;
                fnTrigger_Alarm(y_ok ? 0x02 : 0x01, y_ok ? 0x01 : 0x02, val);
                state_changed = true;
            }
            if (b_ok != phase_b_ok) {
                uint32_t val = (3 << 16) | (uint16_t)cached_vb;
                fnTrigger_Alarm(b_ok ? 0x02 : 0x01, b_ok ? 0x01 : 0x02, val);
                state_changed = true;
            }
        }

        phase_r_ok = r_ok;
        phase_y_ok = y_ok;
        phase_b_ok = b_ok;

        if (state_changed) {
            fnSend_Sensor_Telemetry(0); 
        }
    }
}


void fnSend_ND_Beacon(void) {
    uint8_t nd_payload[8];
    nd_payload[0] = STATUS_ND_BEACON; // e.g., 0x03
    nd_payload[1] = system_config.node_id;       
    // Pack other required ND data (Firmware version, PAN ID, etc.)
    
    // 0xFF is the Broadcast Address
    network_send(0xFF, PKT_TYPE_STATUS, nd_payload, sizeof(nd_payload));
    ESP_LOGD(TAG, "Broadcasted ND Beacon.");
}


//Update the existing telemetry function to be non-blocking
void fnSend_Sensor_Telemetry(uint8_t target_id) {
    sensor_telemetry_t tele;
    
    // Instantly pack the cached values without blocking the RF stack
    tele.voltage_R = (uint16_t)cached_vr;
    tele.voltage_Y = (uint16_t)cached_vy;
    tele.voltage_B = (uint16_t)cached_vb;
    
    tele.current_R = 0; // Keep your existing simulated values for now
    tele.current_Y = 0;
    tele.current_B = 0;
    tele.power_active = ((tele.voltage_R * tele.current_R) / 10) + 
                        ((tele.voltage_Y * tele.current_Y) / 10) + 
                        ((tele.voltage_B * tele.current_B) / 10);
                        
    tele.frequency    = 500; 
    tele.power_factor = 980; 
    tele.motor_status = current_motor_state;
    tele.fault_mask = ( (phase_r_ok ? 0 : 1) | ((phase_y_ok ? 0 : 1) << 1) | ((phase_b_ok ? 0 : 1) << 2) );
    tele.supply_voltage = 3300; 
    
    network_send(target_id, PKT_TYPE_DATA, (uint8_t*)&tele, sizeof(sensor_telemetry_t));
    ESP_LOGI(TAG, "EXEC: Fast Telemetry dispatched to Node %d", target_id);
}


/**
 * @brief Builds and transmits the Periodic Normal Heartbeat (0x02).
 *        Informs the Gateway of node health, motor state, and routing neighbors.
 */
void fnSend_Heartbeat(uint8_t target_id) {
    uint8_t hb_payload[32]; 
    uint8_t index = 0;
    
    hb_payload[index++] = STATUS_NORMAL_HB;   
    hb_payload[index++] = 65;                 
    hb_payload[index++] = 1;                  
    hb_payload[index++] = current_motor_state;
    
    uint8_t simulated_neighbor_count = 2;
    uint8_t simulated_neighbors[2]   = {14, 15}; 
    
    hb_payload[index++] = simulated_neighbor_count; 
    for(uint8_t i = 0; i < simulated_neighbor_count; i++) {
        hb_payload[index++] = simulated_neighbors[i];
    }
    
    // Blast the Status packet to the specific target
    network_send(target_id, PKT_TYPE_STATUS, hb_payload, index);
    ESP_LOGI(TAG, "Transmitted Normal HB to Node %d. (Length: %d bytes)", target_id, index);
}


/**
 * @brief Responds to a Gateway GET request for the motor status.
 * @param target_id The Node ID to send the response back to (usually 0 for Gateway).
 */
static void fnSend_Motor_State_Resp(uint8_t target_id) {
    uint8_t resp_payload[5];
    
    resp_payload[0] = CMD_TYPE_CONFIG;    // 0x05
    resp_payload[1] = _TYPE_CMD_RESPONSE; // 0x02 (Direction: Response)
    resp_payload[2] = ACTION_DATA;         // 0x02 (Answering a GET action)
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
    resp_payload[2] = ACTION_DATA;
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


void fnSend_Panel_Status(uint8_t target_id) {
    uint8_t payload[64] = {0}; // Safe buffer for massive payload
    uint8_t index = 0;

    // MAC Layer CMD Header (Offset 0 to 3)
    payload[index++] = CMD_TYPE_CONFIG;
    payload[index++] = _TYPE_CMD_RESPONSE;
    payload[index++] = ACTION_DATA;
    payload[index++] = PARAM_LORA_PANEL_STATUS;

    // DA-[B]: Node ID
    payload[index++] = system_config.node_id;

    // DA-[C]: Firmware Version (e.g., 0x10 = v1.0)
    payload[index++] = 0x10;

    // DA-[D]: Region & Channel No (e.g., 0x11 = Reg 1, Ch 1)
    payload[index++] = 0x11;

    // DA-[E]: Network ID (2 Bytes - MSB First)
    uint16_t net_id = 1000;
    payload[index++] = (net_id >> 8) & 0xFF;
    payload[index++] = net_id & 0xFF;

    // DA-[F]: UUID / MAC ID (8 Bytes)
    uint8_t mac[8] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77};
    memcpy(&payload[index], mac, 8);
    index += 8;

    // DA-[G]: Power ON Interruption Count
    payload[index++] = 5; // Example count

    // DA-[H]: Motor Status
    payload[index++] = current_motor_state;

    // DA-[I, J, K]: Voltages (2 Bytes Each)
    float vr = 230, vy = 230, vb = 230;
    zmpt_read_all(&vr, &vy, &vb);
    
    uint16_t v1 = (uint16_t)vr;
    uint16_t v2 = (uint16_t)vy;
    uint16_t v3 = (uint16_t)vb;
    
    payload[index++] = (v1 >> 8) & 0xFF; payload[index++] = v1 & 0xFF;
    payload[index++] = (v2 >> 8) & 0xFF; payload[index++] = v2 & 0xFF;
    payload[index++] = (v3 >> 8) & 0xFF; payload[index++] = v3 & 0xFF;

    // DA-[L, M, N]: Temp, Humidity, Soil 
    payload[index++] = 35; // 35C Temp
    payload[index++] = 60; // 60% Hum
    payload[index++] = 45; // 45% Soil

    // DA-[O]: Reserved
    payload[index++] = 0x00;

    // DA-[P, Q]: Alarm 1 & Alarm 2 (2 Bytes Each)
    payload[index++] = 0x00; payload[index++] = 0x00; // Alarm Register 1
    payload[index++] = 0x00; payload[index++] = 0x00; // Alarm Register 2

    // DA-[R]: Neighbor Count
    uint8_t neighbor_count = 2;
    payload[index++] = neighbor_count;

    // DA-[S, T, ...]: Neighbor IDs (Loop based on count)
    payload[index++] = 14;
    payload[index++] = 15;

    // Transmit to Gateway
    network_send(target_id, PKT_TYPE_CMD, payload, index);
    ESP_LOGI(TAG, "EXEC: Panel Status dispatched to Node %d (Len: %d bytes)", target_id, index);
}


void fnCheck_Phase_Loss(void) {
    float vr = 0, vy = 0, vb = 0;
    zmpt_read_all(&vr, &vy, &vb);
    
    // Check if any phase voltage drops below a critical 100V threshold
    if (vr < 100.0 || vy < 100.0 || vb < 100.0) {
        // Find the specific missing phase to report
        uint32_t fault_val = (vr < 100.0) ? (uint32_t)vr : ((vy < 100.0) ? (uint32_t)vy : (uint32_t)vb);
        
        // Trigger Alarm: 0x01 (Phase Loss), 0x02 (Critical Severity)
        fnTrigger_Alarm(0x01, 0x02, fault_val);
        ESP_LOGE(TAG, "CRITICAL: Phase Loss Detected! Alarm Transmitted.");
    }
}


static void fnSet_Device_ID(uint8_t target_id, uint8_t new_id) {
    // 1. Save permanently to Flash and update live RAM
    farmpulse_save_node_id(new_id);
    system_config.node_id = new_id;
    ESP_LOGW(TAG, "EXEC: Node ID permanently changed to %d", new_id);

    // 2. Transmit Acknowledgment
    uint8_t resp[5] = {CMD_TYPE_CONFIG, _TYPE_CMD_RESPONSE, ACTION_DATA, PARAM_DEVICE_ID, new_id};
    network_send(target_id, PKT_TYPE_CMD, resp, 5);
}


static void fnGet_Device_ID(uint8_t target_id) {
    uint8_t resp[5] = {CMD_TYPE_CONFIG, _TYPE_CMD_RESPONSE, ACTION_DATA, PARAM_DEVICE_ID, system_config.node_id};
    network_send(target_id, PKT_TYPE_CMD, resp, 5);
    ESP_LOGI(TAG, "EXEC: Node ID (%d) sent to Gateway", system_config.node_id);
}


static void fnSet_Network_ID(uint8_t target_id, uint8_t nwk_id_msb, uint8_t nwk_id_lsb) {
    uint16_t new_pan = (nwk_id_msb << 8) | nwk_id_lsb;
    // Apply new PAN ID to network layer here
    ESP_LOGW(TAG, "EXEC: Network PAN ID changed to 0x%04X", new_pan);

    uint8_t resp[6] = {CMD_TYPE_CONFIG, _TYPE_CMD_RESPONSE, ACTION_DATA, 0x06, nwk_id_msb, nwk_id_lsb};
    network_send(target_id, PKT_TYPE_CMD, resp, 6);
}


/* App Packet Handler */
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
                                        
                                    case PARAM_LORA_PANEL_STATUS:
                                        ESP_LOGI(TAG, "RX: GET Panel Status Request");
                                        fnSend_Panel_Status(src_id);
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
