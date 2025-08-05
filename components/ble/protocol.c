#include "protocol.h"
#include "esp_log.h"
#include "game_types.h"
#include "physics.h"
#include "utils.h"
#include "math.h"

static const char *TAG = "protocol";

// Protocol state
static protocol_state_t protocol_state = {
    .is_host = false,
    .is_connected = false,
    .local_player_id = 0,
    .remote_player_id = 1,
    .current_frame = 0,
    .last_received_frame = 0,
    .latency_samples = 0,
    .avg_latency = 0,
    .jitter = 0
};

// Ring buffers for input prediction and rollback
typedef struct {
    input_packet_t inputs[PROTOCOL_INPUT_BUFFER_SIZE];
    uint32_t start_frame;
    uint32_t count;
} input_buffer_t;

static input_buffer_t local_input_buffer = {0};
static input_buffer_t remote_input_buffer = {0};

// Prediction and interpolation state
static protocol_prediction_state_t prediction_state = {0};

// Initialize protocol system
esp_err_t protocol_init(bool is_host)
{
    memset(&protocol_state, 0, sizeof(protocol_state));
    memset(&local_input_buffer, 0, sizeof(local_input_buffer));
    memset(&remote_input_buffer, 0, sizeof(remote_input_buffer));
    memset(&prediction_state, 0, sizeof(prediction_state));
    
    protocol_state.is_host = is_host;
    protocol_state.local_player_id = is_host ? 0 : 1;
    protocol_state.remote_player_id = is_host ? 1 : 0;
    
    ESP_LOGI(TAG, "Protocol initialized - Host: %s, Local ID: %d", 
             is_host ? "true" : "false", protocol_state.local_player_id);
    
    return ESP_OK;
}

// Convert physics state to game state packet
void protocol_pack_game_state(const physics_world_t *world, 
                             const car_physics_t *car, 
                             game_state_packet_t *packet)
{
    packet->game_state = 0; // Racing state
    packet->player_id = protocol_state.local_player_id;
    packet->frame_number = protocol_state.current_frame;
    
    // Convert fixed-point to network format (int32_t)
    packet->car_position_x = car->position.x;
    packet->car_position_y = car->position.y;
    packet->car_velocity_x = car->velocity.x;
    packet->car_velocity_y = car->velocity.y;
    packet->car_heading = car->heading;
    
    // Calculate current checkpoint and lap
    packet->checkpoint_index = 0;
    packet->lap_count = 0;
    packet->race_finished = 0;
    
    // Find current checkpoint
    for (int i = 0; i < world->checkpoint_count; i++) {
        if (world->checkpoints[i].passed) {
            packet->checkpoint_index = i;
            packet->lap_count = world->checkpoints[i].lap_count;
            
            if (packet->lap_count >= world->total_laps) {
                packet->race_finished = 1;
            }
            break;
        }
    }
    
    packet->timestamp = esp_timer_get_time() / 1000; // Milliseconds
    packet->checksum = crc16((uint8_t *)packet, sizeof(game_state_packet_t) - sizeof(uint16_t));
}

// Convert input state to input packet
void protocol_pack_input(const input_state_t *input, input_packet_t *packet)
{
    packet->player_id = protocol_state.local_player_id;
    packet->frame_number = protocol_state.current_frame;
    packet->timestamp = esp_timer_get_time() / 1000;
    
    // Map input values to -100 to 100 range
    packet->throttle = input->throttle ? 100 : 0;
    packet->brake = input->brake ? 100 : 0;
    packet->steering = (int8_t)(input->steering * 100.0f);
    
    // Pack button states
    packet->buttons = 0;
    if (input->buttons & BUTTON_A) packet->buttons |= 0x01;
    if (input->buttons & BUTTON_B) packet->buttons |= 0x02;
    if (input->buttons & BUTTON_START) packet->buttons |= 0x04;
    if (input->buttons & BUTTON_SELECT) packet->buttons |= 0x08;
    
    packet->checksum = crc16((uint8_t *)packet, sizeof(input_packet_t) - sizeof(uint16_t));
}

