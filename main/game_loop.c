#include "game_loop.h"
#include "display.h"
#include "input.h"
#include "physics.h"
#include "ble.h"
#include "protocol.h"
#include "asset_loader.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifndef MAX
#define MAX(a,b) (((a) > (b)) ? (a) : (b))
#endif
#ifndef MIN
#define MIN(a,b) (((a) < (b)) ? (a) : (b))
#endif

static const char *TAG = "game_loop";

static game_state_t current_state = GAME_STATE_MENU;
static game_config_t game_config;
static bool game_running = false;
static esp_timer_handle_t game_timer;

static uint32_t frame_count = 0;
static uint32_t last_frame_time = 0;
static float current_fps = 0.0f;
static physics_world_t physics_world;

// Forward declarations
static void game_update_menu(void);
static void game_update_lobby(void);
static void game_update_countdown(void);
static void game_update_racing(void);
static void game_update_results(void);
static void game_render(void);

esp_err_t game_loop_init(void)
{
    ESP_LOGI(TAG, "Initializing game loop");
    
    // Set default configuration
    game_config.target_fps = 30;
    game_config.enable_half_res = false;
    game_config.enable_imu_steering = true;
    game_config.net_update_rate = 20; // Hz
    
    frame_count = 0;
    last_frame_time = 0;
    current_fps = 0.0f;
    
    // Initialize physics system
    esp_err_t ret = physics_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize physics system");
        return ret;
    }
    
    // Set up physics world
    memset(&physics_world, 0, sizeof(physics_world_t));
    physics_world.checkpoint_count = 4;  // Simple 4-checkpoint track
    
    // Add some checkpoints (simple circular track)
    for (int i = 0; i < physics_world.checkpoint_count; i++) {
        fixed16_t angle = FLOAT_TO_FIXED16(i * 1.5708f);  // 90 degrees per checkpoint
        physics_world.checkpoints[i].position.x = fixed_mul(FLOAT_TO_FIXED16(200), fixed_cos(angle));
        physics_world.checkpoints[i].position.y = fixed_mul(FLOAT_TO_FIXED16(200), fixed_sin(angle));
        physics_world.checkpoints[i].radius = PHYSICS_CHECKPOINT_RADIUS;
        physics_world.checkpoints[i].index = i;
        physics_world.checkpoints[i].passed = false;
    }
    
    // Initialize track system
    track_loader_config_t track_config = {
        .enable_cache = true,
        .enable_compression = false,
        .max_memory_usage = 512 * 1024,  // 512KB
        .max_tracks_cached = 2
    };
    
    esp_err_t track_ret = track_loader_init(&track_config);
    if (track_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize track loader");
        return track_ret;
    }
    
    // Initialize track cache
    track_cache_init();

    // Initialize asset system
    asset_config_t asset_config = {
        .enable_compression = false,
        .enable_caching = true,
        .max_memory_usage = 2 * 1024 * 1024,  // 2MB
        .max_cached_assets = 8,
        .preload_textures = true
    };
    asset_loader_init(&asset_config);

    // Initialize tile system and create default assets
    tile_system_init();

    // Create default track if none exists
    track_create_default("default.trk");
    
    // Load default track
    track_data_t *default_track = track_loader_load("default.trk");
    if (default_track) {
        ESP_LOGI(TAG, "Loaded track: %s (%dx%d)", default_track->name, default_track->width, default_track->height);
        
        // Initialize physics with track data
        physics_world.checkpoint_count = default_track->checkpoint_count;
        for (int i = 0; i < default_track->checkpoint_count; i++) {
            physics_world.checkpoints[i].position.x = INT_TO_FIXED16(default_track->checkpoints[i].x);
            physics_world.checkpoints[i].position.y = INT_TO_FIXED16(default_track->checkpoints[i].y);
            physics_world.checkpoints[i].radius = INT_TO_FIXED16(default_track->checkpoints[i].radius);
            physics_world.checkpoints[i].index = default_track->checkpoints[i].index;
            physics_world.checkpoints[i].passed = false;
        }
    } else {
        ESP_LOGW(TAG, "Using fallback track data");
        physics_world.checkpoint_count = 4;
        for (int i = 0; i < 4; i++) {
            fixed16_t angle = FLOAT_TO_FIXED16(i * 1.5708f);
            physics_world.checkpoints[i].position.x = fixed_mul(FLOAT_TO_FIXED16(200), fixed_cos(angle));
            physics_world.checkpoints[i].position.y = fixed_mul(FLOAT_TO_FIXED16(200), fixed_sin(angle));
            physics_world.checkpoints[i].radius = PHYSICS_CHECKPOINT_RADIUS;
            physics_world.checkpoints[i].index = i;
            physics_world.checkpoints[i].passed = false;
        }
    }
    
    // Initialize cars
    physics_reset_race(&physics_world);
    
    // Initialize BLE and protocol
    esp_err_t ble_ret = ble_init();
    if (ble_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize BLE");
        return ble_ret;
    }
    
    esp_err_t protocol_ret = protocol_init(false); // Start as client
    if (protocol_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize protocol");
        return protocol_ret;
    }
    
    ESP_LOGI(TAG, "Game loop initialized");
    return ESP_OK;
}

