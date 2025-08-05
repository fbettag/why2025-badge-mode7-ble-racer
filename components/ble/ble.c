#include "ble.h"
#include "esp_log.h"
#include "esp_nimble_hci.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "services/ans/ble_svc_ans.h"
#include "utils.h"

static const char *TAG = "ble";

// BLE state variables
static ble_state_t ble_state = BLE_STATE_IDLE;
static uint16_t ble_connection_handle = BLE_HS_CONN_HANDLE_NONE;
static ble_event_callback_t ble_event_cb = NULL;
static uint16_t ble_connection_interval = 0;
static uint16_t ble_latency = 0;

// GATT service definition
static const struct ble_gatt_svc_def gatt_services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(BLE_SERVICE_UUID),
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = BLE_UUID16_DECLARE(BLE_GAME_STATE_CHAR_UUID),
                .access_cb = ble_gatt_char_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &game_state_val_handle,
            },
            {
                .uuid = BLE_UUID16_DECLARE(BLE_INPUT_CHAR_UUID),
                .access_cb = ble_gatt_char_access,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &input_val_handle,
            },
            {
                .uuid = BLE_UUID16_DECLARE(BLE_CONFIG_CHAR_UUID),
                .access_cb = ble_gatt_char_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &config_val_handle,
            },
            {
                0, // No more characteristics
            }
        }
    },
    {
        0, // No more services
    }
};

// GATT characteristic handles
static uint16_t game_state_val_handle = 0;
static uint16_t input_val_handle = 0;
static uint16_t config_val_handle = 0;

// Forward declarations
static int ble_gap_event(struct ble_gap_event *event, void *arg);
static int ble_gatt_char_access(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt, void *arg);
static bool ble_validate_connection(void);
static void ble_advertise(void);

esp_err_t ble_init(void)
{
    int rc;
    
    ESP_LOGI(TAG, "Initializing BLE stack");
    
    // Initialize NimBLE host configuration
    ble_hs_cfg.reset_cb = ble_on_reset;
    ble_hs_cfg.sync_cb = ble_on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    
    // Initialize NimBLE stack
    rc = nimble_port_init();
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to init nimble port: %d", rc);
        return ESP_FAIL;
    }
    
    // Initialize GATT services
    rc = ble_gatts_count_cfg(gatt_services);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to count GATT services: %d", rc);
        return ESP_FAIL;
    }
    
    rc = ble_gatts_add_svcs(gatt_services);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to add GATT services: %d", rc);
        return ESP_FAIL;
    }
    
    // Set device name
    rc = ble_svc_gap_device_name_set(BLE_DEVICE_NAME);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to set device name: %d", rc);
        return ESP_FAIL;
    }
    
    // Start NimBLE host task
    nimble_port_freertos_init(ble_host_task);
    
    ESP_LOGI(TAG, "BLE stack initialized successfully");
    return ESP_OK;
}

void ble_deinit(void)
{
    ESP_LOGI(TAG, "Deinitializing BLE stack");
    
    if (ble_state == BLE_STATE_ADVERTISING) {
        ble_stop_advertising();
    }
    
    if (ble_state == BLE_STATE_CONNECTED) {
        ble_gap_terminate(ble_connection_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
    
    nimble_port_deinit();
    ble_state = BLE_STATE_IDLE;
}

esp_err_t ble_start_advertising(void)
{
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    const char *device_name = BLE_DEVICE_NAME;
    int rc;
    
    ESP_LOGI(TAG, "Starting BLE advertising");
    
    // Set advertising data
    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids16 = (ble_uuid16_t[]) {
        BLE_UUID16_INIT(BLE_SERVICE_UUID)
    };
    fields.num_uuids16 = 1;
    fields.uuids16_is_complete = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    fields.tx_pwr_lvl_is_present = 1;
    fields.name = (uint8_t *)device_name;
    fields.name_len = strlen(device_name);
    fields.name_is_complete = 1;
    
    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to set advertising data: %d", rc);
        return ESP_FAIL;
    }
    
    // Set scan response data
    memset(&fields, 0, sizeof(fields));
    fields.name = (uint8_t *)device_name;
    fields.name_len = strlen(device_name);
    fields.name_is_complete = 1;
    fields.mfg_data = (uint8_t *)BLE_MANUFACTURER_DATA;
    fields.mfg_data_len = strlen(BLE_MANUFACTURER_DATA);
    
    rc = ble_gap_adv_rsp_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to set scan response data: %d", rc);
        return ESP_FAIL;
    }
    
    // Configure advertising parameters
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    adv_params.itvl_min = BLE_GAP_ADV_FAST_INTERVAL1_MIN;
    adv_params.itvl_max = BLE_GAP_ADV_FAST_INTERVAL1_MAX;
    adv_params.channel_map = 0x07; // All 3 channels
    
    // Start advertising
    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                          &adv_params, ble_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to start advertising: %d", rc);
        return ESP_FAIL;
    }
    
    ble_state = BLE_STATE_ADVERTISING;
    ESP_LOGI(TAG, "BLE advertising started");
    
    return ESP_OK;
}

