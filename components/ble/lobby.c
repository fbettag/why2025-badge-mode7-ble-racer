#include "lobby.h"
#include "ble.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_gattc_api.h"
#include "string.h"

static const char *TAG = "lobby";

// Lobby service UUIDs
#define LOBBY_SERVICE_UUID        0x1812  // Human Interface Device
#define LOBBY_DEVICE_INFO_UUID    0x2A50  // Device Information
#define LOBBY_GAME_CONFIG_UUID    0x2A23  // System ID
#define LOBBY_PLAYER_DATA_UUID    0x2A24  // Model Number String
#define LOBBY_CONNECTION_UUID     0x2A25  // Serial Number String

// Lobby advertisement data
#define LOBBY_ADVERTISEMENT_DATA  "Mode7Racer"
#define LOBBY_SCAN_RESPONSE_DATA  "1v1Racing"

// Lobby state variables
static lobby_config_t lobby_config;
static lobby_state_t current_state = LOBBY_STATE_IDLE;
static lobby_device_t device_list[LOBBY_MAX_DEVICES];
static uint8_t device_count = 0;
static game_session_t current_session;
static lobby_event_callback_t event_callback = NULL;
static EventGroupHandle_t lobby_event_group = NULL;

// Event bits
#define LOBBY_EVENT_SCAN_COMPLETE    (1 << 0)
#define LOBBY_EVENT_DEVICE_FOUND     (1 << 1)
#define LOBBY_EVENT_CONNECTED        (1 << 2)
#define LOBBY_EVENT_DISCONNECTED     (1 << 3)
#define LOBBY_EVENT_GAME_START       (1 << 4)

// Forward declarations
static void handle_ble_event(uint8_t event_type, const uint8_t *data, uint16_t length);
static void add_device_to_list(const esp_ble_addr_t *addr, const char *name, int8_t rssi, bool is_host);
static void remove_device_from_list(const esp_ble_addr_t *addr);
static bool find_device_by_addr(const uint8_t *addr, lobby_device_t *device);
static void clear_device_list(void);

// Initialize lobby system
esp_err_t lobby_init(const lobby_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(&lobby_config, config, sizeof(lobby_config_t));
    
    // Initialize device list
    memset(device_list, 0, sizeof(device_list));
    device_count = 0;
    
    // Initialize session
    memset(&current_session, 0, sizeof(game_session_t));
    current_session.max_players = 2;
    current_session.ready_to_start = false;
    
    // Create event group
    lobby_event_group = xEventGroupCreate();
    if (!lobby_event_group) {
        ESP_LOGE(TAG, "Failed to create event group");
        return ESP_FAIL;
    }
    
    // Register BLE callback
    ble_register_callback(handle_ble_event);
    
    // Initialize BLE
    esp_err_t ret = ble_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize BLE");
        vEventGroupDelete(lobby_event_group);
        return ret;
    }
    
    current_state = LOBBY_STATE_IDLE;
    ESP_LOGI(TAG, "Lobby system initialized as %s", 
             lobby_config.role == LOBBY_ROLE_HOST ? "host" : "client");
    
    return ESP_OK;
}

// Deinitialize lobby system
void lobby_deinit(void)
{
    if (lobby_event_group) {
        vEventGroupDelete(lobby_event_group);
        lobby_event_group = NULL;
    }
    
    clear_device_list();
    ble_deinit();
    current_state = LOBBY_STATE_IDLE;
    ESP_LOGI(TAG, "Lobby system deinitialized");
}

