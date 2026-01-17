#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

#include "lora.h"
#include "esp_utils.h"
#include "sx127x.h"

static const char *TAG = "sx127x_tx";

sx127x device;

/* Non-blocking TX queue */
static QueueHandle_t tx_queue;

/* Payload */
static const uint8_t payload[] = "! FARMPULSE !";

/* ========= TX CALLBACK (called from SX127x interrupt context) ========= */
void IRAM_ATTR tx_callback(void *ctx)
{
    (void)ctx;
    BaseType_t hp_task_woken = pdFALSE;
    uint8_t msg = 1;

    xQueueSendFromISR(tx_queue, &msg, &hp_task_woken);

    if (hp_task_woken) {
        portYIELD_FROM_ISR();
    }
}

/* ========= TX TASK ========= */
static void tx_task(void *arg)
{
    sx127x *dev = (sx127x *)arg;
    uint8_t msg;

    while (1) {
        if (xQueueReceive(tx_queue, &msg, portMAX_DELAY)) {

            ESP_LOGI(TAG, "Transmitting...");

            ESP_ERROR_CHECK(
                sx127x_lora_tx_set_for_transmission(
                    payload,
                    sizeof(payload) - 1,
                    dev
                )
            );

            ESP_ERROR_CHECK(
                sx127x_set_opmod(
                    SX127X_MODE_TX,
                    SX127X_MODULATION_LORA,
                    dev
                )
            );
        }
    }
}

/* ========= INIT ========= */
void lora_init(void)
{
    ESP_LOGI(TAG, "Starting SX127x TX");

    /* Reset radio */
    sx127x_util_reset();

    /* SPI */
    spi_device_handle_t spi;
    sx127x_init_spi(&spi);

    /* SX127x device */
    ESP_ERROR_CHECK(sx127x_create(spi, &device));

    /* Radio config */
    ESP_ERROR_CHECK(sx127x_set_opmod(SX127X_MODE_STANDBY, SX127X_MODULATION_LORA, &device));
    ESP_ERROR_CHECK(sx127x_set_frequency(TEST_FREQUENCY, &device));
    ESP_ERROR_CHECK(sx127x_lora_reset_fifo(&device));
    ESP_ERROR_CHECK(sx127x_lora_set_bandwidth(SX127X_BW_125000, &device));
    ESP_ERROR_CHECK(sx127x_lora_set_spreading_factor(SX127X_SF_9, &device));
    ESP_ERROR_CHECK(sx127x_lora_set_syncword(0x12, &device));
    ESP_ERROR_CHECK(sx127x_set_preamble_length(8, &device));

    sx127x_tx_header_t header = {
        .enable_crc = true,
        .coding_rate = SX127X_CR_4_5
    };

    ESP_ERROR_CHECK(sx127x_lora_tx_set_explicit_header(&header, &device));
    ESP_ERROR_CHECK(sx127x_tx_set_pa_config(SX127X_PA_PIN_BOOST, 14, &device));

    /* TX queue */
    tx_queue = xQueueCreate(5, sizeof(uint8_t));
    if (!tx_queue) {
        ESP_LOGE(TAG, "TX queue creation failed");
        return;
    }

    /* ===== CRITICAL ORDERING FIX ===== */

    /* 1. Create library interrupt task FIRST */
    ESP_ERROR_CHECK(setup_tx_task(&device, tx_callback));

    /* 2. Install GPIO ISR service */
    esp_err_t err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(err);
    }

    /* 3. Attach DIO0 interrupt */
    setup_gpio_interrupts(DIO0, &device, GPIO_INTR_POSEDGE);

    /* 4. Application TX task */
    xTaskCreate(
        tx_task,
        "sx127x_tx_task",
        4096,
        &device,
        10,
        NULL
    );

    /* Kick first TX */
    uint8_t start = 1;
    xQueueSend(tx_queue, &start, portMAX_DELAY);
}
