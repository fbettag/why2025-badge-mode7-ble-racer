#include "input.h"
#include "keyboard.h"
#include "imu.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>

static const char *TAG = "input";

static input_state_t input_state;
static input_config_t input_config;
static bool input_initialized = false;
static input_state_t prev_state;


esp_err_t input_init(const input_config_t *config) {
    if (input_initialized) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing input system");

    // Copy configuration
    memcpy(&input_config, config, sizeof(input_config_t));

    // Initialize keyboard
    esp_err_t ret = keyboard_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize keyboard");
        return ret;
    }

    // Initialize IMU
    ret = imu_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "IMU initialization failed, continuing without IMU");
        input_config.use_imu_steering = false;
    }

    // Clear input state
    memset(&input_state, 0, sizeof(input_state_t));
    memset(&prev_state, 0, sizeof(input_state_t));

    input_initialized = true;
    ESP_LOGI(TAG, "Input system initialized");
    return ESP_OK;
}

void input_deinit(void) {
    if (!input_initialized) {
        return;
    }

    keyboard_deinit();
    imu_deinit();
    input_initialized = false;
}

void input_update(void) {
    if (!input_initialized) {
        return;
    }

    // Save previous state for edge detection
    memcpy(&prev_state, &input_state, sizeof(input_state_t));

    // Update keyboard state
    keyboard_update();
    
    // Update IMU state
    if (input_config.use_imu_steering) {
        imu_update();
        imu_get_data(&input_state.accel_x, &input_state.accel_y, &input_state.accel_z,
                    &input_state.gyro_x, &input_state.gyro_y, &input_state.gyro_z);
    }

    // Read keyboard keys
    for (int i = 0; i < KEY_COUNT; i++) {
        input_state.keys[i] = keyboard_is_key_pressed(i);
        input_state.keys_changed[i] = (input_state.keys[i] != prev_state.keys[i]);
    }

    // Calculate analog inputs
    input_state.throttle = 0.0f;
    input_state.brake = 0.0f;
    input_state.steering = 0.0f;

    // Keyboard steering
    if (input_state.keys[KEY_UP] || input_state.keys[KEY_W]) {
        input_state.throttle = 1.0f;
    }
    if (input_state.keys[KEY_DOWN] || input_state.keys[KEY_S]) {
        input_state.brake = 1.0f;
    }
    if (input_state.keys[KEY_LEFT] || input_state.keys[KEY_A]) {
        input_state.steering = -1.0f;
    }
    if (input_state.keys[KEY_RIGHT] || input_state.keys[KEY_D]) {
        input_state.steering = 1.0f;
    }

    // IMU steering (overrides keyboard if enabled)
    if (input_config.use_imu_steering) {
        float imu_steering = input_state.tilt_angle / input_config.imu_sensitivity;
        
        // Apply deadzone
        if (fabs(imu_steering) < input_config.deadzone) {
            imu_steering = 0.0f;
        }
        
        // Clamp to [-1, 1]
        if (imu_steering < -1.0f) imu_steering = -1.0f;
        if (imu_steering > 1.0f) imu_steering = 1.0f;
        
        input_state.steering = imu_steering;
    }
}

const input_state_t* input_get_state(void) {
    return &input_state;
}

bool input_key_pressed(key_code_t key) {
    if (!input_initialized || key >= KEY_COUNT) {
        return false;
    }
    return input_state.keys[key];
}

bool input_key_just_pressed(key_code_t key) {
    if (!input_initialized || key >= KEY_COUNT) {
        return false;
    }
    return input_state.keys[key] && input_state.keys_changed[key];
}

bool input_key_just_released(key_code_t key) {
    if (!input_initialized || key >= KEY_COUNT) {
        return false;
    }
    return !input_state.keys[key] && input_state.keys_changed[key];
}

float input_get_throttle(void) {
    return input_initialized ? input_state.throttle : 0.0f;
}

float input_get_brake(void) {
    return input_initialized ? input_state.brake : 0.0f;
}

float input_get_steering(void) {
    return input_initialized ? input_state.steering : 0.0f;
}

void input_calibrate_imu(void) {
    if (input_initialized && input_config.use_imu_steering) {
        imu_calibrate();
    }
}

void input_reset_calibration(void) {
    if (input_initialized && input_config.use_imu_steering) {
        imu_reset_calibration();
    }
}