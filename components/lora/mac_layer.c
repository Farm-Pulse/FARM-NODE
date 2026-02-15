#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_system.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"

// Include the Library Header
#include "sx127x.h"

// Include Our Definitions
#include "farmpulse_defs.h"
#include "network_layer.h"
#include "mac_layer.h"

static const char *TAG = "MAC_LAYER";

// --- LILYGO T3S3 v1.2 Hardware Definitions ---
#define PIN_SCK     5
#define PIN_MISO    3
#define PIN_MOSI    6
#define PIN_SS      7
#define PIN_DIO0    9
#define PIN_RST     8

// --- Internal State ---
static sx127x lora_device; 
static spi_device_handle_t spi_handle = NULL;
static QueueHandle_t mac_rx_queue = NULL;
static TaskHandle_t interrupt_task_handle = NULL;
static SemaphoreHandle_t tx_done_sem = NULL; 

// --- Forward Declarations ---
void mac_rx_callback(void *ctx, uint8_t *data, uint16_t len);
void interrupt_task(void *arg);
void lora_interrupt_handler(void *arg);
void lora_rx_task(void *arg);

/**
 * @brief Initialize the LoRa Hardware (PHY Layer)
 */
void mac_init(void) {
    ESP_LOGI(TAG, "Initializing LoRa MAC Layer...");

    // 1. HARDWARE RESET
    gpio_set_direction(PIN_RST, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_RST, 0); 
    vTaskDelay(pdMS_TO_TICKS(5)); 
    gpio_set_level(PIN_RST, 1); 
    vTaskDelay(pdMS_TO_TICKS(10)); 
    ESP_LOGI(TAG, "Hardware Reset Complete.");

    // 2. Create Queues & Semaphores
    mac_rx_queue = xQueueCreate(10, sizeof(farm_packet_t));
    tx_done_sem = xSemaphoreCreateBinary();

    // 3. SPI Bus Initialization (Matches esp_util.c)
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_MOSI,
        .miso_io_num = PIN_MISO,
        .sclk_io_num = PIN_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 0
    };
    // Note: Using DMA_AUTO as per your working example
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

    // 4. Add SPI Device
    // CRITICAL FIX: Added .address_bits = 8 to match your working code
    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = 4000000, // 4 MHz
        .mode = 0,
        .spics_io_num = PIN_SS,
        .queue_size = 16,
        .command_bits = 0,
        .address_bits = 8, // <--- THIS WAS THE MISSING KEY
        .dummy_bits = 0,
        .flags = 0
    };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &dev_cfg, &spi_handle));

    // 5. Initialize LoRa Struct
    ESP_ERROR_CHECK(sx127x_create(spi_handle, &lora_device));
    ESP_LOGI(TAG, "SX1276 Driver Created.");
    
    // 6. Configure LoRa Radio Parameters
    ESP_ERROR_CHECK(sx127x_set_opmod(SX127X_MODE_SLEEP, SX127X_MODULATION_LORA, &lora_device));
    ESP_ERROR_CHECK(sx127x_set_frequency(865000000, &lora_device));
    ESP_ERROR_CHECK(sx127x_lora_reset_fifo(&lora_device));
    
    // LNA & Power Settings
    ESP_ERROR_CHECK(sx127x_rx_set_lna_boost_hf(true, &lora_device));
    ESP_ERROR_CHECK(sx127x_rx_set_lna_gain(SX127X_LNA_GAIN_G1, &lora_device));
    ESP_ERROR_CHECK(sx127x_tx_set_pa_config(SX127X_PA_PIN_BOOST, 14, &lora_device));
    
    // Signal Parameters
    ESP_ERROR_CHECK(sx127x_lora_set_bandwidth(SX127X_BW_125000, &lora_device));
    ESP_ERROR_CHECK(sx127x_lora_set_spreading_factor(SX127X_SF_9, &lora_device));
    
    // Header & CRC
    sx127x_tx_header_t header = {
        .enable_crc = true,
        .coding_rate = SX127X_CR_4_5
    };
    ESP_ERROR_CHECK(sx127x_lora_tx_set_explicit_header(&header, &lora_device));

    // 7. Setup Callback
    sx127x_rx_set_callback(mac_rx_callback, &lora_device, &lora_device);

    // 8. Setup Interrupt Handling
    xTaskCreate(interrupt_task, "lora_irq", 4096, NULL, 20, &interrupt_task_handle);

    gpio_install_isr_service(0);
    gpio_set_direction(PIN_DIO0, GPIO_MODE_INPUT);
    gpio_set_pull_mode(PIN_DIO0, GPIO_PULLUP_ONLY);
    gpio_set_intr_type(PIN_DIO0, GPIO_INTR_POSEDGE);
    gpio_isr_handler_add(PIN_DIO0, lora_interrupt_handler, NULL);

    // 9. Start RX Consumer Task
    xTaskCreate(lora_rx_task, "lora_rx_task", 4096, NULL, 10, NULL);

    // Final Step: Set to RX Continuous
    ESP_ERROR_CHECK(sx127x_set_opmod(SX127X_MODE_RX_CONT, SX127X_MODULATION_LORA, &lora_device));
    
    ESP_LOGI(TAG, "LoRa Initialized (865MHz, SF9).");
}

