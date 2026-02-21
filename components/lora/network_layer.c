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

static uint8_t get_next_hop(uint8_t final_dest) {
    if (final_dest == BROADCAST_ID) return BROADCAST_ID;

    int direct_idx = find_neighbor_index(final_dest);
    if (direct_idx != -1 && neighbor_table[direct_idx].status == NODE_CONNECTED) {
        return final_dest; 
    }

    uint8_t best_candidate = 0xFF;

    if (final_dest > my_node_id) { // Upstream
        uint8_t max_id = 0;
        for (int i = 0; i < NEIGHBOR_TABLE_SIZE; i++) {
            if (neighbor_table[i].status == NODE_CONNECTED) {
                uint8_t nid = neighbor_table[i].node_id;
                if (nid > my_node_id && nid <= final_dest) {
                    if (nid > max_id) { max_id = nid; best_candidate = nid; }
                }
            }
        }
    } else { // Downstream
        uint8_t min_id = 0xFF;
        for (int i = 0; i < NEIGHBOR_TABLE_SIZE; i++) {
            if (neighbor_table[i].status == NODE_CONNECTED) {
                uint8_t nid = neighbor_table[i].node_id;
                if (nid < my_node_id && nid >= final_dest) {
                    if (nid < min_id) { min_id = nid; best_candidate = nid; }
                }
            }
        }
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

void network_handle_packet(farm_packet_t *pkt, int8_t rssi) {
    update_neighbor(pkt->header.sender_id, rssi);

    if (pkt->header.target_id == my_node_id || pkt->header.target_id == BROADCAST_ID) {
        uint8_t pkt_type = pkt->header.fcf & 0x0F;

        // --- NEW: ACK CHECKING LOGIC ---
        if (pkt_type == PKT_TYPE_ACK) {
            ESP_LOGI(TAG, ">> ACK RECEIVED from Node %d for Seq: %d", 
                     pkt->header.sender_id, pkt->payload[0]);
            
            // If this is the ACK we are actively waiting for, unlock the TX task!
            if (is_waiting_for_ack && 
                pkt->header.sender_id == expected_ack_from && 
                pkt->payload[0] == expected_ack_seq) {
                xSemaphoreGive(ack_wait_sem);
            }
            return; 
        }

        // --- Data/Cmd Logic ---
        ESP_LOGD(TAG, "Packet Accepted (Type: 0x%02X)", pkt_type);

        if ((pkt->header.fcf & FCF_MASK_ACK_REQ) && pkt->header.target_id != BROADCAST_ID) {
            send_ack(pkt->header.sender_id, pkt->header.seq_num);
        }

        if (pkt->header.final_dest_id == my_node_id || pkt->header.final_dest_id == BROADCAST_ID) {
            if (app_rx_cb != NULL) {
                app_rx_cb(pkt->header.origin_src_id, pkt_type, pkt->payload, pkt->header.length - 10);
            }
        } else {
            // Relay (Will implement Hop-by-Hop retry for relays in Phase 5)
            if (pkt->header.hop_count > 0 && pkt->header.target_id != BROADCAST_ID) {
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