void game_loop_run(void)
{
    ESP_LOGI(TAG, "Starting game loop");
    game_running = true;
    
    uint32_t frame_start_time, frame_end_time;
    uint32_t target_frame_time = 1000 / game_config.target_fps;
    uint32_t last_physics_update = 0;
    
    while (game_running) {
        frame_start_time = esp_timer_get_time() / 1000;
        
        // Handle input
        input_update();
        
        // Update physics at fixed timestep
        uint32_t current_time = esp_timer_get_time() / 1000;
        if (current_state == GAME_STATE_RACING && 
            (current_time - last_physics_update) >= 16) {  // 60Hz physics
            physics_update(&physics_world, 0.016f);
            last_physics_update = current_time;
        }
        
        // Update game state
        switch (current_state) {
            case GAME_STATE_MENU:
                game_update_menu();
                break;
            case GAME_STATE_LOBBY:
                game_update_lobby();
                break;
            case GAME_STATE_COUNTDOWN:
                game_update_countdown();
                break;
            case GAME_STATE_RACING:
                game_update_racing();
                break;
            case GAME_STATE_RESULTS:
                game_update_results();
                break;
            case GAME_STATE_SETTINGS:
                // TODO: Settings menu
                break;
        }
        
        // Render frame
        game_render();
        
        frame_end_time = esp_timer_get_time() / 1000;
        uint32_t frame_time = frame_end_time - frame_start_time;
        
        // Frame pacing
        if (frame_time < target_frame_time) {
            vTaskDelay(pdMS_TO_TICKS(target_frame_time - frame_time));
        }
        
        // Update FPS counter
        frame_count++;
        if (frame_count % 60 == 0) {
            uint32_t current_time = esp_timer_get_time() / 1000;
            if (last_frame_time > 0) {
                current_fps = 60000.0f / (current_time - last_frame_time);
            }
            last_frame_time = current_time;
            ESP_LOGD(TAG, "FPS: %.2f", current_fps);
        }
    }
    
    ESP_LOGI(TAG, "Game loop stopped");
    
    ble_deinit();
    track_cache_deinit();
    track_loader_deinit();
}

void game_loop_deinit(void)
{
    ESP_LOGI(TAG, "Deinitializing game loop");
    game_running = false;
    
    if (game_timer) {
        esp_timer_stop(game_timer);
        esp_timer_delete(game_timer);
        game_timer = NULL;
    }
}

void game_set_state(game_state_t state)
{
    ESP_LOGI(TAG, "Game state changing: %d -> %d", current_state, state);
    current_state = state;
    
    // State transition logic
    switch (state) {
        case GAME_STATE_MENU:
            // Reset to main menu
            break;
        case GAME_STATE_LOBBY:
            // Start lobby for P2P connection
            break;
        case GAME_STATE_COUNTDOWN:
            // Start 3-2-1 countdown
            break;
        case GAME_STATE_RACING:
            // Start race
            break;
        case GAME_STATE_RESULTS:
            // Show race results
            break;
        case GAME_STATE_SETTINGS:
            // Settings menu
            break;
    }
}

game_state_t game_get_state(void)
{
    return current_state;
}

void game_set_config(const game_config_t *config)
{
    memcpy(&game_config, config, sizeof(game_config_t));
}

const game_config_t* game_get_config(void)
{
    return &game_config;
}

float game_get_fps(void)
{
    return current_fps;
}

uint32_t game_get_frame_time_ms(void)
{
    return 1000 / game_config.target_fps;
}

