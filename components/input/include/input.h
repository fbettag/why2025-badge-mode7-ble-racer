#ifndef _INPUT_H_
#define _INPUT_H_

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// Key definitions (QWERTY keyboard matrix)
typedef enum {
    KEY_UP = 0,
    KEY_DOWN,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_W,
    KEY_A,
    KEY_S,
    KEY_D,
    KEY_SPACE,
    KEY_ENTER,
    KEY_ESC,
    KEY_BACKSPACE,
    KEY_COUNT
} key_code_t;

// Input state structure
typedef struct {
    bool keys[KEY_COUNT];
    bool keys_changed[KEY_COUNT];
    
    // IMU data
    float accel_x, accel_y, accel_z;
    float gyro_x, gyro_y, gyro_z;
    float tilt_angle;  // Steering angle from IMU (-5° to +5°)
    
    // Analog inputs
    float throttle;    // 0.0 to 1.0
    float brake;       // 0.0 to 1.0
    float steering;    // -1.0 (left) to 1.0 (right)
} input_state_t;

// Input configuration
typedef struct {
    bool use_imu_steering;
    float imu_sensitivity;    // Degrees to steering ratio
    float deadzone;          // Input deadzone
} input_config_t;

// Public API
esp_err_t input_init(const input_config_t *config);
void input_deinit(void);

// Update input state (call every frame)
void input_update(void);

// Get current input state
const input_state_t* input_get_state(void);

// Key state queries
bool input_key_pressed(key_code_t key);
bool input_key_just_pressed(key_code_t key);
bool input_key_just_released(key_code_t key);

// Analog input queries
float input_get_throttle(void);
float input_get_brake(void);
float input_get_steering(void);

// Calibration functions
void input_calibrate_imu(void);
void input_reset_calibration(void);

#endif // _INPUT_H_