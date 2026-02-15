/**
 * @file network_layer.h
 * @brief Newtork Layer Implementation - Base Level RF Format Packet
 * @author Shahid  
 * @date Feb 2026
 */

#ifndef NETWORK_H
#define NETWORK_H

#include "farmpulse_defs.h"
#include <stdbool.h>

// --- Configuration Constants (Linked to Kconfig) ---
#define NEIGHBOR_TABLE_SIZE     12   // Keep small for now (Ref source: 237 says 12)
#define NEIGHBOR_TIMEOUT_MS     (CONFIG_MESH_HEARTBEAT_INTERVAL_MS * 3) // 3 missed heartbeats = Dead

// --- Neighbor Table Entry ---
typedef enum {
    NODE_DISCONNECTED = 0,
    NODE_CONNECTED    = 1
} node_status_t;

typedef struct {
    uint8_t       node_id;
    int8_t        rssi;          // Signal Strength
    node_status_t status;
    uint32_t      last_seen_ms;  // Timestamp for cleanup
    uint8_t       tx_seq;        // Last Seq sent to them
    uint8_t       rx_seq;        // Last Seq received from them
} neighbor_entry_t;

// --- Function Prototypes ---

/**
 * @brief Initialize the Network Layer (Clear tables, set defaults)
 */
void network_init(void);

/**
 * @brief Main Routing Logic (The Hunting Algorithm)
 * Decides whether to consume the packet or forward it.
 * * @param packet Pointer to the received packet
 * @param rssi   Signal strength of the received packet
 */
void network_handle_packet(farm_packet_t *packet, int8_t rssi);

/**
 * @brief Send data to a specific ID (handles routing automatically)
 * * @param dest_id Ultimate destination ID
 * @param type    Packet Type (CMD, DATA, etc.)
 * @param payload Data buffer
 * @param len     Data length
 * @return true if routed successfully to next hop
 */
bool network_send(uint8_t dest_id, packet_type_t type, uint8_t *payload, uint8_t len);

#endif // NETWORK_H