esp_err_t ble_stop_advertising(void)
{
    int rc;
    
    ESP_LOGI(TAG, "Stopping BLE advertising");
    
    rc = ble_gap_adv_stop();
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to stop advertising: %d", rc);
        return ESP_FAIL;
    }
    
    ble_state = BLE_STATE_IDLE;
    ESP_LOGI(TAG, "BLE advertising stopped");
    
    return ESP_OK;
}

esp_err_t ble_start_scanning(void)
{
    struct ble_gap_disc_params disc_params;
    int rc;
    
    ESP_LOGI(TAG, "Starting BLE scanning");
    
    memset(&disc_params, 0, sizeof(disc_params));
    disc_params.filter_duplicates = 1;
    disc_params.passive = 0;
    disc_params.itvl = BLE_GAP_SCAN_FAST_INTERVAL_MIN;
    disc_params.window = BLE_GAP_SCAN_FAST_WINDOW;
    disc_params.filter_policy = BLE_GAP_SCAN_FP_ACCEPT_ALL;
    
    rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, BLE_HS_FOREVER, &disc_params,
                     ble_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to start scanning: %d", rc);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "BLE scanning started");
    return ESP_OK;
}

esp_err_t ble_stop_scanning(void)
{
    int rc;
    
    ESP_LOGI(TAG, "Stopping BLE scanning");
    
    rc = ble_gap_disc_cancel();
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to stop scanning: %d", rc);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "BLE scanning stopped");
    return ESP_OK;
}

esp_err_t ble_send_game_state(const game_state_packet_t *state)
{
    if (!state) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!ble_validate_connection()) {
        return ESP_ERR_INVALID_STATE;
    }
    
    int rc = ble_gatts_notify(ble_connection_handle, game_state_val_handle,
                             (uint8_t *)state, sizeof(game_state_packet_t));
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to send game state: %d", rc);
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

esp_err_t ble_send_input(const input_packet_t *input)
{
    if (!input) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!ble_validate_connection()) {
        return ESP_ERR_INVALID_STATE;
    }
    
    int rc = ble_gatts_notify(ble_connection_handle, input_val_handle,
                             (uint8_t *)input, sizeof(input_packet_t));
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to send input: %d", rc);
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

esp_err_t ble_send_config(const config_packet_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!ble_validate_connection()) {
        return ESP_ERR_INVALID_STATE;
    }
    
    int rc = ble_gatts_notify(ble_connection_handle, config_val_handle,
                             (uint8_t *)config, sizeof(config_packet_t));
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to send config: %d", rc);
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

ble_state_t ble_get_state(void)
{
    return ble_state;
}

bool ble_is_connected(void)
{
    return (ble_state == BLE_STATE_CONNECTED && 
            ble_connection_handle != BLE_HS_CONN_HANDLE_NONE);
}

static bool ble_validate_connection(void)
{
    if (ble_connection_handle == BLE_HS_CONN_HANDLE_NONE) {
        ESP_LOGW(TAG, "Invalid connection handle");
        return false;
    }
    
    if (ble_state != BLE_STATE_CONNECTED) {
        ESP_LOGW(TAG, "BLE not in connected state");
        return false;
    }
    
    return true;
}

uint16_t ble_get_connection_interval(void)
{
    return ble_connection_interval;
}

uint16_t ble_get_latency(void)
{
    return ble_latency;
}

void ble_register_callback(ble_event_callback_t callback)
{
    ble_event_cb = callback;
}

uint16_t ble_calculate_latency(void)
{
    // Calculate latency based on connection interval and slave latency
    if (ble_connection_interval > 0) {
        return ble_connection_interval * (1 + ble_latency);
    }
    return 0;
}

