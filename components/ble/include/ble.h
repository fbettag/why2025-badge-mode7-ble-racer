#ifndef _BLE_H_
#define _BLE_H_

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "game_types.h"

// BLE configuration
#define BLE_DEVICE_NAME             "Mode7Racer"
#define BLE_MANUFACTURER_DATA       "ESP32C6"
#define BLE_SERVICE_UUID            0x1815  // Automation IO service
#define BLE_GAME_STATE_CHAR_UUID    0x2A56  // Digital characteristic
#define BLE_INPUT_CHAR_UUID         0x2A57  // Analog characteristic
#define BLE_CONFIG_CHAR_UUID        0x2A58  // Aggregate characteristic

// BLE connection states
typedef enum {
    BLE_STATE_IDLE,
    BLE_STATE_ADVERTISING,
    BLE_STATE_CONNECTING,
    BLE_STATE_CONNECTED,
    BLE_STATE_DISCONNECTED
} ble_state_t;

// Game state packet structure
typedef struct __attribute__((packed)) {
    uint8_t game_state;           // Current game state
    uint8_t player_id;            // Local player ID (0 or 1)
    uint32_t frame_number;        // Synchronization frame
    int32_t car_position_x;       // Fixed-point car X position
    int32_t car_position_y;       // Fixed-point car Y position
    int32_t car_velocity_x;       // Fixed-point car X velocity
    int32_t car_velocity_y;       // Fixed-point car Y velocity
    int32_t car_heading;          // Fixed-point car heading (angle)
    uint8_t checkpoint_index;     // Current checkpoint
    uint8_t lap_count;            // Current lap
    uint8_t race_finished;        // Race completion flag
    uint16_t timestamp;           // Timestamp for latency calculation
    uint16_t checksum;            // CRC16 checksum
} game_state_packet_t;

// Input packet structure
typedef struct __attribute__((packed)) {
    uint8_t player_id;            // Player ID (0 or 1)
    int8_t throttle;              // Throttle input (-100 to 100)
    int8_t brake;                 // Brake input (-100 to 100)
    int8_t steering;              // Steering input (-100 to 100)
    uint8_t buttons;              // Button states (bitmask)
    uint32_t frame_number;        // Frame this input is for
    uint16_t timestamp;           // Timestamp for latency calculation
    uint16_t checksum;            // CRC16 checksum
} input_packet_t;

// Configuration packet structure
typedef struct __attribute__((packed)) {
    uint8_t config_type;          // Type of configuration
    uint8_t track_id;             // Track identifier
    uint8_t lap_count;            // Number of laps
    uint8_t game_mode;            // Game mode flags
    uint16_t latency_target;      // Target latency in ms
    uint16_t update_rate;         // Update rate in Hz
    uint32_t checksum;            // CRC32 checksum
} config_packet_t;

// BLE event callback type
typedef void (*ble_event_callback_t)(uint8_t event_type, const uint8_t *data, uint16_t length);

// BLE initialization and management
esp_err_t ble_init(void);
void ble_deinit(void);

// Connection management
esp_err_t ble_start_advertising(void);
esp_err_t ble_stop_advertising(void);
esp_err_t ble_start_scanning(void);
esp_err_t ble_stop_scanning(void);

// Data transmission
esp_err_t ble_send_game_state(const game_state_packet_t *state);
esp_err_t ble_send_input(const input_packet_t *input);
esp_err_t ble_send_config(const config_packet_t *config);

// State queries
ble_state_t ble_get_state(void);
bool ble_is_connected(void);
uint16_t ble_get_connection_interval(void);
uint16_t ble_get_latency(void);

// Callback registration
void ble_register_callback(ble_event_callback_t callback);

// Utility functions
uint16_t ble_calculate_latency(void);
void ble_update_connection_parameters(uint16_t interval, uint16_t latency, uint16_t timeout);

#endif // _BLE_H_