// Convert input packet to physics input
void protocol_unpack_input(const input_packet_t *packet, 
                          float *throttle, float *brake, float *steering)
{
    if (packet->player_id != protocol_state.remote_player_id) {
        ESP_LOGW(TAG, "Input packet for wrong player ID: %d", packet->player_id);
        return;
    }
    
    // Verify checksum
    uint16_t expected_crc = crc16((uint8_t *)packet, sizeof(input_packet_t) - sizeof(uint16_t));
    if (packet->checksum != expected_crc) {
        ESP_LOGE(TAG, "Input packet checksum mismatch");
        return;
    }
    
    // Convert to float values
    *throttle = (float)packet->throttle / 100.0f;
    *brake = (float)packet->brake / 100.0f;
    *steering = (float)packet->steering / 100.0f;
    
    // Store in input buffer for prediction
    uint32_t buffer_index = (packet->frame_number - remote_input_buffer.start_frame) % PROTOCOL_INPUT_BUFFER_SIZE;
    if (packet->frame_number >= remote_input_buffer.start_frame && 
        packet->frame_number < remote_input_buffer.start_frame + PROTOCOL_INPUT_BUFFER_SIZE) {
        
        remote_input_buffer.inputs[buffer_index] = *packet;
        remote_input_buffer.count = MAX(remote_input_buffer.count, 
                                       packet->frame_number - remote_input_buffer.start_frame + 1);
    }
}

// Convert game state packet to physics state
void protocol_unpack_game_state(const game_state_packet_t *packet, 
                               car_physics_t *car, physics_world_t *world)
{
    if (packet->player_id != protocol_state.remote_player_id) {
        ESP_LOGW(TAG, "Game state packet for wrong player ID: %d", packet->player_id);
        return;
    }
    
    // Verify checksum
    uint16_t expected_crc = crc16((uint8_t *)packet, sizeof(game_state_packet_t) - sizeof(uint16_t));
    if (packet->checksum != expected_crc) {
        ESP_LOGE(TAG, "Game state packet checksum mismatch");
        return;
    }
    
    // Update remote car state
    car->position.x = packet->car_position_x;
    car->position.y = packet->car_position_y;
    car->velocity.x = packet->car_velocity_x;
    car->velocity.y = packet->car_velocity_y;
    car->heading = packet->car_heading;
    
    // Update checkpoint and lap info
    if (packet->checkpoint_index < world->checkpoint_count) {
        world->checkpoints[packet->checkpoint_index].passed = true;
        world->checkpoints[packet->checkpoint_index].lap_count = packet->lap_count;
    }
    
    // Calculate latency
    uint32_t current_time = esp_timer_get_time() / 1000;
    uint32_t latency = current_time - packet->timestamp;
    
    if (protocol_state.latency_samples < PROTOCOL_MAX_LATENCY_SAMPLES) {
        protocol_state.latency_samples++;
        protocol_state.avg_latency = (protocol_state.avg_latency * (protocol_state.latency_samples - 1) + latency) / 
                                     protocol_state.latency_samples;
    } else {
        protocol_state.avg_latency = (protocol_state.avg_latency * (PROTOCOL_MAX_LATENCY_SAMPLES - 1) + latency) / 
                                     PROTOCOL_MAX_LATENCY_SAMPLES;
    }
    
    protocol_state.last_received_frame = packet->frame_number;
    
    ESP_LOGD(TAG, "Latency: %d ms, Avg: %d ms", latency, protocol_state.avg_latency);
}

// Store local input for prediction
void protocol_store_local_input(const input_state_t *input, uint32_t frame)
{
    uint32_t buffer_index = (frame - local_input_buffer.start_frame) % PROTOCOL_INPUT_BUFFER_SIZE;
    
    if (frame >= local_input_buffer.start_frame && 
        frame < local_input_buffer.start_frame + PROTOCOL_INPUT_BUFFER_SIZE) {
        
        protocol_pack_input(input, &local_input_buffer.inputs[buffer_index]);
        local_input_buffer.count = MAX(local_input_buffer.count, 
                                     frame - local_input_buffer.start_frame + 1);
    }
}

