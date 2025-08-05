#ifndef _LOBBY_H_
#define _LOBBY_H_

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "ble.h"

// Lobby configuration
#define LOBBY_DEVICE_NAME_MAX_LEN 32
#define LOBBY_MAX_DEVICES 8
#define LOBBY_SCAN_TIMEOUT_MS 10000
#define LOBBY_ADVERTISE_TIMEOUT_MS 15000

// Lobby states
typedef enum {
    LOBBY_STATE_IDLE,
    LOBBY_STATE_HOSTING,
    LOBBY_STATE_SCANNING,
    LOBBY_STATE_CONNECTING,
    LOBBY_STATE_CONNECTED,
    LOBBY_STATE_ERROR
} lobby_state_t;

// Device role
typedef enum {
    LOBBY_ROLE_HOST,
    LOBBY_ROLE_CLIENT
} lobby_role_t;

// Device information
typedef struct {
    uint8_t addr[6];                    // BLE MAC address
    char device_name[LOBBY_DEVICE_NAME_MAX_LEN];
    int8_t rssi;                        // Signal strength
    uint32_t last_seen;                 // Last seen timestamp
    bool is_host;                       // Whether this device is hosting
    uint16_t connection_handle;         // Connection handle if connected
} lobby_device_t;

// Lobby configuration
typedef struct {
    lobby_role_t role;
    char player_name[LOBBY_DEVICE_NAME_MAX_LEN];
    char game_name[LOBBY_DEVICE_NAME_MAX_LEN];
    uint8_t max_players;
    uint16_t game_mode;
    uint32_t timeout_ms;
} lobby_config_t;

// Lobby event types
typedef enum {
    LOBBY_EVENT_DEVICE_FOUND,
    LOBBY_EVENT_DEVICE_LOST,
    LOBBY_EVENT_CONNECTION_REQUEST,
    LOBBY_EVENT_CONNECTION_SUCCESS,
    LOBBY_EVENT_CONNECTION_FAILED,
    LOBBY_EVENT_PLAYER_JOINED,
    LOBBY_EVENT_PLAYER_LEFT,
    LOBBY_EVENT_GAME_START,
    LOBBY_EVENT_TIMEOUT
} lobby_event_type_t;

// Lobby event callback
typedef void (*lobby_event_callback_t)(lobby_event_type_t event, const void *data, uint16_t length);

// Game session structure
typedef struct {
    uint8_t player_count;
    uint8_t host_player_id;
    lobby_device_t players[2];  // Max 2 players for 1v1
    uint8_t track_id;
    uint8_t lap_count;
    bool ready_to_start;
} game_session_t;

// Lobby system functions
esp_err_t lobby_init(const lobby_config_t *config);
void lobby_deinit(void);

// Host functions
esp_err_t lobby_start_hosting(void);
esp_err_t lobby_stop_hosting(void);

// Client functions
esp_err_t lobby_start_scanning(void);
esp_err_t lobby_stop_scanning(void);
esp_err_t lobby_connect_to_device(const uint8_t *addr);

// Connection management
esp_err_t lobby_accept_connection(const uint8_t *addr);
esp_err_t lobby_reject_connection(const uint8_t *addr);
esp_err_t lobby_start_game(void);

// State queries
lobby_state_t lobby_get_state(void);
uint8_t lobby_get_device_count(void);
const lobby_device_t* lobby_get_device_list(void);
const game_session_t* lobby_get_session_info(void);

// Event handling
void lobby_register_callback(lobby_event_callback_t callback);

// Utility functions
const char* lobby_state_to_string(lobby_state_t state);
const char* lobby_event_to_string(lobby_event_type_t event);
void lobby_set_timeout(uint32_t timeout_ms);

// Connection quality
int8_t lobby_get_connection_rssi(const uint8_t *addr);
bool lobby_is_device_connected(const uint8_t *addr);

#endif // _LOBBY_H_