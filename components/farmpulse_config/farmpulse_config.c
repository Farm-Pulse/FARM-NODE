/**
 * @file farmpulse_config.c
 * @brief FARMPULSE configuration for device with signature implementations for security.
 * @author Shahid  
 * @date June 2026
 */

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "farmpulse_config.h"

static const char *TAG = "NVS_CONFIG";

// Allocate RAM for the system configuration
farmpulse_config_t system_config;

// --- INTERNAL: FACTORY RESET ---
// This writes fresh data to a brand new (or corrupted) chip.
static void factory_reset_nvs(nvs_handle_t handle) {
    ESP_LOGW(TAG, "Executing Factory Reset! Writing default values...");

    // Write Defaults to the NVS partition
    nvs_set_u16(handle, "SIG", CURRENT_SIGNATURE_BYTES);
    nvs_set_u8(handle, "NODE_ID", DEFAULT_NODE_ID);
    nvs_set_u8(handle, "NET_ID", DEFAULT_NETWORK_ID);
    
    // You MUST commit, or it stays in RAM and vanishes on reboot
    nvs_commit(handle);

    // Update our live system struct
    system_config.signature = CURRENT_SIGNATURE_BYTES;
    system_config.node_id = DEFAULT_NODE_ID;
    system_config.network_id = DEFAULT_NETWORK_ID;
    
    ESP_LOGI(TAG, "Factory Reset Complete. Node ID set to %d", system_config.node_id);
}

// --- MAIN BOOT LOGIC ---
esp_err_t farmpulse_config_init(void) {
    nvs_handle_t my_handle;
    esp_err_t err;

    // 1. Open the "FarmPulse" namespace. 
    // Namespaces keep your data separate from Wi-Fi or Bluetooth saved data.
    err = nvs_open("FarmPulse", NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS handle!");
        return err;
    }

    // 2. Look for our Secret Handshake (The Signature)
    uint16_t read_signature = 0;
    err = nvs_get_u16(my_handle, "SIG", &read_signature);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        // SCENARIO A: The key doesn't exist. This is a brand new board straight from the factory.
        ESP_LOGW(TAG, "No Signature found. Brand new board detected.");
        factory_reset_nvs(my_handle);
    } 
    else if (err == ESP_OK) {
        // SCENARIO B: We found a signature. Now we validate it.
        ESP_LOGI(TAG, "Found EEPROM Signature: 0x%04X", read_signature);

        // Check if the signature is our current one OR an older legacy one
        if (read_signature >= CURRENT_SIGNATURE_BYTES && read_signature <= LEGACY_SIGNATURE_MAX) {
            
            // The memory is safe! Extract the user's saved data.
            nvs_get_u8(my_handle, "NODE_ID", &system_config.node_id);
            nvs_get_u8(my_handle, "NET_ID", &system_config.network_id);
            system_config.signature = read_signature;

            ESP_LOGI(TAG, "Valid Configuration Loaded -> Node ID: %d", system_config.node_id);

            // --- THE LEGACY UPGRADE SAFETY NET ---
            // If the board had an older firmware signature (like 0x00AA), we rewrite it 
            // to the new standard (0x00A8) without deleting their NODE_ID.
            if (read_signature > CURRENT_SIGNATURE_BYTES) {
                ESP_LOGW(TAG, "Legacy Firmware detected. Upgrading signature gently...");
                nvs_set_u16(my_handle, "SIG", CURRENT_SIGNATURE_BYTES);
                nvs_commit(my_handle);
                system_config.signature = CURRENT_SIGNATURE_BYTES;
                ESP_LOGI(TAG, "Signature upgraded safely.");
            }
        } 
        else {
            // SCENARIO C: The signature is totally wrong (e.g., 0xFFFF). The flash memory got corrupted.
            ESP_LOGE(TAG, "FATAL: Invalid Signature (0x%04X)! Memory corrupted.", read_signature);
            factory_reset_nvs(my_handle);
        }
    }

    // 3. Close the handle to free up memory
    nvs_close(my_handle);
    return ESP_OK;
}

// --- HELPER: SAVE NEW ID ---
// Call this if you ever build a Bluetooth app or Serial CLI to assign Node IDs in the field.
esp_err_t farmpulse_save_node_id(uint8_t new_id) {
    nvs_handle_t my_handle;
    if (nvs_open("FarmPulse", NVS_READWRITE, &my_handle) == ESP_OK) {
        nvs_set_u8(my_handle, "NODE_ID", new_id);
        nvs_commit(my_handle);
        nvs_close(my_handle);
        system_config.node_id = new_id;
        ESP_LOGI(TAG, "Successfully updated NODE_ID to %d", new_id);
        return ESP_OK;
    }
    return ESP_FAIL;
}