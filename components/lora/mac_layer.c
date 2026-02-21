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

#include "sx127x.h"
#include "farmpulse_defs.h"
#include "network_layer.h"
#include "mac_layer.h"

static const char *TAG = "MAC_LAYER";

// --- Hardware Definitions ---
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
static SemaphoreHandle_t spi_mutex = NULL;

// --- Forward Declarations ---
void mac_rx_callback(void *ctx, uint8_t *data, uint16_t len);
void interrupt_task(void *arg);
void lora_interrupt_handler(void *arg);
void lora_rx_task(void *arg);

void mac_init(void) {
    ESP_LOGI(TAG, "Initializing LoRa MAC Layer (Bulletproof)...");

    // 1. Hardware Reset
    gpio_set_direction(PIN_RST, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_RST, 0); 
    vTaskDelay(pdMS_TO_TICKS(5)); 
    gpio_set_level(PIN_RST, 1); 
    vTaskDelay(pdMS_TO_TICKS(10)); 

    // 2. Create Objects
    mac_rx_queue = xQueueCreate(10, sizeof(farm_packet_t));
    tx_done_sem = xSemaphoreCreateBinary();
    spi_mutex = xSemaphoreCreateMutex();

    // 3. SPI Init
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_MOSI,
        .miso_io_num = PIN_MISO,
        .sclk_io_num = PIN_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 0
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

    // 4. Add Device
    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = 4000000, 
        .mode = 0,
        .spics_io_num = PIN_SS,
        .queue_size = 16,
        .command_bits = 0,
        .address_bits = 8, 
        .dummy_bits = 0,
        .flags = 0
    };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &dev_cfg, &spi_handle));

    // 5. Initialize LoRa
    xSemaphoreTake(spi_mutex, portMAX_DELAY);
    ESP_ERROR_CHECK(sx127x_create(spi_handle, &lora_device));
    
    // Radio Config
    ESP_ERROR_CHECK(sx127x_set_opmod(SX127X_MODE_SLEEP, SX127X_MODULATION_LORA, &lora_device));
    ESP_ERROR_CHECK(sx127x_set_frequency(865000000, &lora_device));
    ESP_ERROR_CHECK(sx127x_lora_reset_fifo(&lora_device));
    ESP_ERROR_CHECK(sx127x_rx_set_lna_boost_hf(true, &lora_device));
    ESP_ERROR_CHECK(sx127x_rx_set_lna_gain(SX127X_LNA_GAIN_G1, &lora_device));
    ESP_ERROR_CHECK(sx127x_tx_set_pa_config(SX127X_PA_PIN_BOOST, 14, &lora_device));
    ESP_ERROR_CHECK(sx127x_lora_set_bandwidth(SX127X_BW_125000, &lora_device));
    ESP_ERROR_CHECK(sx127x_lora_set_spreading_factor(SX127X_SF_9, &lora_device));
    
    sx127x_tx_header_t header = { .enable_crc = true, .coding_rate = SX127X_CR_4_5 };
    ESP_ERROR_CHECK(sx127x_lora_tx_set_explicit_header(&header, &lora_device));

    // Callback
    sx127x_rx_set_callback(mac_rx_callback, &lora_device, &lora_device);
    
    // Set to RX
    ESP_ERROR_CHECK(sx127x_set_opmod(SX127X_MODE_RX_CONT, SX127X_MODULATION_LORA, &lora_device));
    xSemaphoreGive(spi_mutex);

    // 6. Tasks
    xTaskCreate(interrupt_task, "lora_irq", 4096, NULL, 20, &interrupt_task_handle);
    gpio_install_isr_service(0);
    gpio_set_direction(PIN_DIO0, GPIO_MODE_INPUT);
    gpio_set_pull_mode(PIN_DIO0, GPIO_PULLUP_ONLY);
    gpio_set_intr_type(PIN_DIO0, GPIO_INTR_POSEDGE);
    gpio_isr_handler_add(PIN_DIO0, lora_interrupt_handler, NULL);

    xTaskCreate(lora_rx_task, "lora_rx_task", 4096, NULL, 10, NULL);
    
    ESP_LOGI(TAG, "LoRa Initialized (Final).");
}

