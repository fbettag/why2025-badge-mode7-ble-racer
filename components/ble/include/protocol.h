#ifndef _PROTOCOL_H_
#define _PROTOCOL_H_

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "game_types.h"
#include "physics.h"

// Protocol configuration
#define PROTOCOL_INPUT_BUFFER_SIZE      64
#define PROTOCOL_MAX_LATENCY_SAMPLES    100
#define PROTOCOL_PREDICTION_THRESHOLD   5.0f  // 5 units distance
#define PROTOCOL_MAX_PREDICTION_FRAMES  8     // Maximum frames to predict ahead

// Protocol statistics structure
typedef struct {
    uint32_t avg_latency;           // Average latency in milliseconds
    uint32_t jitter;               // Network jitter in milliseconds
    uint32_t current_frame;        // Current synchronization frame
    uint32_t last_received_frame;  // Last received frame from remote
    bool is_host;                  // True if this device is the host
    bool is_connected;             // True if BLE connection is active
} protocol_stats_t;

// Prediction state structure
typedef struct {
    car_physics_t predicted_states[PROTOCOL_MAX_PREDICTION_FRAMES];
    uint32_t prediction_frame;
    bool has_prediction_error;
    uint32_t prediction_error_count;
    float avg_prediction_error;
} protocol_prediction_state_t;

// Protocol state structure
typedef struct {
    bool is_host;
    bool is_connected;
    uint8_t local_player_id;
    uint8_t remote_player_id;
    uint32_t current_frame;
    uint32_t last_received_frame;
    uint32_t latency_samples;
    uint32_t avg_latency;
    uint32_t jitter;
} protocol_state_t;

// Protocol initialization
esp_err_t protocol_init(bool is_host);
void protocol_reset(void);

// Data packing/unpacking functions
void protocol_pack_game_state(const physics_world_t *world, 
                             const car_physics_t *car, 
                             game_state_packet_t *packet);

void protocol_pack_input(const input_state_t *input, input_packet_t *packet);

void protocol_unpack_game_state(const game_state_packet_t *packet, 
                               car_physics_t *car, physics_world_t *world);

void protocol_unpack_input(const input_packet_t *packet, 
                          float *throttle, float *brake, float *steering);

// Input prediction and synchronization
void protocol_store_local_input(const input_state_t *input, uint32_t frame);
bool protocol_predict_remote_input(uint32_t frame, input_packet_t *predicted_input);
bool protocol_should_rollback(uint32_t frame, const car_physics_t *predicted, 
                             const car_physics_t *actual, float threshold);

// Frame management
void protocol_advance_frame(void);

// Connection management
void protocol_handle_connection(bool connected);

// Statistics
void protocol_get_stats(protocol_stats_t *stats);

// Utility functions
uint16_t protocol_estimate_latency(void);
bool protocol_is_input_late(uint32_t frame_number);

#endif // _PROTOCOL_H_