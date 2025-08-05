#include "gatt.h"
#include "esp_log.h"
#include "nimble/ble.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "utils.h"

static const char *TAG = "gatt";

// GATT service UUIDs
static const ble_uuid128_t gatt_service_uuid =
    BLE_UUID128_INIT(0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0,
                     0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88);

static const ble_uuid128_t game_state_char_uuid =
    BLE_UUID128_INIT(0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0,
                     0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x89);

static const ble_uuid128_t input_char_uuid =
    BLE_UUID128_INIT(0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0,
                     0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x8a);

static const ble_uuid128_t config_char_uuid =
    BLE_UUID128_INIT(0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0,
                     0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x8b);

// GATT service definition with extended characteristics
static const struct ble_gatt_svc_def gatt_services_extended[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &gatt_service_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &game_state_char_uuid.u,
                .access_cb = gatt_game_state_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_INDICATE,
                .val_handle = NULL,
                .descriptors = (struct ble_gatt_dsc_def[]) {
                    {
                        .uuid = BLE_UUID16_DECLARE(GATT_CLIENT_CHAR_CFG_UUID),
                        .access_cb = gatt_ccc_access,
                        .att_flags = BLE_ATT_F_READ | BLE_ATT_F_WRITE,
                    },
                    {
                        0, // No more descriptors
                    }
                }
            },
            {
                .uuid = &input_char_uuid.u,
                .access_cb = gatt_input_access,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = NULL,
                .descriptors = (struct ble_gatt_dsc_def[]) {
                    {
                        .uuid = BLE_UUID16_DECLARE(GATT_CLIENT_CHAR_CFG_UUID),
                        .access_cb = gatt_ccc_access,
                        .att_flags = BLE_ATT_F_READ | BLE_ATT_F_WRITE,
                    },
                    {
                        0, // No more descriptors
                    }
                }
            },
            {
                .uuid = &config_char_uuid.u,
                .access_cb = gatt_config_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = NULL,
                .descriptors = (struct ble_gatt_dsc_def[]) {
                    {
                        .uuid = BLE_UUID16_DECLARE(GATT_CLIENT_CHAR_CFG_UUID),
                        .access_cb = gatt_ccc_access,
                        .att_flags = BLE_ATT_F_READ | BLE_ATT_F_WRITE,
                    },
                    {
                        0, // No more descriptors
                    }
                }
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

// Characteristic data storage
game_state_packet_t gatt_game_state_data = {0};
input_packet_t gatt_input_data = {0};
config_packet_t gatt_config_data = {0};

// Client Characteristic Configuration (CCC) values
static uint16_t gatt_game_state_ccc = 0;
static uint16_t gatt_input_ccc = 0;
static uint16_t gatt_config_ccc = 0;

// Access callbacks
static int gatt_game_state_access(uint16_t conn_handle, uint16_t attr_handle,
                                 struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    int rc = 0;
    
    switch (ctxt->op) {
        case BLE_GATT_ACCESS_OP_READ_CHR:
            // Update checksum before sending
            gatt_game_state_data.checksum = crc16((uint8_t *)&gatt_game_state_data, 
                                                 sizeof(gatt_game_state_data) - sizeof(uint16_t));
            
            rc = os_mbuf_append(ctxt->om, &gatt_game_state_data, sizeof(gatt_game_state_data));
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
            
        case BLE_GATT_ACCESS_OP_WRITE_CHR:
            if (ctxt->om->om_len != sizeof(game_state_packet_t)) {
                return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
            }
            
            memcpy(&gatt_game_state_data, ctxt->om->om_data, sizeof(gatt_game_state_data));
            
            // Verify checksum
            uint16_t expected_crc = crc16((uint8_t *)&gatt_game_state_data,
                                        sizeof(gatt_game_state_data) - sizeof(uint16_t));
            if (gatt_game_state_data.checksum != expected_crc) {
                ESP_LOGE(TAG, "Game state checksum mismatch");
                return BLE_ATT_ERR_UNLIKELY;
            }
            
            return 0;
            
        default:
            return BLE_ATT_ERR_UNLIKELY;
    }
}

static int gatt_input_access(uint16_t conn_handle, uint16_t attr_handle,
                            struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    int rc = 0;
    
    switch (ctxt->op) {
        case BLE_GATT_ACCESS_OP_WRITE_CHR:
            if (ctxt->om->om_len != sizeof(input_packet_t)) {
                return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
            }
            
            memcpy(&gatt_input_data, ctxt->om->om_data, sizeof(input_packet_t));
            
            // Verify checksum
            uint16_t expected_crc = crc16((uint8_t *)&gatt_input_data,
                                        sizeof(input_packet_t) - sizeof(uint16_t));
            if (gatt_input_data.checksum != expected_crc) {
                ESP_LOGE(TAG, "Input checksum mismatch");
                return BLE_ATT_ERR_UNLIKELY;
            }
            
            return 0;
            
        case BLE_GATT_ACCESS_OP_READ_CHR:
            // Update checksum before sending
            gatt_input_data.checksum = crc16((uint8_t *)&gatt_input_data,
                                           sizeof(input_packet_t) - sizeof(uint16_t));
            
            rc = os_mbuf_append(ctxt->om, &gatt_input_data, sizeof(input_packet_t));
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
            
        default:
            return BLE_ATT_ERR_UNLIKELY;
    }
}

static int gatt_config_access(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    int rc = 0;
    
    switch (ctxt->op) {
        case BLE_GATT_ACCESS_OP_READ_CHR:
            // Update checksum before sending
            gatt_config_data.checksum = crc32((uint8_t *)&gatt_config_data,
                                            sizeof(config_packet_t) - sizeof(uint32_t));
            
            rc = os_mbuf_append(ctxt->om, &gatt_config_data, sizeof(config_packet_t));
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
            
        case BLE_GATT_ACCESS_OP_WRITE_CHR:
            if (ctxt->om->om_len != sizeof(config_packet_t)) {
                return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
            }
            
            memcpy(&gatt_config_data, ctxt->om->om_data, sizeof(config_packet_t));
            
            // Verify checksum
            uint32_t expected_crc = crc32((uint8_t *)&gatt_config_data,
                                        sizeof(config_packet_t) - sizeof(uint32_t));
            if (gatt_config_data.checksum != expected_crc) {
                ESP_LOGE(TAG, "Config checksum mismatch");
                return BLE_ATT_ERR_UNLIKELY;
            }
            
            ESP_LOGI(TAG, "Config updated - Track: %d, Laps: %d, Mode: %d",
                     gatt_config_data.track_id, gatt_config_data.lap_count,
                     gatt_config_data.game_mode);
            
            return 0;
            
        default:
            return BLE_ATT_ERR_UNLIKELY;
    }
}

static int gatt_ccc_access(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    uint16_t *ccc_value = (uint16_t *)arg;
    
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_DSC) {
        return os_mbuf_append(ctxt->om, ccc_value, sizeof(*ccc_value));
    } else if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_DSC) {
        if (ctxt->om->om_len != sizeof(*ccc_value)) {
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        memcpy(ccc_value, ctxt->om->om_data, sizeof(*ccc_value));
        return 0;
    }
    
    return BLE_ATT_ERR_UNLIKELY;
}

