#ifndef _GATT_H_
#define _GATT_H_

#include <stdint.h>
#include "esp_err.h"
#include "ble.h"

// GATT service initialization
esp_err_t gatt_init_services(void);
void gatt_init_data(void);

// Notification functions
esp_err_t gatt_notify_game_state(uint16_t conn_handle, const game_state_packet_t *state);
esp_err_t gatt_notify_input(uint16_t conn_handle, const input_packet_t *input);
esp_err_t gatt_notify_config(uint16_t conn_handle, const config_packet_t *config);

// Access callbacks
int gatt_game_state_access(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg);
int gatt_input_access(uint16_t conn_handle, uint16_t attr_handle,
                     struct ble_gatt_access_ctxt *ctxt, void *arg);
int gatt_config_access(uint16_t conn_handle, uint16_t attr_handle,
                      struct ble_gatt_access_ctxt *ctxt, void *arg);
int gatt_ccc_access(uint16_t conn_handle, uint16_t attr_handle,
                   struct ble_gatt_access_ctxt *ctxt, void *arg);

// External variables for GATT data
extern game_state_packet_t gatt_game_state_data;
extern input_packet_t gatt_input_data;
extern config_packet_t gatt_config_data;

#endif // _GATT_H_