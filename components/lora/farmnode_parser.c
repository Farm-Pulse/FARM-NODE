#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "network_layer.h"
#include "farmpulse_defs.h"
#include "farmpulse_config.h"
#include "farmnode_parser.h"
#include "zmpt101b.h"
#include "soil_sensor.h"
#include "DHT_temp.h"
#include "DS18B20_temp.h"

#define RELAY_PIN 48

static const char *TAG = "LoRa_PARSER";

static uint8_t current_motor_state = 0; 
static float cached_vr = 0.0f;
static float cached_vy = 0.0f;
static float cached_vb = 0.0f;
static bool phase_r_ok = true;
static bool phase_y_ok = true;
static bool phase_b_ok = true;

// --- Decoupling Flags & Payloads ---
volatile uint8_t pending_req_src = 0; // Remembers who sent the command (e.g., Gateway)

// SET Command Flags & Data
volatile bool flag_set_motor  = false;
volatile uint8_t p_motor_val  = 0;

volatile bool flag_set_net_id = false;
volatile uint8_t p_net_val1   = 0;
volatile uint8_t p_net_val2   = 0;

volatile bool flag_set_dev_id = false;
volatile uint8_t p_dev_id     = 0;

// GET Command Flags
volatile bool flag_get_telemetry = false;
volatile bool flag_get_panel     = false;
volatile bool flag_get_unified   = false;
volatile bool flag_get_motor     = false;
volatile bool flag_get_soil      = false;
volatile bool flag_get_dev_id    = false;
volatile bool flag_get_temp      = false;
volatile bool flag_get_soil_temp = false;

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


void fnSend_Unified_Sensor_Data(uint8_t target_id) {
    // 1. Execute physical hardware reads
    float vr = 0.0f, vy = 0.0f, vb = 0.0f;
    zmpt_read_all(&vr, &vy, &vb);

    float soil_v = 0.0f;
    uint8_t soil_pct = 0;
    soil_sensor_read(&soil_v, &soil_pct); 

    float air_temp = 0.0f, air_hum = 0.0f;
    dht22_read(&air_temp, &air_hum);

    float soil_temp = 0.0f;
    fnRead_Soil_Temperature(&soil_temp);

    // --- CHECKOUT LOGS: Print to Serial Monitor Before Sending ---
    ESP_LOGI(TAG, "=== NODE HARDWARE READOUT ===");
    ESP_LOGI(TAG, "Voltages : R:%.1f V | Y:%.1f V | B:%.1f V", vr, vy, vb);
    ESP_LOGI(TAG, "Soil Mois: %d %% (Raw: %.2f V)", soil_pct, soil_v);
    ESP_LOGI(TAG, "Air Temp : %.2f °C", air_temp);
    ESP_LOGI(TAG, "Air Hum  : %.2f %%", air_hum);
    ESP_LOGI(TAG, "Soil Temp: %.2f °C", soil_temp);
    ESP_LOGI(TAG, "=============================");

    // 2. Build Payload (23 Bytes Total)
    uint8_t resp[23];
    resp[0] = CMD_TYPE_CONFIG;
    resp[1] = _TYPE_CMD_RESPONSE;
    resp[2] = ACTION_DATA;
    resp[3] = PARAM_SENSOR_DATA;

    // Pack 16-bit ZMPT Voltages
    uint16_t v1 = (uint16_t)vr, v2 = (uint16_t)vy, v3 = (uint16_t)vb;
    resp[4] = (v1 >> 8) & 0xFF; resp[5] = v1 & 0xFF;
    resp[6] = (v2 >> 8) & 0xFF; resp[7] = v2 & 0xFF;
    resp[8] = (v3 >> 8) & 0xFF; resp[9] = v3 & 0xFF;

    // Pack 8-bit Soil Moisture
    resp[10] = soil_pct;

    // Pack 32-bit Environmental Floats
    *(float*)&resp[11] = air_temp;
    *(float*)&resp[15] = air_hum;
    *(float*)&resp[19] = soil_temp;

    // 3. Transmit to Gateway
    network_send(target_id, PKT_TYPE_CMD, resp, 23);
    ESP_LOGI(TAG, "EXEC: Unified Sensor Data dispatched to Node %d", target_id);
}