// Utility functions for GATT operations
esp_err_t gatt_init_services(void)
{
    int rc;
    
    ESP_LOGI(TAG, "Initializing extended GATT services");
    
    rc = ble_gatts_count_cfg(gatt_services_extended);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to count extended GATT services: %d", rc);
        return ESP_FAIL;
    }
    
    rc = ble_gatts_add_svcs(gatt_services_extended);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to add extended GATT services: %d", rc);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Extended GATT services initialized");
    return ESP_OK;
}

esp_err_t gatt_notify_game_state(uint16_t conn_handle, const game_state_packet_t *state)
{
    int rc;
    
    if (gatt_game_state_ccc == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Update local data
    memcpy(&gatt_game_state_data, state, sizeof(game_state_packet_t));
    
    // Calculate checksum
    gatt_game_state_data.checksum = crc16((uint8_t *)&gatt_game_state_data,
                                        sizeof(game_state_packet_t) - sizeof(uint16_t));
    
    rc = ble_gatts_notify(conn_handle, gatt_game_state_val_handle,
                         (uint8_t *)&gatt_game_state_data, sizeof(game_state_packet_t));
    
    return rc == 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t gatt_notify_input(uint16_t conn_handle, const input_packet_t *input)
{
    int rc;
    
    if (gatt_input_ccc == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Update local data
    memcpy(&gatt_input_data, input, sizeof(input_packet_t));
    
    // Calculate checksum
    gatt_input_data.checksum = crc16((uint8_t *)&gatt_input_data,
                                   sizeof(input_packet_t) - sizeof(uint16_t));
    
    rc = ble_gatts_notify(conn_handle, gatt_input_val_handle,
                         (uint8_t *)&gatt_input_data, sizeof(input_packet_t));
    
    return rc == 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t gatt_notify_config(uint16_t conn_handle, const config_packet_t *config)
{
    int rc;
    
    if (gatt_config_ccc == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Update local data
    memcpy(&gatt_config_data, config, sizeof(config_packet_t));
    
    // Calculate checksum
    gatt_config_data.checksum = crc32((uint8_t *)&gatt_config_data,
                                    sizeof(config_packet_t) - sizeof(uint32_t));
    
    rc = ble_gatts_notify(conn_handle, gatt_config_val_handle,
                         (uint8_t *)&gatt_config_data, sizeof(config_packet_t));
    
    return rc == 0 ? ESP_OK : ESP_FAIL;
}

// Initialize GATT data with defaults
void gatt_init_data(void)
{
    memset(&gatt_game_state_data, 0, sizeof(gatt_game_state_data));
    memset(&gatt_input_data, 0, sizeof(gatt_input_data));
    memset(&gatt_config_data, 0, sizeof(gatt_config_data));
    
    // Set default configuration
    gatt_config_data.config_type = 1;  // Default game configuration
    gatt_config_data.track_id = 0;     // Default track
    gatt_config_data.lap_count = 3;    // 3 laps
    gatt_config_data.game_mode = 0;    // Standard race
    gatt_config_data.latency_target = 80;  // 80ms target latency
    gatt_config_data.update_rate = 30;     // 30Hz updates
    
    ESP_LOGI(TAG, "GATT data initialized with defaults");
}