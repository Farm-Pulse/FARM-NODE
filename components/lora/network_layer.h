/**
 * @file network_layer.h
 * @brief Handles Mesh Routing, Retries, ACKs, Neighbor Tables, and Sequence filtering.
 * @author Shahid  
 * @date Feb 2026
 */

#ifndef NETWORK_H
#define NETWORK_H

#include <stdint.h>
#include <stdbool.h>
#include "farmpulse_defs.h"

#define NEIGHBOR_TABLE_SIZE 12

typedef enum {
    NODE_DISCONNECTED = 0,
    NODE_CONNECTED = 1
} neighbor_status_t;

typedef struct {
    uint8_t node_id;
    neighbor_status_t status;
    int8_t rssi;
    uint32_t last_seen_ms;
    uint8_t rx_seq; // <--- NEW: Tracks the last Sequence Number received from this neighbor
} neighbor_entry_t;

// --- Function Prototypes ---
void network_init(void);
void send_ack(uint8_t target_node, uint8_t acked_seq_num);
void network_handle_packet(farm_packet_t *pkt, int8_t rssi);
bool network_send(uint8_t dest_id, packet_type_t type, uint8_t *payload, uint8_t len);

typedef void (*network_receive_cb_t)(uint8_t src_id, uint8_t type, uint8_t *data, uint8_t len);
void network_register_cb(network_receive_cb_t cb);

#endif // NETWORK_H