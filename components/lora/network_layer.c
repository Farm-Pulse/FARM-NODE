#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "farmpulse_defs.h"
#include "network_layer.h"
#include "mac_layer.h"

static const char *TAG = "NETWORK";

#define BROADCAST_ID 0xFF

// --- Internal State ---
static neighbor_entry_t neighbor_table[NEIGHBOR_TABLE_SIZE];
static uint8_t my_node_id = CONFIG_FARMPULSE_NODE_ID;
static uint8_t current_seq_num = 0;

// --- Reliability State (Phase 4 Additions) ---
static SemaphoreHandle_t ack_wait_sem = NULL;
static SemaphoreHandle_t tx_pipeline_mutex = NULL; // Prevents multiple tasks from sending at once
static uint8_t expected_ack_seq = 0;
static uint8_t expected_ack_from = 0;
static bool is_waiting_for_ack = false;

// --- Application Callback Pointer ---
static network_receive_cb_t app_rx_cb = NULL;

void network_register_cb(network_receive_cb_t cb) {
    app_rx_cb = cb;
}

static uint32_t millis() {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

void network_init(void) {
    ESP_LOGI(TAG, "Initializing Network Layer (Phase 4). My ID: %d", my_node_id);
    
    // Create Synchronization Objects
    ack_wait_sem = xSemaphoreCreateBinary();
    tx_pipeline_mutex = xSemaphoreCreateMutex();
    
    for (int i = 0; i < NEIGHBOR_TABLE_SIZE; i++) {
        neighbor_table[i].status = NODE_DISCONNECTED;
        neighbor_table[i].node_id = 0xFF; 
    }
    
    // Default Neighbor Strategy
    if (my_node_id > 0) {
        neighbor_table[0].node_id = my_node_id - 1;
        neighbor_table[0].status = NODE_CONNECTED;
        neighbor_table[0].rssi = -80; 
        neighbor_table[0].last_seen_ms = millis();
        ESP_LOGI(TAG, "Added Default Neighbor: %d", neighbor_table[0].node_id);
    }
}

static int find_neighbor_index(uint8_t id) {
    for (int i = 0; i < NEIGHBOR_TABLE_SIZE; i++) {
        if (neighbor_table[i].node_id == id) return i;
    }
    return -1;
}

static void update_neighbor(uint8_t id, int8_t rssi) {
    if (id == BROADCAST_ID) return;

    int idx = find_neighbor_index(id);
    
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
    
    if (idx != -1) {
        neighbor_table[idx].status = NODE_CONNECTED;
        neighbor_table[idx].rssi = rssi;
        neighbor_table[idx].last_seen_ms = millis();
    }
}

//Temporary blinfolding the Node-2
// static void update_neighbor(uint8_t id, int8_t rssi) {
//     if (id == BROADCAST_ID) return;

//     // --- NEW: THE SOFTWARE BLINDFOLD (For Testing Node 2 Only) ---
//     // If I am Node 2, pretend I can NEVER hear Node 0 directly.
//     if (my_node_id == 2 && id == 0) {
//         return; 
//     }
//     // -------------------------------------------------------------

//     int idx = find_neighbor_index(id);
//     if (idx == -1) {
//         for (int i = 0; i < NEIGHBOR_TABLE_SIZE; i++) {
//             if (neighbor_table[i].status == NODE_DISCONNECTED) {
//                 idx = i;
//                 neighbor_table[i].node_id = id;
//                 ESP_LOGW(TAG, "*** New Neighbor Linked: Node %d (RSSI %d) ***", id, rssi);
//                 break;
//             }
//         }
//     }
    
//     if (idx != -1) {
//         neighbor_table[idx].status = NODE_CONNECTED;
//         neighbor_table[idx].rssi = rssi;
//         neighbor_table[idx].last_seen_ms = millis();
//     }
// }


// --- HYBRID ROUTING ALGORITHM (Industry Standard) ---
static uint8_t get_next_hop(uint8_t final_dest) {
    if (final_dest == BROADCAST_ID) return BROADCAST_ID;

    // 1. DIRECT CONNECTION PRIORITY
    int direct_idx = find_neighbor_index(final_dest);
    if (direct_idx != -1 && neighbor_table[direct_idx].status == NODE_CONNECTED) {
        return final_dest; 
    }

    // 2. LINEAR HOPPING
    uint8_t best_candidate = 0xFF;

    if (final_dest > my_node_id) { 
        // I need to send UP the chain. 
        // Find the SMALLEST neighbor ID that is GREATER than me.
        uint8_t closest_above = 0xFF;
        for (int i = 0; i < NEIGHBOR_TABLE_SIZE; i++) {
            if (neighbor_table[i].status == NODE_CONNECTED) {
                uint8_t nid = neighbor_table[i].node_id;
                if (nid > my_node_id && nid < closest_above) {
                    closest_above = nid;
                    best_candidate = nid;
                }
            }
        }
    } else { 
        // I need to send DOWN the chain (towards Gateway 0).
        // Find the LARGEST neighbor ID that is LESS than me.
        uint8_t closest_below = 0;
        bool found = false;
        for (int i = 0; i < NEIGHBOR_TABLE_SIZE; i++) {
            if (neighbor_table[i].status == NODE_CONNECTED) {
                uint8_t nid = neighbor_table[i].node_id;
                if (nid < my_node_id && nid >= closest_below) {
                    closest_below = nid;
                    best_candidate = nid;
                    found = true;
                }
            }
        }
        if(!found) best_candidate = 0xFF;
    }
    
    return best_candidate;
}

void send_ack(uint8_t target_node, uint8_t acked_seq_num) {
    farm_packet_t ack_pkt;
    ack_pkt.header.target_id = target_node; 
    ack_pkt.header.sender_id = my_node_id;
    ack_pkt.header.network_id = CONFIG_FARMPULSE_NETWORK_ID;
    ack_pkt.header.seq_num = current_seq_num++; 
    ack_pkt.header.fcf = PKT_TYPE_ACK; 
    ack_pkt.header.hop_count = 1; 
    ack_pkt.header.final_dest_id = target_node;
    ack_pkt.header.origin_src_id = my_node_id;
    ack_pkt.payload[0] = acked_seq_num;
    ack_pkt.header.length = 10 + 1; 

    // Send immediately
    mac_tx(&ack_pkt);
}

// --- Packet Handler (RX) ---
void network_handle_packet(farm_packet_t *pkt, int8_t rssi) {
    // 1. Learn from the packet (Snooping)
    update_neighbor(pkt->header.sender_id, rssi);

    // 2. Filter: Is this packet addressed to my MAC address, or is it a broadcast?
    if (pkt->header.target_id == my_node_id || pkt->header.target_id == BROADCAST_ID) {
        
        uint8_t pkt_type = pkt->header.fcf & 0x0F;

        // Handle ACKs first
        if (pkt_type == PKT_TYPE_ACK) {
            ESP_LOGI(TAG, ">> ACK RECEIVED from Node %d for Seq: %d", 
                     pkt->header.sender_id, pkt->payload[0]);
            
            if (is_waiting_for_ack && 
                pkt->header.sender_id == expected_ack_from && 
                pkt->payload[0] == expected_ack_seq) {
                xSemaphoreGive(ack_wait_sem);
            }
            return; 
        }

        // Send Hop-by-Hop ACK back to the immediate sender
        if ((pkt->header.fcf & FCF_MASK_ACK_REQ) && pkt->header.target_id != BROADCAST_ID) {
            send_ack(pkt->header.sender_id, pkt->header.seq_num);
        }

        // 3. AM I THE FINAL DESTINATION?
        if (pkt->header.final_dest_id == my_node_id || pkt->header.final_dest_id == BROADCAST_ID) {
            ESP_LOGD(TAG, "Packet Accepted (Type: 0x%02X)", pkt_type);
            // Hand over to App Layer
            if (app_rx_cb != NULL) {
                app_rx_cb(pkt->header.origin_src_id, pkt_type, pkt->payload, pkt->header.length - 10);
            }
        } 
        
        // 4. I AM A RELAY! (Transparent Bridging)
        else {
            if (pkt->header.hop_count > 0 && pkt->header.target_id != BROADCAST_ID) {
                
                // Ask the routing algorithm for the next step
                uint8_t next_hop = get_next_hop(pkt->header.final_dest_id);
                
                if (next_hop != 0xFF) {
                    // --- EXPLICIT OBSERVABILITY LOGGING ---
                    ESP_LOGW(TAG, "=================================================");
                    ESP_LOGW(TAG, "  INTERCEPTED PACKET - ACTING AS MESH RELAY");
                    ESP_LOGW(TAG, "  Type: 0x%02X | Hop Count Left: %d", pkt_type, pkt->header.hop_count);
                    ESP_LOGW(TAG, "  Origin: Node %d  --->  Final Dest: Node %d", 
                             pkt->header.origin_src_id, pkt->header.final_dest_id);
                    ESP_LOGW(TAG, "  Routing through Next Hop: Node %d", next_hop);
                    ESP_LOGW(TAG, "=================================================");
                             
                    // Update header for the next hop
                    pkt->header.target_id = next_hop;
                    pkt->header.sender_id = my_node_id; // I am the new immediate sender
                    pkt->header.hop_count--;
                    
                    // Transmit the relayed packet
                    mac_tx(pkt); 
                } else {
                    ESP_LOGE(TAG, "Relay Dropped: No valid route found to Node %d", pkt->header.final_dest_id);
                }
            } else if (pkt->header.hop_count == 0) {
                ESP_LOGE(TAG, "Relay Dropped: Hop count reached 0 (Infinite Loop Prevented)");
            }
        }
    }
}

// --- Send Function with Auto-Retry & Self Healing ---
bool network_send(uint8_t dest_id, packet_type_t type, uint8_t *payload, uint8_t len) {
    // Lock the pipeline so multiple app tasks don't mix up the ACK waiting
    xSemaphoreTake(tx_pipeline_mutex, portMAX_DELAY);
    
    farm_packet_t pkt;
    uint8_t next_hop;

    if (dest_id == BROADCAST_ID) {
        next_hop = BROADCAST_ID;
    } else {
        next_hop = get_next_hop(dest_id);
        if (next_hop == 0xFF) {
            ESP_LOGE(TAG, "Cannot Send: No Route to %d", dest_id);
            xSemaphoreGive(tx_pipeline_mutex);
            return false;
        }
    }

    pkt.header.length = 10 + len;
    pkt.header.target_id = next_hop;
    pkt.header.sender_id = my_node_id;
    pkt.header.network_id = CONFIG_FARMPULSE_NETWORK_ID;
    pkt.header.seq_num = current_seq_num++;
    
    pkt.header.fcf = (type & 0x0F);
    if (dest_id != BROADCAST_ID && type != PKT_TYPE_ACK) {
        pkt.header.fcf |= FCF_MASK_ACK_REQ; 
    }

    pkt.header.hop_count = CONFIG_MESH_MAX_HOPS; // e.g. 10
    pkt.header.final_dest_id = dest_id;
    pkt.header.origin_src_id = my_node_id;
    memcpy(pkt.payload, payload, len);

    // --- RETRY LOOP EXECUTION ---
    bool requires_ack = (pkt.header.fcf & FCF_MASK_ACK_REQ);
    bool tx_success = false;
    uint8_t retries = CONFIG_MESH_MAX_RETRIES; // e.g. 3

    if (requires_ack) {
        expected_ack_seq = pkt.header.seq_num;
        expected_ack_from = next_hop;
        is_waiting_for_ack = true;
        xSemaphoreTake(ack_wait_sem, 0); // Clear any old stale acks
    }

    while (retries > 0) {
        // Step 1: Send the raw packet to the air
        mac_tx(&pkt);
        
        // If it's a broadcast, we don't expect an ACK, so we are done!
        if (!requires_ack) {
            tx_success = true;
            break;
        }

        // Step 2: Block and wait for the RX Task to give us the ACK semaphore
        // We wait for the specific Timeout duration defined in menuconfig
        if (xSemaphoreTake(ack_wait_sem, pdMS_TO_TICKS(CONFIG_MESH_ACK_TIMEOUT_MS)) == pdTRUE) {
            // We got the ACK!
            tx_success = true;
            break;
        } else {
            // Step 3: Timeout! Decrement retry count.
            retries--;
            if (retries > 0) {
                ESP_LOGW(TAG, "No ACK from Node %d. Retrying... (%d attempts left)", next_hop, retries);
                // Important: We increment the sequence number on a retry to avoid duplicate rejection
                // (though in some protocols retries keep the same seq, incrementing is safer here)
                // We'll keep the same seq num so the receiver knows it's a retry.
            }
        }
    }

    // Clean up state
    is_waiting_for_ack = false;

    // --- STEP 4: SELF HEALING (DEAD NODE PURGING) ---
    if (!tx_success && requires_ack) {
        ESP_LOGE(TAG, "CRITICAL: Max retries reached! Node %d is unresponsive.", next_hop);
        
        // Mark the node as disconnected in the routing table.
        // The NEXT time this function is called, get_next_hop() will ignore this node
        // and find an alternative path. This is the core of Mesh Self-Healing.
        int idx = find_neighbor_index(next_hop);
        if (idx != -1) {
            neighbor_table[idx].status = NODE_DISCONNECTED;
            ESP_LOGW(TAG, "Route to Node %d purged from Neighbor Table.", next_hop);
        }
    }

    xSemaphoreGive(tx_pipeline_mutex);
    return tx_success;
}