// Predict remote input for frame prediction
bool protocol_predict_remote_input(uint32_t frame, input_packet_t *predicted_input)
{
    if (remote_input_buffer.count == 0) {
        // No remote input received yet, use neutral input
        predicted_input->throttle = 0;
        predicted_input->brake = 0;
        predicted_input->steering = 0;
        predicted_input->buttons = 0;
        predicted_input->frame_number = frame;
        return true;
    }
    
    uint32_t buffer_index = (frame - remote_input_buffer.start_frame) % PROTOCOL_INPUT_BUFFER_SIZE;
    
    if (frame < remote_input_buffer.start_frame) {
        // Frame is before our buffer, use neutral input
        predicted_input->throttle = 0;
        predicted_input->brake = 0;
        predicted_input->steering = 0;
        predicted_input->buttons = 0;
        predicted_input->frame_number = frame;
        return true;
    }
    
    if (frame < remote_input_buffer.start_frame + remote_input_buffer.count) {
        // We have actual input for this frame
        *predicted_input = remote_input_buffer.inputs[buffer_index];
        return true;
    }
    
    // Extrapolate from last known input
    uint32_t last_frame = remote_input_buffer.start_frame + remote_input_buffer.count - 1;
    uint32_t last_index = last_frame % PROTOCOL_INPUT_BUFFER_SIZE;
    
    *predicted_input = remote_input_buffer.inputs[last_index];
    predicted_input->frame_number = frame;
    
    return true;
}

// Check if we should rollback due to prediction error
bool protocol_should_rollback(uint32_t frame, const car_physics_t *predicted, 
                             const car_physics_t *actual, float threshold)
{
    // Calculate position error
    fixed16_t dx = predicted->position.x - actual->position.x;
    fixed16_t dy = predicted->position.y - actual->position.y;
    fixed16_t distance_error = fixed_sqrt(fixed_mul(dx, dx) + fixed_mul(dy, dy));
    
    // Convert to float for comparison
    float distance_error_f = FIXED16_TO_FLOAT(distance_error);
    
    // Calculate heading error
    fixed16_t heading_error = predicted->heading - actual->heading;
    if (heading_error < 0) heading_error = -heading_error;
    float heading_error_f = FIXED16_TO_FLOAT(heading_error);
    
    return distance_error_f > threshold || heading_error_f > 0.1f; // 0.1 rad ~ 5.7 degrees
}

// Update protocol state for new frame
void protocol_advance_frame(void)
{
    protocol_state.current_frame++;
    
    // Clean up old input data
    uint32_t buffer_end = local_input_buffer.start_frame + local_input_buffer.count;
    if (protocol_state.current_frame > buffer_end + PROTOCOL_INPUT_BUFFER_SIZE / 2) {
        local_input_buffer.start_frame = protocol_state.current_frame - PROTOCOL_INPUT_BUFFER_SIZE / 4;
        local_input_buffer.count = PROTOCOL_INPUT_BUFFER_SIZE / 4;
    }
    
    buffer_end = remote_input_buffer.start_frame + remote_input_buffer.count;
    if (protocol_state.current_frame > buffer_end + PROTOCOL_INPUT_BUFFER_SIZE / 2) {
        remote_input_buffer.start_frame = protocol_state.current_frame - PROTOCOL_INPUT_BUFFER_SIZE / 4;
        remote_input_buffer.count = PROTOCOL_INPUT_BUFFER_SIZE / 4;
    }
}

// Get protocol statistics
void protocol_get_stats(protocol_stats_t *stats)
{
    stats->avg_latency = protocol_state.avg_latency;
    stats->current_frame = protocol_state.current_frame;
    stats->last_received_frame = protocol_state.last_received_frame;
    stats->jitter = protocol_state.jitter;
    stats->is_host = protocol_state.is_host;
    stats->is_connected = protocol_state.is_connected;
}

// Reset protocol state
void protocol_reset(void)
{
    protocol_state.current_frame = 0;
    protocol_state.last_received_frame = 0;
    protocol_state.latency_samples = 0;
    protocol_state.avg_latency = 0;
    protocol_state.jitter = 0;
    
    memset(&local_input_buffer, 0, sizeof(local_input_buffer));
    memset(&remote_input_buffer, 0, sizeof(remote_input_buffer));
    memset(&prediction_state, 0, sizeof(prediction_state));
    
    ESP_LOGI(TAG, "Protocol state reset");
}

// Handle connection state changes
void protocol_handle_connection(bool connected)
{
    protocol_state.is_connected = connected;
    
    if (!connected) {
        protocol_reset();
    }
    
    ESP_LOGI(TAG, "Protocol connection state: %s", connected ? "connected" : "disconnected");
}