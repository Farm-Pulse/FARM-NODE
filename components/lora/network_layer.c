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

// --- EmSave Thresholds ---
#define RSSI_THRESHOLD_PEER   -90  // Ignore LCs weaker than -90 dBm
#define RSSI_THRESHOLD_MASTER -80  // Ignore Master weaker than -80 dBm

static neighbor_entry_t neighbor_table[NEIGHBOR_TABLE_SIZE];
static uint8_t my_node_id = CONFIG_FARMPULSE_NODE_ID;
static uint8_t current_seq_num = 1; // EmSave standard starts at 1

static SemaphoreHandle_t ack_wait_sem = NULL;
static SemaphoreHandle_t tx_pipeline_mutex = NULL; 
static uint8_t expected_ack_seq = 0;
static uint8_t expected_ack_from = 0;
static bool is_waiting_for_ack = false;

static network_receive_cb_t app_rx_cb = NULL;

void network_register_cb(network_receive_cb_t cb) { app_rx_cb = cb; }

static uint32_t millis() { return (uint32_t)(esp_timer_get_time() / 1000); }

void network_init(void) {
    ESP_LOGI(TAG, "Initializing EmSave Network Layer. My ID: %d", my_node_id);
    ack_wait_sem = xSemaphoreCreateBinary();
    tx_pipeline_mutex = xSemaphoreCreateMutex();
    
    for (int i = 0; i < NEIGHBOR_TABLE_SIZE; i++) {
        neighbor_table[i].status = NODE_DISCONNECTED;
        neighbor_table[i].node_id = 0xFF; 
        neighbor_table[i].rx_seq = 0; // Initialize sequence memory to 0
    }
}

static int find_neighbor_index(uint8_t id) {
    for (int i = 0; i < NEIGHBOR_TABLE_SIZE; i++) {
        if (neighbor_table[i].status == NODE_CONNECTED && neighbor_table[i].node_id == id) return i;
    }
    return -1;
}

static void update_neighbor(uint8_t id, int8_t rssi, uint8_t rx_seq) {
    if (id == BROADCAST_ID) return;

    // --- EmSave RSSI Threshold Logic ---
    if (id == 0 && rssi < RSSI_THRESHOLD_MASTER) {
        ESP_LOGD(TAG, "Rejected Master (ID 0) due to weak RSSI: %d", rssi);
        return;
    }
    if (id != 0 && rssi < RSSI_THRESHOLD_PEER) {
        ESP_LOGD(TAG, "Rejected Peer (ID %d) due to weak RSSI: %d", id, rssi);
        return;
    }

    int idx = find_neighbor_index(id);
    
    // Add new neighbor if not found
    if (idx == -1) {
        for (int i = 0; i < NEIGHBOR_TABLE_SIZE; i++) {
            if (neighbor_table[i].status == NODE_DISCONNECTED) {
                idx = i;
                neighbor_table[i].node_id = id;
                ESP_LOGW(TAG, "*** New Neighbor Linked: Node %d (RSSI %d) ***", id, rssi);
                break;
            }
        }
    }
    
    // Update stats
    if (idx != -1) {
        neighbor_table[idx].status = NODE_CONNECTED;
        neighbor_table[idx].rssi = rssi;
        neighbor_table[idx].last_seen_ms = millis();
        neighbor_table[idx].rx_seq = rx_seq; // Update sequence memory
    }
}

