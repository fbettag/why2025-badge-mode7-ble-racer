#ifndef _GAME_TYPES_H_
#define _GAME_TYPES_H_

#include <stdint.h>
#include <stdbool.h>
#include "math.h"

// Game states
typedef enum {
    GAME_STATE_MENU,
    GAME_STATE_LOBBY,
    GAME_STATE_COUNTDOWN,
    GAME_STATE_RACING,
    GAME_STATE_RESULTS,
    GAME_STATE_SETTINGS
} game_state_t;

// Game configuration
typedef struct {
    uint8_t target_fps;
    bool enable_half_res;
    bool enable_imu_steering;
    uint8_t net_update_rate;
    uint8_t max_players;
    uint8_t track_id;
    uint32_t race_seed;
} game_config_t;

// Player data
typedef struct {
    uint8_t player_id;
    char name[32];
    uint32_t best_lap_time;
    uint32_t current_lap_time;
    uint8_t current_lap;
    uint8_t total_laps;
    bool is_finished;
} player_data_t;

// Race data
typedef struct {
    uint32_t start_time;
    uint32_t current_time;
    uint8_t num_players;
    player_data_t players[2];  // Support 2 players for 1v1
    uint8_t track_id;
    uint32_t race_seed;
} race_data_t;

// Car state for network synchronization
typedef struct {
    vec2_t position;
    vec2_t velocity;
    fixed16_t heading;
    fixed16_t angular_velocity;
    uint8_t current_checkpoint;
    uint8_t lap_count;
    bool is_finished;
} car_state_t;

// Input state
typedef struct {
    float throttle;     // 0.0 to 1.0
    float brake;        // 0.0 to 1.0
    float steering;     // -1.0 to 1.0
    uint8_t buttons;    // Bit flags for buttons
} input_state_t;

// Button flags
#define BUTTON_BOOST     (1 << 0)
#define BUTTON_HANDBRAKE (1 << 1)
#define BUTTON_HORN      (1 << 2)
#define BUTTON_PAUSE     (1 << 3)

// Track tile types
typedef enum {
    TILE_GRASS = 0,
    TILE_ROAD,
    TILE_SAND,
    TILE_WATER,
    TILE_WALL,
    TILE_CHECKPOINT,
    TILE_START_FINISH,
    TILE_BOOST_PAD,
    TILE_OIL_SLICK,
    TILE_COUNT
} tile_type_t;

// Track properties
typedef struct {
    uint8_t friction;      // 0-255, where 255 is max friction
    uint8_t speed_modifier; // 0-255, where 128 is normal speed
    bool is_solid;         // Can't drive through
    bool is_checkpoint;    // Checkpoint tile
    bool is_hazard;        // Causes spin/damage
} tile_properties_t;

// HUD display data
typedef struct {
    uint32_t current_speed;
    uint32_t current_lap_time;
    uint32_t best_lap_time;
    uint8_t current_lap;
    uint8_t total_laps;
    uint8_t position;  // Race position (1st, 2nd, etc.)
    uint8_t checkpoint_progress;
    bool wrong_way;
} hud_data_t;

#endif // _GAME_TYPES_H_