// Game state update functions
static void game_update_menu(void)
{
    const input_state_t *input = input_get_state();
    
    // Check for menu navigation
    if (input_key_just_pressed(KEY_ENTER)) {
        game_set_state(GAME_STATE_LOBBY);
    }
    
    // Simple menu rendering
    display_clear(0x001F); // Blue background
    
    // Draw menu text (placeholder)
    display_fill_rect(300, 300, 120, 60, 0xFFFF);
    (void)input; // Suppress unused variable warning
}

static void game_update_lobby(void)
{
    const input_state_t *input = input_get_state();
    
    // Check for lobby actions
    if (input_key_just_pressed(KEY_ESC)) {
        game_set_state(GAME_STATE_MENU);
    }
    
    // Lobby rendering
    display_clear(0x07E0); // Green background
    (void)input; // Suppress unused variable warning
}

static void game_update_countdown(void)
{
    static uint32_t countdown_start = 0;
    static bool countdown_init = false;
    
    if (!countdown_init) {
        countdown_start = esp_timer_get_time() / 1000;
        countdown_init = true;
    }
    
    uint32_t elapsed = (esp_timer_get_time() / 1000) - countdown_start;
    
    if (elapsed > 3000) { // 3 second countdown
        game_set_state(GAME_STATE_RACING);
        physics_start_race(&physics_world);
        countdown_init = false;
    }
    
    // Countdown rendering
    display_clear(0xF800); // Red background
    
    int countdown = 3 - (elapsed / 1000);
    if (countdown > 0) {
        // TODO: Display countdown number
    }
}

static void game_update_racing(void)
{
    // Handle racing input
    float throttle = input_get_throttle();
    float brake = input_get_brake();
    float steering = input_get_steering();
    
    // Update car physics
    if (physics_world.car_count > 0 && physics_world.cars != NULL) {
        physics_handle_input(&physics_world.cars[0], throttle, brake, steering, 0.016f);
    }
    
    // Check if race finished
    if (physics_check_race_finished(&physics_world, 0)) {
        game_set_state(GAME_STATE_RESULTS);
    }
    
    // Check for race end condition
    if (input_key_just_pressed(KEY_ESC)) {
        game_set_state(GAME_STATE_RESULTS);
    }
    
    // Send game state to remote player via BLE
    if (ble_is_connected()) {
        game_state_packet_t game_state;
        if (physics_world.car_count > 0 && physics_world.cars != NULL) {
            protocol_pack_game_state(&physics_world, &physics_world.cars[0], &game_state);
            ble_send_game_state(&game_state);
        }
    }
    
    // Racing rendering
    display_clear(0x001F); // Blue background for sky
    
    // TODO: Render Mode-7 track using physics world
    // For now, render simple car based on physics
    if (physics_world.car_count > 0 && physics_world.cars != NULL) {
        car_physics_t *car1 = &physics_world.cars[0];
        int car_x = 360 + (FIXED16_TO_INT(car1->position.x) / 100);
        int car_y = 360 + (FIXED16_TO_INT(car1->position.y) / 100);
        
        // Clamp to screen bounds
        car_x = MAX(0, MIN(DISPLAY_WIDTH - 16, car_x));
        car_y = MAX(0, MIN(DISPLAY_HEIGHT - 16, car_y));
        
        display_fill_rect(car_x, car_y, 16, 16, 0xF800); // Red car
    }
    
    // Render remote car if connected
    if (ble_is_connected() && physics_world.car_count > 1 && physics_world.cars != NULL) {
        car_physics_t *car2 = &physics_world.cars[1];
        int remote_car_x = 360 + (FIXED16_TO_INT(car2->position.x) / 100);
        int remote_car_y = 360 + (FIXED16_TO_INT(car2->position.y) / 100);
        
        remote_car_x = MAX(0, MIN(DISPLAY_WIDTH - 16, remote_car_x));
        remote_car_y = MAX(0, MIN(DISPLAY_HEIGHT - 16, remote_car_y));
        
        display_fill_rect(remote_car_x, remote_car_y, 16, 16, 0x07E0); // Green car
    }
}

static void game_update_results(void)
{
    const input_state_t *input = input_get_state();
    
    // Check for menu navigation
    if (input_key_just_pressed(KEY_ENTER)) {
        game_set_state(GAME_STATE_MENU);
    }
    
    // Results rendering
    display_clear(0xFFE0); // Yellow background
    (void)input; // Suppress unused variable warning
}

static void game_render(void)
{
    // Flush display buffer
    display_flush();
    display_swap_buffers();
}