#include "app.h"
#include "display.h"
#include "input.h"
#include "game_loop.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "app";

esp_err_t app_init(void)
{
    ESP_LOGI(TAG, "Initializing application...");
    
    // Initialize display
    display_config_t display_cfg = {
        .width = 720,
        .height = 720,
        .refresh_rate = 30,
        .use_dma = true
    };
    
    esp_err_t ret = display_init(&display_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Display initialization failed");
        return ret;
    }
    
    // Initialize input
    input_config_t input_cfg = {
        .use_imu_steering = true,
        .imu_sensitivity = 5.0f,
        .deadzone = 0.1f
    };
    
    ret = input_init(&input_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Input initialization failed");
        display_deinit();
        return ret;
    }
    
    // Initialize game loop
    ret = game_loop_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Game loop initialization failed");
        input_deinit();
        display_deinit();
        return ret;
    }
    
    ESP_LOGI(TAG, "Application initialized successfully");
    return ESP_OK;
}

void app_run(void)
{
    ESP_LOGI(TAG, "Starting game loop");
    game_loop_run();
}

void app_deinit(void)
{
    ESP_LOGI(TAG, "Shutting down application...");
    game_loop_deinit();
    input_deinit();
    display_deinit();
}