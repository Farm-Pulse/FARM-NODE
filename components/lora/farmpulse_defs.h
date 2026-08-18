/**
 * @file farmpulse_defs.h
 * @brief FARMPULSE RF-Frame Format Implemnetation - Base Level RF Format Packet
 * @author Shahid  
 * @date Feb 2026
 */

#ifndef FARMPULSE_DEFS_H
#define FARMPULSE_DEFS_H

#include <stdint.h>
#include <stdbool.h>

// Maximum Payload (LoRa MTU is 255, but we stick to ~200 for safety)
#define MAX_PAYLOAD_SIZE 200

// ============================================================================
// 1. MAC / NETWORK LAYER DEFINITIONS
// ============================================================================

// --- Packet Types (FCF bits 0-3) ---
typedef enum {
    PKT_TYPE_CMD        = 0x00, // Config & Control (SET/GET/RESP)
    PKT_TYPE_DATA       = 0x01, // Telemetry & Sensor Data
    PKT_TYPE_STATUS     = 0x02, // Heartbeats & Node Discovery
    PKT_TYPE_ACK        = 0x04, // Hop Acknowledgement
    PKT_TYPE_BROADCAST  = 0x05, // Broadcast Alerts / Time Sync
    PKT_TYPE_FAULT      = 0x09  // Instant Protection & Alarms
} packet_type_t;

// ============================================================================
// 2. APPLICATION LAYER: COMMAND & CONTROL (PKT_TYPE_CMD)
// ============================================================================

// --- Sub-Command / Dispatch Types (Offset 0) ---
typedef enum {
    CMD_TYPE_CONFIG     = 0x05,
    CMD_TYPE_OTA_REQ    = 0x06,
    CMD_TYPE_OTA_DATA   = 0x07
} cmd_type_t;

// --- Command Direction (Offset 1) ---
typedef enum {
    _TYPE_SEND_CMD      = 0x01,
    _TYPE_CMD_RESPONSE  = 0x02
} cmd_direction_t;

// --- Action Codes (Offset 2) ---
typedef enum {
    ACTION_SET          = 0x01,
    ACTION_GET          = 0x02,
    ACTION_EXEC_CMD     = 0x03
} action_type_t;

// --- Config Parameter IDs (Offset 3) ---
typedef enum {
    PARAM_RF_CONFIG     = 0x01, // PAN ID, Channel, TxPower
    PARAM_MOTOR_CTRL    = 0x02, // Motor Relay ON/OFF
    PARAM_DEVICE_ID     = 0x05, // Change Node ID
    PARAM_SIGNATURE     = 0x09, // EEPROM Signature
    PARAM_RSSI_THRESH   = 0x0A, // RSSI Filter Threshold
    PARAM_HB_INTERVAL   = 0x10, // Heartbeat reporting period
    PARAM_ALARM_MASK    = 0x12, // Alarm notification bitmasks
    PARAM_METER_DATA_REQ= 0x1E, // Force Telemetry Transmission
    PARAM_REBOOT        = 0xFF  // System Reboot
} param_id_t;

// ============================================================================
// 3. APPLICATION LAYER: STATUS & ALARMS (PKT_TYPE_STATUS / FAULT)
// ============================================================================

// --- Status Sub-Types ---
typedef enum {
    STATUS_FIRST_BOOTUP = 0x01,
    STATUS_NORMAL_HB    = 0x02,
    STATUS_ND_BEACON    = 0x03
} status_sub_type_t;

// --- Alarm Codes ---
typedef enum {
    ALARM_PHASE_LOSS    = 0x01,
    ALARM_OVERCURRENT   = 0x02,
    ALARM_DRY_RUN       = 0x03,
    ALARM_FREQ_UNSTABLE = 0x04,
    ALARM_COMM_TIMEOUT  = 0x05
} alarm_code_t;

// --- Frame Control Flags ---
// Bit 7-6: EHO (Extended Header)
// Bit 5:   Encryption
// Bit 4:   Ack Requestidf.py
// Bit 3-0: Packet Type
#define FCF_MASK_EHO        0xC0
#define FCF_MASK_ENC        0x20
#define FCF_MASK_ACK_REQ    0x10
#define FCF_MASK_TYPE       0x0F

// --- The Fixed 10-Byte Header ---
// [EmSave Reference source: 100-138] adapted for LoRa
#pragma pack(push, 1) // Ensure no padding bytes!
typedef struct {
    uint8_t  length;         // Total Packet Length
    uint8_t  target_id;      // Immediate Neighbor ID (Next Hop)
    uint8_t  sender_id;      // Immediate Sender ID (Previous Hop)
    uint16_t network_id;     // Farm ID (PAN ID)
    uint8_t  seq_num;        // Anti-duplicate counter
    uint8_t  fcf;            // Frame Control Field (Flags + Type)
    uint8_t  hop_count;      // Remaining Hops
    uint8_t  final_dest_id;  // Ultimate Destination
    uint8_t  origin_src_id;  // Original Creator
} farm_header_t;

// --- Command Message Struct (Payload for PKT_TYPE_CMD) ---
typedef struct {
    uint8_t cmd_type;        // CMD_TYPE_CONFIG (0x05)
    uint8_t direction;       // _TYPE_SEND_CMD (0x01) or _TYPE_CMD_RESPONSE (0x02)
    uint8_t action;          // SET (1), GET (2), EXEC (3)
    uint8_t param_id;        // Parameter ID (e.g., PARAM_MOTOR_CTRL)
    uint8_t payload[32];     // Variable Parameter data
} farm_cmd_t;

// --- Complete 3-Phase Sensor Telemetry Struct (Payload for PKT_TYPE_DATA) ---
typedef struct {
    uint16_t voltage_R;      // Volts
    uint16_t voltage_Y;      // Volts
    uint16_t voltage_B;      // Volts
    uint16_t current_R;      // Amps * 10
    uint16_t current_Y;      // Amps * 10
    uint16_t current_B;      // Amps * 10
    uint32_t power_active;   // Watts
    uint16_t frequency;      // Hz * 10
    uint16_t power_factor;   // PF * 1000
    uint8_t  motor_status;   // 0=OFF, 1=ON, 2=Tripped
    uint8_t  fault_mask;     // Active alarms bitfield
    uint16_t supply_voltage; // Battery / DC Bus mV
} sensor_telemetry_t;

//Full Packet Structure
typedef struct {
    farm_header_t header;
    uint8_t       payload[MAX_PAYLOAD_SIZE];
} farm_packet_t;
#pragma pack(pop)

#endif // FARMPULSE_DEFS_H