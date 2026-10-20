#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "app_state.h"

typedef struct {
  char message[49];
} ble_action_result_t;

// Protocol v4, see magpie-usage-display/docs/BLE_PROTOCOL.md.
bool ble_begin(void);
bool ble_connected(void);
void ble_maintain_connection(void);
void ble_start_advertising(void);

bool ble_take_state(app_state_t *state);
bool ble_take_action_result(ble_action_result_t *result);
bool ble_take_pairing_code(uint32_t *passkey);
bool ble_take_pairing_finished(bool *success);
bool ble_send_action(const char *action);
bool ble_clear_bonds(void);
