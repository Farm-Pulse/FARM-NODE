#include "lora.h"
#include <esp_log.h>

void app_main(void) {
    ESP_LOGI("main", "Farm-Node starting...");
    lora_init();
}