// --- TRUE EMSAVE HUNTING ALGORITHM ---
static uint8_t get_next_hop(uint8_t final_dest) {
    if (final_dest == BROADCAST_ID) return BROADCAST_ID;

    // 1. Context 1: Direct Connection Priority
    int direct_idx = find_neighbor_index(final_dest);
    if (direct_idx != -1 && neighbor_table[direct_idx].status == NODE_CONNECTED) {
        return final_dest; 
    }

    // 2. Context 3: Linear Hopping
    uint8_t best_candidate = 0xFF;

    if (final_dest > my_node_id) { 
        // Upstream: Find HIGHEST neighbor ID that is <= Destination
        uint8_t highest_valid = 0;
        bool found = false;
        for (int i = 0; i < NEIGHBOR_TABLE_SIZE; i++) {
            if (neighbor_table[i].status == NODE_CONNECTED) {
                uint8_t nid = neighbor_table[i].node_id;
                if (nid > my_node_id && nid <= final_dest) {
                    if (!found || nid > highest_valid) {
                        highest_valid = nid;
                        best_candidate = nid;
                        found = true;
                    }
                }
            }
        }
    } else { 
        // Downstream: Find LOWEST neighbor ID that is >= Destination
        uint8_t lowest_valid = 0xFF;
        bool found = false;
        for (int i = 0; i < NEIGHBOR_TABLE_SIZE; i++) {
            if (neighbor_table[i].status == NODE_CONNECTED) {
                uint8_t nid = neighbor_table[i].node_id;
                if (nid < my_node_id && nid >= final_dest) {
                    if (!found || nid < lowest_valid) {
                        lowest_valid = nid;
                        best_candidate = nid;
                        found = true;
                    }
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
    mac_tx(&ack_pkt);
}

void network_handle_packet(farm_packet_t *pkt, int8_t rssi) {
    // 1. EmSave RSSI Thresholding
    if (pkt->header.sender_id == 0 && rssi < RSSI_THRESHOLD_MASTER) return;
    if (pkt->header.sender_id != 0 && rssi < RSSI_THRESHOLD_PEER) return;

    // 2. EMSAVE DUPLICATE REJECTION LOGIC
    int idx = find_neighbor_index(pkt->header.sender_id);
    
    if (idx != -1 && neighbor_table[idx].rx_seq == pkt->header.seq_num) {
        ESP_LOGD(TAG, "Duplicate frame rejected (Seq: %d, From: %d)", pkt->header.seq_num, pkt->header.sender_id);
        
        // If it's a duplicate, it means our previous ACK was lost. Resend the ACK to shut them up!
        uint8_t pkt_type = pkt->header.fcf & 0x0F;
        if ((pkt->header.fcf & FCF_MASK_ACK_REQ) && pkt->header.target_id != BROADCAST_ID && pkt_type != PKT_TYPE_ACK) {
            send_ack(pkt->header.sender_id, pkt->header.seq_num);
        }
        return; // Halt processing
    }

    // 3. Learn from the packet (It is fresh data!)
    update_neighbor(pkt->header.sender_id, rssi, pkt->header.seq_num);

    // 4. Filter: Is this packet addressed to me, or broadcast?
    if (pkt->header.target_id == my_node_id || pkt->header.target_id == BROADCAST_ID) {
        
        uint8_t pkt_type = pkt->header.fcf & 0x0F;

        // Handle ACKs
        if (pkt_type == PKT_TYPE_ACK) {
            ESP_LOGI(TAG, ">> ACK RECEIVED from Node %d for Seq: %d", pkt->header.sender_id, pkt->payload[0]);
            if (is_waiting_for_ack && pkt->header.sender_id == expected_ack_from && pkt->payload[0] == expected_ack_seq) {
                xSemaphoreGive(ack_wait_sem);
            }
            return; 
        }

        // Send Hop-by-Hop ACK
        if ((pkt->header.fcf & FCF_MASK_ACK_REQ) && pkt->header.target_id != BROADCAST_ID) {
            send_ack(pkt->header.sender_id, pkt->header.seq_num);
        }

        // 5. FINAL DESTINATION
        if (pkt->header.final_dest_id == my_node_id || pkt->header.final_dest_id == BROADCAST_ID) {
            ESP_LOGD(TAG, "Packet Accepted (Type: 0x%02X)", pkt_type);
            if (app_rx_cb != NULL) {
                app_rx_cb(pkt->header.origin_src_id, pkt_type, pkt->payload, pkt->header.length - 10);
            }
        } 
        // 6. MESH RELAY
        // 6. MESH RELAY
        else {
            if (pkt->header.hop_count > 0 && pkt->header.target_id != BROADCAST_ID) {
                uint8_t next_hop = get_next_hop(pkt->header.final_dest_id);
                if (next_hop != 0xFF) {
                    ESP_LOGW(TAG, "=================================================");
                    ESP_LOGW(TAG, "  INTERCEPTED PACKET - ACTING AS MESH RELAY");
                    ESP_LOGW(TAG, "  Origin: Node %d  --->  Final Dest: Node %d", pkt->header.origin_src_id, pkt->header.final_dest_id);
                    ESP_LOGW(TAG, "  Routing through Next Hop: Node %d", next_hop);
                    ESP_LOGW(TAG, "=================================================");
                             
                    pkt->header.target_id = next_hop;
                    pkt->header.sender_id = my_node_id; 
                    
                    // --- THE CRITICAL BUG FIX ---
                    // We MUST assign a new sequence number from our own pool so the 
                    // receiver doesn't mistake this for a duplicate of our own packets!
                    pkt->header.seq_num = current_seq_num++; 
                    // ----------------------------

                    pkt->header.hop_count--;
                    mac_tx(pkt); 
                } else {
                    ESP_LOGE(TAG, "Relay Dropped: No route to %d", pkt->header.final_dest_id);
                }
            }
        }
    }
}

bool network_send(uint8_t dest_id, packet_type_t type, uint8_t *payload, uint8_t len) {
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

    pkt.header.hop_count = CONFIG_MESH_MAX_HOPS; 
    pkt.header.final_dest_id = dest_id;
    pkt.header.origin_src_id = my_node_id;
    memcpy(pkt.payload, payload, len);

    bool requires_ack = (pkt.header.fcf & FCF_MASK_ACK_REQ);
    bool tx_success = false;
    uint8_t retries = CONFIG_MESH_MAX_RETRIES;

    if (requires_ack) {
        expected_ack_seq = pkt.header.seq_num;
        expected_ack_from = next_hop;
        is_waiting_for_ack = true;
        xSemaphoreTake(ack_wait_sem, 0); 
    }

    while (retries > 0) {
        mac_tx(&pkt);
        if (!requires_ack) { tx_success = true; break; }

        if (xSemaphoreTake(ack_wait_sem, pdMS_TO_TICKS(CONFIG_MESH_ACK_TIMEOUT_MS)) == pdTRUE) {
            tx_success = true;
            break;
        } else {
            retries--;
            if (retries > 0) ESP_LOGW(TAG, "No ACK from Node %d. Retrying...", next_hop);
        }
    }

    is_waiting_for_ack = false;
    if (!tx_success && requires_ack) {
        int idx = find_neighbor_index(next_hop);
        if (idx != -1) neighbor_table[idx].status = NODE_DISCONNECTED;
        // NOTE: In the next step, we will notify the Gateway here as per Declare_Dead.md
    }

    xSemaphoreGive(tx_pipeline_mutex);
    return tx_success;
}