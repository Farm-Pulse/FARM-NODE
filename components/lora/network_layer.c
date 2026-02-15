#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "farmpulse_defs.h"
#include "network_layer.h"
#include "mac_layer.h"

static const char *TAG = "NETWORK";

// --- Configuration ---
#define BROADCAST_ID 0xFF

// --- Internal State ---
static neighbor_entry_t neighbor_table[NEIGHBOR_TABLE_SIZE];
static uint8_t my_node_id = CONFIG_FARMPULSE_NODE_ID;
static uint8_t current_seq_num = 0;

// --- Helper: Get Timestamp ---
static uint32_t millis() {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

// --- Initialization ---
void network_init(void) {
    ESP_LOGI(TAG, "Initializing Network Layer. My ID: %d", my_node_id);
    
    // Clear Neighbor Table
    for (int i = 0; i < NEIGHBOR_TABLE_SIZE; i++) {
        neighbor_table[i].status = NODE_DISCONNECTED;
        neighbor_table[i].node_id = 0xFF; 
    }
    
    // STRATEGY: Default Neighbors (Linear Topology)
    // Assume the node mathematically "below" us exists so we can try to join immediately.
    if (my_node_id > 0) {
        // e.g. Node 1 assumes Node 0 exists.
        // We inject it with a fake RSSI so we have a route to start with.
        int idx = 0;
        neighbor_table[idx].node_id = my_node_id - 1;
        neighbor_table[idx].status = NODE_CONNECTED;
        neighbor_table[idx].rssi = -80; // Weak signal assumption
        neighbor_table[idx].last_seen_ms = millis();
        ESP_LOGI(TAG, "Added Default Neighbor: %d", neighbor_table[idx].node_id);
    }
}

// --- Helper: Find or Add Neighbor ---
static int find_neighbor_index(uint8_t id) {
    for (int i = 0; i < NEIGHBOR_TABLE_SIZE; i++) {
        if (neighbor_table[i].node_id == id) return i;
    }
    return -1;
}

static void update_neighbor(uint8_t id, int8_t rssi) {
    if (id == BROADCAST_ID) return; // Don't add broadcast ID to table

    int idx = find_neighbor_index(id);
    
    // If not found, look for empty slot
    if (idx == -1) {
        for (int i = 0; i < NEIGHBOR_TABLE_SIZE; i++) {
            if (neighbor_table[i].status == NODE_DISCONNECTED) {
                idx = i;
                neighbor_table[i].node_id = id;
                ESP_LOGI(TAG, "New Neighbor Discovered: Node %d (RSSI %d)", id, rssi);
                break;
            }
        }
    }
    
    // Update Stats
    if (idx != -1) {
        neighbor_table[idx].status = NODE_CONNECTED;
        neighbor_table[idx].rssi = rssi;
        neighbor_table[idx].last_seen_ms = millis();
    }
}

// --- The Hunting Algorithm ---
static uint8_t get_next_hop(uint8_t final_dest) {
    // 1. If Broadcast, Next Hop is Broadcast
    if (final_dest == BROADCAST_ID) {
        return BROADCAST_ID;
    }

    uint8_t best_candidate = 0xFF;

    // 2. Direct connection check
    int direct_idx = find_neighbor_index(final_dest);
    if (direct_idx != -1 && neighbor_table[direct_idx].status == NODE_CONNECTED) {
        return final_dest; // Send directly!
    }

    // 3. Routing Logic
    // If Dest > Me (Going Upstream)
    if (final_dest > my_node_id) {
        uint8_t max_id = 0;
        for (int i = 0; i < NEIGHBOR_TABLE_SIZE; i++) {
            if (neighbor_table[i].status == NODE_CONNECTED) {
                uint8_t nid = neighbor_table[i].node_id;
                if (nid > my_node_id && nid <= final_dest) {
                    if (nid > max_id) {
                        max_id = nid;
                        best_candidate = nid;
                    }
                }
            }
        }
    } 
    // If Dest < Me (Going Downstream / To Gateway)
    else {
        uint8_t min_id = 0xFF;
        for (int i = 0; i < NEIGHBOR_TABLE_SIZE; i++) {
            if (neighbor_table[i].status == NODE_CONNECTED) {
                uint8_t nid = neighbor_table[i].node_id;
                if (nid < my_node_id && nid >= final_dest) {
                    if (nid < min_id) {
                        min_id = nid;
                        best_candidate = nid;
                    }
                }
            }
        }
    }

    return best_candidate;
}

// --- Packet Handler (RX) ---
void network_handle_packet(farm_packet_t *pkt, int8_t rssi) {
    // 1. Snooping: Learn from everyone we hear
    update_neighbor(pkt->header.sender_id, rssi);

    // 2. Filter: Is this for Me OR Broadcast?
    if (pkt->header.target_id == my_node_id || pkt->header.target_id == BROADCAST_ID) {
        
        ESP_LOGI(TAG, "Packet Accepted from %d (RSSI: %d)", pkt->header.sender_id, rssi);
        
        // 3. Am I the Final Destination (or is it Broadcast)?
        if (pkt->header.final_dest_id == my_node_id || pkt->header.final_dest_id == BROADCAST_ID) {
            
            // Log the payload content for debugging
            ESP_LOGI(TAG, ">>> PAYLOAD RECEIVED: Type=0x%02X, Data=[%02X %02X %02X...]", 
                     (pkt->header.fcf & 0x0F), 
                     pkt->payload[0], pkt->payload[1], pkt->payload[2]);

            // If it was a Broadcast Heartbeat from Gateway, we now know Gateway exists!
            if (pkt->header.sender_id == 0) {
                 // update_neighbor already handled this above
            }
        } 
        else {
            // 4. Relay Logic (Only if not broadcast)
            if (pkt->header.hop_count > 0 && pkt->header.target_id != BROADCAST_ID) {
                ESP_LOGI(TAG, "Relaying packet to Final: %d", pkt->header.final_dest_id);
                
                uint8_t next_hop = get_next_hop(pkt->header.final_dest_id);
                
                if (next_hop != 0xFF) {
                    pkt->header.target_id = next_hop;
                    pkt->header.sender_id = my_node_id;
                    pkt->header.hop_count--;
                    mac_tx(pkt);
                }
            }
        }
    }
}

// --- Send Function (TX) ---
bool network_send(uint8_t dest_id, packet_type_t type, uint8_t *payload, uint8_t len) {
    farm_packet_t pkt;
    
    // Find Next Hop
    uint8_t next_hop = get_next_hop(dest_id);
    if (next_hop == 0xFF) {
        ESP_LOGE(TAG, "Cannot Send: No Route to %d", dest_id);
        return false;
    }

    // Prepare Header
    pkt.header.length = 10 + len;
    pkt.header.target_id = next_hop;
    pkt.header.sender_id = my_node_id;
    pkt.header.network_id = CONFIG_FARMPULSE_NETWORK_ID;
    pkt.header.seq_num = current_seq_num++;
    pkt.header.fcf = (type & 0x0F); 
    pkt.header.hop_count = CONFIG_MESH_MAX_HOPS;
    pkt.header.final_dest_id = dest_id;
    pkt.header.origin_src_id = my_node_id;
    
    // Copy Payload
    memcpy(pkt.payload, payload, len);

    // Send
    return mac_tx(&pkt);
}