// Start hosting
esp_err_t lobby_start_hosting(void)
{
    if (lobby_config.role != LOBBY_ROLE_HOST) {
        ESP_LOGE(TAG, "Cannot host - configured as client");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (current_state != LOBBY_STATE_IDLE) {
        ESP_LOGE(TAG, "Cannot start hosting - already active");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Clear device list
    clear_device_list();
    
    // Start BLE advertising
    esp_err_t ret = ble_start_advertising();
    if (ret == ESP_OK) {
        current_state = LOBBY_STATE_HOSTING;
        ESP_LOGI(TAG, "Started hosting lobby");
        
        if (event_callback) {
            event_callback(LOBBY_EVENT_DEVICE_FOUND, NULL, 0);
        }
    }
    
    return ret;
}

// Stop hosting
esp_err_t lobby_stop_hosting(void)
{
    if (current_state != LOBBY_STATE_HOSTING) {
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t ret = ble_stop_advertising();
    if (ret == ESP_OK) {
        current_state = LOBBY_STATE_IDLE;
        clear_device_list();
        ESP_LOGI(TAG, "Stopped hosting lobby");
    }
    
    return ret;
}

// Start scanning for devices
esp_err_t lobby_start_scanning(void)
{
    if (lobby_config.role != LOBBY_ROLE_CLIENT) {
        ESP_LOGE(TAG, "Cannot scan - configured as host");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (current_state != LOBBY_STATE_IDLE) {
        ESP_LOGE(TAG, "Cannot start scanning - already active");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Clear device list
    clear_device_list();
    
    // Start BLE scanning
    esp_err_t ret = ble_start_scanning();
    if (ret == ESP_OK) {
        current_state = LOBBY_STATE_SCANNING;
        ESP_LOGI(TAG, "Started scanning for devices");
        
        // Set timeout for scanning
        xEventGroupSetBits(lobby_event_group, 0);
        
        // In a real implementation, we'd set a timer here
        // For now, we'll rely on the BLE scanning timeout
    }
    
    return ret;
}

// Stop scanning
esp_err_t lobby_stop_scanning(void)
{
    if (current_state != LOBBY_STATE_SCANNING) {
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t ret = ble_stop_scanning();
    if (ret == ESP_OK) {
        current_state = LOBBY_STATE_IDLE;
        ESP_LOGI(TAG, "Stopped scanning for devices");
    }
    
    return ret;
}

// Connect to a device
esp_err_t lobby_connect_to_device(const uint8_t *addr)
{
    if (current_state != LOBBY_STATE_SCANNING) {
        return ESP_ERR_INVALID_STATE;
    }
    
    lobby_device_t device;
    if (!find_device_by_addr(addr, &device)) {
        ESP_LOGE(TAG, "Device not found");
        return ESP_ERR_NOT_FOUND;
    }
    
    current_state = LOBBY_STATE_CONNECTING;
    ESP_LOGI(TAG, "Connecting to device %02X:%02X:%02X:%02X:%02X:%02X",
             addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
    
    // In a real implementation, we'd initiate BLE connection here
    // For now, we'll simulate success
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // Add to session
    current_session.players[1] = device;
    current_session.player_count = 2;
    current_state = LOBBY_STATE_CONNECTED;
    
    if (event_callback) {
        event_callback(LOBBY_EVENT_CONNECTION_SUCCESS, addr, 6);
        event_callback(LOBBY_EVENT_PLAYER_JOINED, addr, 6);
    }
    
    return ESP_OK;
}

// Accept connection
esp_err_t lobby_accept_connection(const uint8_t *addr)
{
    if (current_state != LOBBY_STATE_HOSTING) {
        return ESP_ERR_INVALID_STATE;
    }
    
    lobby_device_t device;
    if (!find_device_by_addr(addr, &device)) {
        return ESP_ERR_NOT_FOUND;
    }
    
    current_session.players[1] = device;
    current_session.player_count = 2;
    current_session.host_player_id = 0;
    
    if (event_callback) {
        event_callback(LOBBY_EVENT_PLAYER_JOINED, addr, 6);
    }
    
    return ESP_OK;
}

// Reject connection
esp_err_t lobby_reject_connection(const uint8_t *addr)
{
    ESP_LOGI(TAG, "Rejected connection from %02X:%02X:%02X:%02X:%02X:%02X",
             addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
    
    if (event_callback) {
        event_callback(LOBBY_EVENT_CONNECTION_FAILED, addr, 6);
    }
    
    return ESP_OK;
}

// Start game
esp_err_t lobby_start_game(void)
{
    if (current_state != LOBBY_STATE_CONNECTED) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (current_session.player_count < 2) {
        return ESP_ERR_INVALID_STATE;
    }
    
    current_session.ready_to_start = true;
    
    if (event_callback) {
        event_callback(LOBBY_EVENT_GAME_START, NULL, 0);
    }
    
    ESP_LOGI(TAG, "Game starting with %d players", current_session.player_count);
    return ESP_OK;
}

// Get current state
lobby_state_t lobby_get_state(void)
{
    return current_state;
}

// Get device count
uint8_t lobby_get_device_count(void)
{
    return device_count;
}

// Get device list
const lobby_device_t* lobby_get_device_list(void)
{
    return device_list;
}

// Get session info
const game_session_t* lobby_get_session_info(void)
{
    return &current_session;
}

// Register event callback
void lobby_register_callback(lobby_event_callback_t callback)
{
    event_callback = callback;
}

// BLE event handler
static void handle_ble_event(uint8_t event_type, const uint8_t *data, uint16_t length)
{
    switch (event_type) {
        case 0: // Device found
            if (length >= 7) {
                const char *name = (const char*)(data + 6);
                int8_t rssi = (int8_t)data[0];
                add_device_to_list((const esp_ble_addr_t*)(data + 1), name, rssi, false);
                
                if (event_callback) {
                    event_callback(LOBBY_EVENT_DEVICE_FOUND, data + 1, 6);
                }
            }
            break;
            
        case 1: // Device lost
            if (length >= 6) {
                remove_device_from_list((const esp_ble_addr_t*)data);
                
                if (event_callback) {
                    event_callback(LOBBY_EVENT_DEVICE_LOST, data, 6);
                }
            }
            break;
            
        case 2: // Connected
            current_state = LOBBY_STATE_CONNECTED;
            if (event_callback) {
                event_callback(LOBBY_EVENT_CONNECTED, data, 6);
            }
            break;
            
        case 3: // Disconnected
            current_state = LOBBY_STATE_IDLE;
            current_session.player_count = 1;
            if (event_callback) {
                event_callback(LOBBY_EVENT_DISCONNECTED, data, 6);
            }
            break;
    }
}

// Device management functions
static void add_device_to_list(const esp_ble_addr_t *addr, const char *name, int8_t rssi, bool is_host)
{
    if (device_count >= LOBBY_MAX_DEVICES) {
        return;
    }
    
    // Check if device already exists
    for (int i = 0; i < device_count; i++) {
        if (memcmp(device_list[i].addr, addr->val, 6) == 0) {
            // Update existing device
            strncpy(device_list[i].device_name, name, LOBBY_DEVICE_NAME_MAX_LEN - 1);
            device_list[i].rssi = rssi;
            device_list[i].last_seen = esp_timer_get_time() / 1000;
            return;
        }
    }
    
    // Add new device
    memcpy(device_list[device_count].addr, addr->val, 6);
    strncpy(device_list[device_count].device_name, name, LOBBY_DEVICE_NAME_MAX_LEN - 1);
    device_list[device_count].rssi = rssi;
    device_list[device_count].last_seen = esp_timer_get_time() / 1000;
    device_list[device_count].is_host = is_host;
    device_count++;
    
    ESP_LOGI(TAG, "Added device: %s (%02X:%02X:%02X:%02X:%02X:%02X), RSSI: %d",
             name, addr->val[0], addr->val[1], addr->val[2],
             addr->val[3], addr->val[4], addr->val[5], rssi);
}

static void remove_device_from_list(const esp_ble_addr_t *addr)
{
    for (int i = 0; i < device_count; i++) {
        if (memcmp(device_list[i].addr, addr->val, 6) == 0) {
            // Shift remaining devices
            for (int j = i; j < device_count - 1; j++) {
                device_list[j] = device_list[j + 1];
            }
            device_count--;
            break;
        }
    }
}

static bool find_device_by_addr(const uint8_t *addr, lobby_device_t *device)
{
    for (int i = 0; i < device_count; i++) {
        if (memcmp(device_list[i].addr, addr, 6) == 0) {
            if (device) {
                *device = device_list[i];
            }
            return true;
        }
    }
    return false;
}

static void clear_device_list(void)
{
    memset(device_list, 0, sizeof(device_list));
    device_count = 0;
}

// Utility functions
const char* lobby_state_to_string(lobby_state_t state)
{
    switch (state) {
        case LOBBY_STATE_IDLE: return "IDLE";
        case LOBBY_STATE_HOSTING: return "HOSTING";
        case LOBBY_STATE_SCANNING: return "SCANNING";
        case LOBBY_STATE_CONNECTING: return "CONNECTING";
        case LOBBY_STATE_CONNECTED: return "CONNECTED";
        case LOBBY_STATE_ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

const char* lobby_event_to_string(lobby_event_type_t event)
{
    switch (event) {
        case LOBBY_EVENT_DEVICE_FOUND: return "DEVICE_FOUND";
        case LOBBY_EVENT_DEVICE_LOST: return "DEVICE_LOST";
        case LOBBY_EVENT_CONNECTION_REQUEST: return "CONNECTION_REQUEST";
        case LOBBY_EVENT_CONNECTION_SUCCESS: return "CONNECTION_SUCCESS";
        case LOBBY_EVENT_CONNECTION_FAILED: return "CONNECTION_FAILED";
        case LOBBY_EVENT_PLAYER_JOINED: return "PLAYER_JOINED";
        case LOBBY_EVENT_PLAYER_LEFT: return "PLAYER_LEFT";
        case LOBBY_EVENT_GAME_START: return "GAME_START";
        case LOBBY_EVENT_TIMEOUT: return "TIMEOUT";
        default: return "UNKNOWN";
    }
}

// Connection quality
int8_t lobby_get_connection_rssi(const uint8_t *addr)
{
    lobby_device_t device;
    if (find_device_by_addr(addr, &device)) {
        return device.rssi;
    }
    return -128; // Invalid RSSI
}

bool lobby_is_device_connected(const uint8_t *addr)
{
    for (int i = 0; i < current_session.player_count; i++) {
        if (memcmp(current_session.players[i].addr, addr, 6) == 0) {
            return true;
        }
    }
    return false;
}