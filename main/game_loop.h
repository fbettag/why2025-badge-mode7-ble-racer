#ifndef _GAME_LOOP_H_
#define _GAME_LOOP_H_

#include "esp_err.h"
#include <stdbool.h>

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
    uint32_t target_fps;
    bool enable_half_res;
    bool enable_imu_steering;
    uint8_t net_update_rate;
} game_config_t;

esp_err_t game_loop_init(void);
void game_loop_run(void);
void game_loop_deinit(void);

// State management
void game_set_state(game_state_t state);
game_state_t game_get_state(void);

// Configuration
void game_set_config(const game_config_t *config);
const game_config_t* game_get_config(void);

// Performance monitoring
float game_get_fps(void);
uint32_t game_get_frame_time_ms(void);

#endif // _GAME_LOOP_H_