void fnSend_Temp_Data_Resp(uint8_t target_id) {
    float temp = 0.0f, hum = 0.0f;
    
    // Read the hardware sensor
    if (dht22_read(&temp, &hum) != ESP_OK) {
        ESP_LOGW(TAG, "Warning: DHT22 sensor read failed.");
    }
    
    // Build the response payload (13 Bytes Total to hold two 32-bit floats)
    uint8_t resp_payload[13];
    resp_payload[0] = CMD_TYPE_CONFIG;
    resp_payload[1] = _TYPE_CMD_RESPONSE;
    resp_payload[2] = ACTION_DATA;
    resp_payload[3] = PARAM_TEMP_DATA_REQ;
    
    // Pack both 32-bit floats
    *(float*)&resp_payload[4] = temp;           
    *(float*)&resp_payload[8] = hum;           
    
    // Transmit back to Gateway
    network_send(target_id, PKT_TYPE_CMD, resp_payload, 13);
    ESP_LOGI(TAG, "EXEC: DHT22 Data (%.1fC, %.1f%%) dispatched to Node %d", temp, hum, target_id);
}


void fnSend_Soil_Temp_Resp(uint8_t target_id) {
    float soil_temp = 0.0f;
    
    if (fnRead_Soil_Temperature(&soil_temp) != ESP_OK) {
        ESP_LOGW(TAG, "Warning: DS18B20 sensor read failed.");
    }
    
    // Build the 9-Byte Response Payload (Header + 1 Float)
    uint8_t resp[9];
    resp[0] = CMD_TYPE_CONFIG;
    resp[1] = _TYPE_CMD_RESPONSE;
    resp[2] = ACTION_DATA;
    resp[3] = PARAM_SOIL_TEMP_REQ;
    
    // Pack the 32-bit float safely
    *(float*)&resp[4] = soil_temp;           
    
    network_send(target_id, PKT_TYPE_CMD, resp, 9);
    ESP_LOGI(TAG, "EXEC: Soil Temp (%.2fC) sent to Node %d", soil_temp, target_id);
}


