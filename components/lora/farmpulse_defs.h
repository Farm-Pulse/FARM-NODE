/**
 * @file farmpulse_defs.h
 * @brief FARMPULSE RF-Frame Format Implemnetation - Base Level RF Format Packet
 * @author Shahid  
 * @date Feb 2026
 */

#ifndef FARMPULSE_DEFS_H
#define FARMPULSE_DEFS_H

#include <stdint.h>

// --- Packet Types (4 Bits) ---
typedef enum {
    PKT_TYPE_CMD        = 0x00, // Master -> Node (Set Motor, Get Config)
    PKT_TYPE_DATA       = 0x01, // Node -> Master (Sensor Data)
    PKT_TYPE_STATUS     = 0x02, // I am Alive / Battery Report
    PKT_TYPE_ACK        = 0x04, // Receipt Confirmation
    PKT_TYPE_BROADCAST  = 0x05, // Emergency Stop / Time Sync
    PKT_TYPE_FAULT      = 0x09  // Jamming / Sensor Failure
} packet_type_t;

// --- Frame Control Flags ---
// Bit 7-6: EHO (Extended Header)
// Bit 5:   Encryption
// Bit 4:   Ack Request
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
#pragma pack(pop)

// Maximum Payload (LoRa MTU is 255, but we stick to ~200 for safety)
#define MAX_PAYLOAD_SIZE 200

// Full Packet Structure
typedef struct {
    farm_header_t header;
    uint8_t       payload[MAX_PAYLOAD_SIZE];
} farm_packet_t;

#endif // FARMPULSE_DEFS_H