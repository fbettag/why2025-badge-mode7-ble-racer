#include <stdio.h>
#include "esp_log.h"
#include "esp_system.h"
#include "app.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "Mode-7 BLE Racer starting...");
    ESP_LOGI(TAG, "ESP-IDF: %s", esp_get_idf_version());
    
    esp_err_t ret = app_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "App initialization failed: %s", esp_err_to_name(ret));
        return;
    }
    
    ESP_LOGI(TAG, "Starting main game loop...");
    app_run();
    
    ESP_LOGI(TAG, "App shutdown complete");
}