/**
 * @brief Send a Packet (Interrupt Driven)
 */
bool mac_tx(farm_packet_t *packet) {
    uint8_t total_len = sizeof(farm_header_t) + packet->header.length - 10; 
    
    ESP_LOGI(TAG, "TX -> NextHop: %d", packet->header.target_id);
    
    // 1. Prepare Packet
    ESP_ERROR_CHECK(sx127x_lora_tx_set_for_transmission((uint8_t *)packet, total_len, &lora_device));
    
    // 2. Trigger TX
    ESP_ERROR_CHECK(sx127x_set_opmod(SX127X_MODE_TX, SX127X_MODULATION_LORA, &lora_device));
    
    // 3. WAIT for Interrupt
    if (xSemaphoreTake(tx_done_sem, pdMS_TO_TICKS(1000)) == pdTRUE) {
        ESP_LOGI(TAG, "TX Complete");
    } else {
        ESP_LOGE(TAG, "TX Timeout");
        sx127x_set_opmod(SX127X_MODE_STANDBY, SX127X_MODULATION_LORA, &lora_device);
        sx127x_set_opmod(SX127X_MODE_RX_CONT, SX127X_MODULATION_LORA, &lora_device);
        return false;
    }

    // 4. Return to RX
    ESP_ERROR_CHECK(sx127x_set_opmod(SX127X_MODE_RX_CONT, SX127X_MODULATION_LORA, &lora_device));
    
    return true;
}

/**
 * @brief Raw Hardware ISR
 */
void IRAM_ATTR lora_interrupt_handler(void *arg) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (interrupt_task_handle != NULL) {
        vTaskNotifyGiveFromISR(interrupt_task_handle, &xHigherPriorityTaskWoken);
    }
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/**
 * @brief Interrupt Bridge Task
 */
void interrupt_task(void *arg) {
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        sx127x_handle_interrupt(&lora_device);
    }
}

/**
 * @brief Universal Callback (RX and TX)
 */
void mac_rx_callback(void *ctx, uint8_t *data, uint16_t len) {
    // Scenario A: RX Packet Received
    if (len > 0 && data != NULL) {
        if (len < sizeof(farm_header_t) || len > sizeof(farm_packet_t)) return;
        
        farm_packet_t pkt;
        memcpy(&pkt, data, len);
        xQueueSend(mac_rx_queue, &pkt, 0);
    }
    // Scenario B: TX Done Event
    else {
        xSemaphoreGive(tx_done_sem);
    }
}

/**
 * @brief RX Task (Consumer)
 */
void lora_rx_task(void *arg) {
    farm_packet_t pkt;
    int16_t rssi = 0;

    while (1) {
        if (xQueueReceive(mac_rx_queue, &pkt, portMAX_DELAY)) {
            sx127x_rx_get_packet_rssi(&lora_device, &rssi);
            ESP_LOGI(TAG, "RX Packet (RSSI %d) -> Network Layer", rssi);

            if (pkt.header.network_id != CONFIG_FARMPULSE_NETWORK_ID) continue;
            network_handle_packet(&pkt, (int8_t)rssi);
        }
    }
}