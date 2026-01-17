#pragma once
//#include <driver/gpio.h>
//#include <sx127x.h>
//#include "esp_utils.h"

// LilyGO T3S3-v1.2 pin mapping
// #define LORA_NSS_PIN     GPIO_NUM_7
// #define LORA_SCK_PIN     GPIO_NUM_5
// #define LORA_MISO_PIN    GPIO_NUM_3
// #define LORA_MOSI_PIN    GPIO_NUM_6
// #define LORA_RESET_PIN   GPIO_NUM_8

// #define LORA_DIO0_PIN    GPIO_NUM_9
// #define LORA_DIO1_PIN    GPIO_NUM_33   // Optional, if needed

void lora_init(void);
