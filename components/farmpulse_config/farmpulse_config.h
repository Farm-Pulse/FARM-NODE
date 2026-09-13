/**
 * @file farmpulse_config.h
 * @brief FARMPULSE configuration for device with signature implementations for security.
 * @author Shahid  
 * @date June 2026
 */

 #ifndef FARMPULSE_CONFIG_H
#define FARMPULSE_CONFIG_H

#include <stdint.h>
#include "esp_err.h"

// --- THE SIGNATURE DEFINITIONS ---
#define CURRENT_SIGNATURE_BYTES   0x00A8
#define LEGACY_SIGNATURE_MAX      0x00AB

// --- DEFAULT FACTORY VALUES ---
// If a board has blank memory, it defaults to these:
#define DEFAULT_NODE_ID           1
#define DEFAULT_NETWORK_ID        0x1A // E.g., Farm ID 26

// Global Configuration Struct mapped to RAM
typedef struct {
    uint16_t signature;
    uint8_t  node_id;
    uint8_t  network_id;
} farmpulse_config_t;

// Make this struct accessible to main.c
extern farmpulse_config_t system_config;

// Initialize NVS and check the signature
esp_err_t farmpulse_config_init(void);

// Helper function to save a new Node ID (useful for technician setups)
esp_err_t farmpulse_save_node_id(uint8_t new_id);

#endif // FARMPULSE_CONFIG_H