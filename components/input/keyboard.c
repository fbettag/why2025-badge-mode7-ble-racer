#include "keyboard.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "keyboard";

// GPIO pin definitions for keyboard matrix
#define KB_ROWS 4
#define KB_COLS 12

static const gpio_num_t row_pins[KB_ROWS] = {1, 2, 3, 4};
static const gpio_num_t col_pins[KB_COLS] = {5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};

static bool key_states[KEY_COUNT] = {false};
static bool keyboard_initialized = false;


esp_err_t keyboard_init(void) {
    if (keyboard_initialized) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing keyboard matrix");

    // Configure row pins as outputs
    for (int i = 0; i < KB_ROWS; i++) {
        gpio_config_t io_conf = {
            .pin_bit_mask = 1ULL << row_pins[i],
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE
        };
        ESP_ERROR_CHECK(gpio_config(&io_conf));
        gpio_set_level(row_pins[i], 1); // Active low
    }

    // Configure column pins as inputs with pull-ups
    for (int i = 0; i < KB_COLS; i++) {
        gpio_config_t io_conf = {
            .pin_bit_mask = 1ULL << col_pins[i],
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE
        };
        ESP_ERROR_CHECK(gpio_config(&io_conf));
    }

    keyboard_initialized = true;
    ESP_LOGI(TAG, "Keyboard initialized");
    return ESP_OK;
}

void keyboard_deinit(void) {
    if (!keyboard_initialized) {
        return;
    }

    // Reset all pins to input mode
    for (int i = 0; i < KB_ROWS; i++) {
        gpio_set_direction(row_pins[i], GPIO_MODE_INPUT);
    }
    for (int i = 0; i < KB_COLS; i++) {
        gpio_set_direction(col_pins[i], GPIO_MODE_INPUT);
    }

    keyboard_initialized = false;
}

void keyboard_update(void) {
    if (!keyboard_initialized) {
        return;
    }

    // Clear all key states
    memset(key_states, 0, sizeof(key_states));

    // Scan keyboard matrix
    for (int row = 0; row < KB_ROWS; row++) {
        // Set current row low
        gpio_set_level(row_pins[row], 0);
        
        // Small delay for signal settling
        esp_rom_delay_us(10);
        
        // Read column pins
        for (int col = 0; col < KB_COLS; col++) {
            if (gpio_get_level(col_pins[col]) == 0) {
                // Key pressed - map to our key codes
                int key_index = row * KB_COLS + col;
                if (key_index < KEY_COUNT) {
                    key_states[key_index] = true;
                }
            }
        }
        
        // Set row back to high (inactive)
        gpio_set_level(row_pins[row], 1);
    }
}

bool keyboard_is_key_pressed(key_code_t key) {
    if (!keyboard_initialized || key >= KEY_COUNT) {
        return false;
    }
    return key_states[key];
}