void fnSend_Soil_Data_Resp(uint8_t target_id) {
    float soil_v = 0.0f;
    uint8_t soil_pct = 0;
    
    // Execute live hardware read from AIN3
    if (soil_sensor_read(&soil_v, &soil_pct) != ESP_OK) {
        ESP_LOGW(TAG, "Warning: Soil sensor read failed during GET request.");
    }
    
    // Build the response payload (9 Bytes Total)
    uint8_t resp_payload[9];
    resp_payload[0] = CMD_TYPE_CONFIG;
    resp_payload[1] = _TYPE_CMD_RESPONSE;
    resp_payload[2] = ACTION_DATA;
    resp_payload[3] = PARAM_SOIL_DATA_REQ;
    resp_payload[4] = soil_pct;                   // 1 Byte: Moisture (0-100%)
    
    // Pack 32-bit float into the remaining 4 bytes using pointer casting
    *(float*)&resp_payload[5] = soil_v;           
    
    // Transmit back to Gateway
    network_send(target_id, PKT_TYPE_CMD, resp_payload, 9);
    ESP_LOGI(TAG, "EXEC: Soil Data (%d%%, %.2fV) dispatched to Node %d", soil_pct, soil_v, target_id);
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
void fnSend_Motor_State_Resp(uint8_t target_id) {
    uint8_t resp_payload[5];
    
    resp_payload[0] = CMD_TYPE_CONFIG;    // 0x05
    resp_payload[1] = _TYPE_CMD_RESPONSE; // 0x02 (Direction: Response)
    resp_payload[2] = ACTION_DATA;         // 0x02 (Answering a GET action)
    resp_payload[3] = PARAM_LORA_MOTOR_CTRL;   // 0x02
    resp_payload[4] = current_motor_state;// Data: 0 or 1
    
    network_send(target_id, PKT_TYPE_CMD, resp_payload, 5);
    ESP_LOGI(TAG, "EXEC: Motor State (%d) Response dispatched to Gateway.", current_motor_state);
}


void fnSet_Motor_Relay(uint8_t target_id, uint8_t motor_action) {
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
    uint8_t payload[64] = {0}; 
    uint8_t index = 0;

    // MAC Layer CMD Header 
    payload[index++] = CMD_TYPE_CONFIG;
    payload[index++] = _TYPE_CMD_RESPONSE;
    payload[index++] = ACTION_DATA;
    payload[index++] = PARAM_LORA_PANEL_STATUS;

    // DA-[B]: Node ID
    payload[index++] = system_config.node_id;

    // DA-[C]: Firmware Version (e.g., 0x10 = v1.0)
    payload[index++] = 0x10;

    // DA-[D]: Region & Channel No 
    payload[index++] = 0x11;

    // DA-[E]: Network ID 
    uint16_t net_id = 1000;
    payload[index++] = (net_id >> 8) & 0xFF;
    payload[index++] = net_id & 0xFF;

    // DA-[F]: UUID / MAC ID
    uint8_t mac[8] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77};
    memcpy(&payload[index], mac, 8);
    index += 8;

    // DA-[G]: Power ON Interruption Count
    payload[index++] = 5; 

    // DA-[H]: Motor Status
    payload[index++] = current_motor_state;

    // DA-[I, J, K]: ZMPT Live Voltages (4 Bytes Each)
    float vr = 0.0f, vy = 0.0f, vb = 0.0f;
    zmpt_read_all(&vr, &vy, &vb);
    
    *(float*)&payload[index] = vr; index += sizeof(float);
    *(float*)&payload[index] = vy; index += sizeof(float);
    *(float*)&payload[index] = vb; index += sizeof(float);

    // DA-[L, M, N]: Live DHT22 and DS18B20 Environmental Data (4 Bytes Each)
    float air_temp = 0.0f, air_hum = 0.0f, soil_temp = 0.0f;
    
    dht22_read(&air_temp, &air_hum); 
    fnRead_Soil_Temperature(&soil_temp);     
    
    *(float*)&payload[index] = air_temp;  index += sizeof(float);
    *(float*)&payload[index] = air_hum;   index += sizeof(float);
    *(float*)&payload[index] = soil_temp; index += sizeof(float);

    // DA-[O]: Reserved
    payload[index++] = 0x00;

    // DA-[P, Q]: Alarm Registers
    payload[index++] = 0x00; payload[index++] = 0x00; // Alarm Register 1
    payload[index++] = 0x00; payload[index++] = 0x00; // Alarm Register 2

    // DA-[R]: Neighbor Count
    uint8_t neighbor_count = 2;
    payload[index++] = neighbor_count;

    // DA-[S, T, ...]: Neighbor IDs (Offsets shifted automatically)
    payload[index++] = 14;
    payload[index++] = 15;

    // Transmit to Gateway
    network_send(target_id, PKT_TYPE_CMD, payload, index);
    ESP_LOGI(TAG, "EXEC: Live Panel Status dispatched to Node %d (Len: %d bytes)", target_id, index);
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


void fnSet_Device_ID(uint8_t target_id, uint8_t new_id) {
    // 1. Save permanently to Flash and update live RAM
    farmpulse_save_node_id(new_id);
    system_config.node_id = new_id;
    ESP_LOGW(TAG, "EXEC: Node ID permanently changed to %d", new_id);

    // 2. Transmit Acknowledgment
    uint8_t resp[5] = {CMD_TYPE_CONFIG, _TYPE_CMD_RESPONSE, ACTION_DATA, PARAM_DEVICE_ID, new_id};
    network_send(target_id, PKT_TYPE_CMD, resp, 5);
}


void fnGet_Device_ID(uint8_t target_id) {
    uint8_t resp[5] = {CMD_TYPE_CONFIG, _TYPE_CMD_RESPONSE, ACTION_DATA, PARAM_DEVICE_ID, system_config.node_id};
    network_send(target_id, PKT_TYPE_CMD, resp, 5);
    ESP_LOGI(TAG, "EXEC: Node ID (%d) sent to Gateway", system_config.node_id);
}


void fnSet_Network_ID(uint8_t target_id, uint8_t nwk_id_msb, uint8_t nwk_id_lsb) {
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
                                pending_req_src = src_id; // Capture the sender ID

                                // Level 5: Switch by Parameter ID (Offset 3)
                                switch (msg[3]) {
                                    case PARAM_LORA_MOTOR_CTRL:
                                        ESP_LOGI(TAG, "RX: SET Motor Control");
                                        //fnSet_Motor_Relay(src_id, msg[4]); // msg[4] holds the ON/OFF payload
                                        p_motor_val = msg[4];
                                        flag_set_motor = true;
                                        break;
                                        
                                    case PARAM_LORA_CONFIG:
                                        ESP_LOGI(TAG, "RX: SET RF Config (Future implementation)");
                                        //fnSet_Network_ID(src_id, msg[4], msg[5]);
                                        p_net_val1 = msg[4];
                                        p_net_val2 = msg[5];
                                        flag_set_net_id = true;
                                        break;
                                    
                                    case PARAM_DEVICE_ID:
                                        ESP_LOGI(TAG, "RX: SET Device ID Request");
                                        //fnSet_Device_ID(src_id, msg[4]); 
                                        p_dev_id = msg[4];
                                        flag_set_dev_id = true;
                                        break;

                                    default:
                                        ESP_LOGW(TAG, "Unknown SET Parameter: 0x%02X", msg[3]);
                                        break;
                                }
                            }
                            break; // End ACTION_SET

                            case ACTION_GET: // GET_CONFIG (0x02)
                            {
                                pending_req_src = src_id; 

                                // Level 5: Switch by Parameter ID (Offset 3)
                                switch (msg[3]) {
                                    case PARAM_METER_DATA_REQ:
                                        ESP_LOGI(TAG, "RX: GET Sensor Telemetry Request");
                                        //fnSend_Sensor_Telemetry(src_id);
                                        flag_get_telemetry = true;
                                        break;
                                        
                                    case PARAM_LORA_PANEL_STATUS:
                                        ESP_LOGI(TAG, "RX: GET Panel Status Request");
                                        //fnSend_Panel_Status(src_id);
                                        flag_get_panel = true;
                                        break;
                                    
                                    case PARAM_SENSOR_DATA:
                                        ESP_LOGI(TAG, "RX: GET Unified Sensor Data Request");
                                        //fnSend_Unified_Sensor_Data(src_id);
                                        flag_get_unified = true;
                                        break;

                                    case PARAM_LORA_MOTOR_CTRL:
                                        ESP_LOGI(TAG, "RX: GET Motor State Request");
                                        //fnSend_Motor_State_Resp(src_id);
                                        flag_get_motor = true;
                                        break;
                                    
                                    case PARAM_SOIL_DATA_REQ: // NEW ROUTING HERE
                                        ESP_LOGI(TAG, "RX: GET Soil Sensor Request");
                                        //fnSend_Soil_Data_Resp(src_id);
                                        flag_get_soil = true;
                                        break;
                                    
                                    case PARAM_DEVICE_ID:
                                        ESP_LOGI(TAG, "RX: GET Device ID Request");
                                        //fnGet_Device_ID(src_id);
                                        flag_get_dev_id = true;
                                        break;

                                    case PARAM_TEMP_DATA_REQ: // 0x20
                                        ESP_LOGI(TAG, "RX: GET DHT22 Sensor Request");
                                        //fnSend_Temp_Data_Resp(src_id);
                                        flag_get_temp = true;
                                        break;

                                    case PARAM_SOIL_TEMP_REQ: // 0x21
                                        ESP_LOGI(TAG, "RX: GET DS18B20 Soil Temp Request");
                                        //fnSend_Soil_Temp_Resp(src_id);
                                        flag_get_soil_temp = true;
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