bool mac_tx(farm_packet_t *packet) {
    uint8_t total_len = sizeof(farm_header_t) + packet->header.length - 10; 
    
    ESP_LOGI(TAG, "TX -> NextHop: %d", packet->header.target_id);
    
    // 1. Clear any stale semaphore signals from previous RX events
    xSemaphoreTake(tx_done_sem, 0); 

    // 2. Prepare & Trigger
    xSemaphoreTake(spi_mutex, portMAX_DELAY);
    ESP_ERROR_CHECK(sx127x_lora_tx_set_for_transmission((uint8_t *)packet, total_len, &lora_device));
    ESP_ERROR_CHECK(sx127x_set_opmod(SX127X_MODE_TX, SX127X_MODULATION_LORA, &lora_device));
    xSemaphoreGive(spi_mutex);
    
    // 3. WAIT for Interrupt
    bool success = false;
    if (xSemaphoreTake(tx_done_sem, pdMS_TO_TICKS(2000)) == pdTRUE) {
        ESP_LOGI(TAG, "TX Complete (ISR)");
        success = true;
    } else {
        ESP_LOGE(TAG, "TX Timeout");
        // Reset radio
        xSemaphoreTake(spi_mutex, portMAX_DELAY);
        sx127x_set_opmod(SX127X_MODE_STANDBY, SX127X_MODULATION_LORA, &lora_device);
        xSemaphoreGive(spi_mutex);
    }

    // 4. Return to RX
    xSemaphoreTake(spi_mutex, portMAX_DELAY);
    ESP_ERROR_CHECK(sx127x_set_opmod(SX127X_MODE_RX_CONT, SX127X_MODULATION_LORA, &lora_device));
    xSemaphoreGive(spi_mutex);
    
    return success;
}

void IRAM_ATTR lora_interrupt_handler(void *arg) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (interrupt_task_handle != NULL) {
        vTaskNotifyGiveFromISR(interrupt_task_handle, &xHigherPriorityTaskWoken);
    }
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

void interrupt_task(void *arg) {
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        
        // 1. Handle the HW interrupt (Read flags, maybe call callback)
        xSemaphoreTake(spi_mutex, portMAX_DELAY);
        sx127x_handle_interrupt(&lora_device);
        xSemaphoreGive(spi_mutex);
        
        // 2. FORCE UNLOCK: Whether it was RX or TX, the radio is done doing something.
        // If mac_tx() is sleeping waiting for this, wake it up.
        xSemaphoreGive(tx_done_sem);
    }
}

void mac_rx_callback(void *ctx, uint8_t *data, uint16_t len) {
    // Only queue if it looks like a valid packet
    if (len >= sizeof(farm_header_t) && len <= sizeof(farm_packet_t) && data != NULL) {
        farm_packet_t pkt;
        memcpy(&pkt, data, len);
        xQueueSend(mac_rx_queue, &pkt, 0);
    }
}

void lora_rx_task(void *arg) {
    farm_packet_t pkt;
    int16_t rssi = 0;

    while (1) {
        if (xQueueReceive(mac_rx_queue, &pkt, portMAX_DELAY)) {
            xSemaphoreTake(spi_mutex, portMAX_DELAY);
            sx127x_rx_get_packet_rssi(&lora_device, &rssi);
            xSemaphoreGive(spi_mutex);

            ESP_LOGI(TAG, "RX Packet (RSSI %d) -> Network Layer", rssi);

            if (pkt.header.network_id != CONFIG_FARMPULSE_NETWORK_ID) continue;
            network_handle_packet(&pkt, (int8_t)rssi);
        }
    }
}