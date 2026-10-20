#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "app_state.h"

typedef enum {
  UI_ACTION_FOCUS_MAGPIE,
  UI_ACTION_REFRESH,
  UI_ACTION_RECONNECT,
} ui_action_t;

typedef void (*ui_action_handler_t)(ui_action_t action);

void ui_create(ui_action_handler_t handler);
void ui_update(const app_state_t *state, uint32_t elapsed_seconds);
void ui_cycle_page(void);
void ui_show_quick_actions(void);
void ui_show_toast(const char *text);
void ui_show_pairing_code(uint32_t passkey);
void ui_hide_pairing_code(void);
bool ui_pairing_code_visible(void);
bool ui_modal_visible(void);