void ble_update_connection_parameters(uint16_t interval, uint16_t latency, uint16_t timeout)
{
    struct ble_gap_upd_params params;
    int rc;
    
    if (!ble_is_connected()) {
        return;
    }
    
    memset(&params, 0, sizeof(params));
    params.itvl_min = interval;
    params.itvl_max = interval;
    params.latency = latency;
    params.supervision_timeout = timeout;
    
    rc = ble_gap_update_params(ble_connection_handle, &params);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to update connection parameters: %d", rc);
    }
}

// BLE event handler
static int ble_gap_event(struct ble_gap_event *event, void *arg)
{
    struct ble_gap_conn_desc desc;
    int rc;
    
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                ble_state = BLE_STATE_CONNECTED;
                ble_connection_handle = event->connect.conn_handle;
                
                rc = ble_gap_conn_find(ble_connection_handle, &desc);
                if (rc == 0) {
                    ble_connection_interval = desc.conn_itvl;
                    ble_latency = desc.conn_latency;
                }
                
                ESP_LOGI(TAG, "BLE connected, handle=%d", ble_connection_handle);
                
                if (ble_event_cb) {
                    ble_event_cb(0, NULL, 0); // Connected event
                }
            } else {
                ESP_LOGE(TAG, "BLE connection failed: %d", event->connect.status);
                ble_state = BLE_STATE_IDLE;
            }
            break;
            
        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(TAG, "BLE disconnected: %d", event->disconnect.reason);
            ble_state = BLE_STATE_DISCONNECTED;
            ble_connection_handle = 0xFFFF;
            
            if (ble_event_cb) {
                ble_event_cb(1, NULL, 0); // Disconnected event
            }
            break;
            
        case BLE_GAP_EVENT_ADV_COMPLETE:
            ESP_LOGI(TAG, "BLE advertising complete");
            if (ble_state == BLE_STATE_ADVERTISING) {
                ble_state = BLE_STATE_IDLE;
            }
            break;
            
        case BLE_GAP_EVENT_CONN_UPDATE:
            ESP_LOGI(TAG, "Connection parameters updated");
            break;
            
        default:
            break;
    }
    
    return 0;
}

// GATT characteristic access callback
static int ble_gatt_char_access(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    uint16_t uuid = ble_uuid_u16(ctxt->chr->uuid);
    
    switch (uuid) {
        case BLE_GAME_STATE_CHAR_UUID:
            if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
                // Handle incoming game state
                if (ble_event_cb && ctxt->om->om_len == sizeof(game_state_packet_t)) {
                    ble_event_cb(2, ctxt->om->om_data, ctxt->om->om_len);
                }
            }
            break;
            
        case BLE_INPUT_CHAR_UUID:
            if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
                // Handle incoming input
                if (ble_event_cb && ctxt->om->om_len == sizeof(input_packet_t)) {
                    ble_event_cb(3, ctxt->om->om_data, ctxt->om->om_len);
                }
            }
            break;
            
        case BLE_CONFIG_CHAR_UUID:
            if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
                // Handle incoming config
                if (ble_event_cb && ctxt->om->om_len == sizeof(config_packet_t)) {
                    ble_event_cb(4, ctxt->om->om_data, ctxt->om->om_len);
                }
            }
            break;
            
        default:
            break;
    }
    
    return 0;
}

// NimBLE host task
void ble_host_task(void *param)
{
    ESP_LOGI(TAG, "BLE host task started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

// BLE reset callback
void ble_on_reset(int reason)
{
    ESP_LOGI(TAG, "BLE reset: %d", reason);
}

// BLE sync callback
void ble_on_sync(void)
{
    ESP_LOGI(TAG, "BLE synchronized");
    
    // Enable 2M PHY for better performance
    int rc = ble_gap_set_prefered_default_le_phy(BLE_GAP_LE_PHY_2M_MASK, BLE_GAP_LE_PHY_2M_MASK);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to set 2M PHY preference: %d", rc);
    }
    
    // Configure connection parameters for low latency
    struct ble_gap_conn_params conn_params = {
        .scan_itvl = 0x0010,
        .scan_window = 0x0010,
        .itvl_min = BLE_GAP_INITIAL_CONN_ITVL_MIN,
        .itvl_max = BLE_GAP_INITIAL_CONN_ITVL_MAX,
        .latency = 0,
        .supervision_timeout = BLE_GAP_INITIAL_SUPERVISION_TIMEOUT,
        .min_ce_len = BLE_GAP_INITIAL_CONN_MIN_CE_LEN,
        .max_ce_len = BLE_GAP_INITIAL_CONN_MAX_CE_LEN,
    };
    
    rc = ble_gap_set_default_conn_params(&conn_params);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to set default connection parameters: %d", rc);
    }
}