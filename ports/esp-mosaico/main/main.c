// Magpie Usage Display for ESP-Mosaico (ESP32-S31 + CO5300 480x480).
//
// Port of https://github.com/peterroe/magpie-usage-display (MIT) from the
// Waveshare ESP32-S3-Touch-AMOLED-2.16 / Arduino build to ESP-IDF + NimBLE.
#include <stdio.h>
#include <string.h>

#include "app_state.h"
#include "ble_bridge.h"
#include "board.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "nvs_flash.h"
#include "rgb565_transport.h"
#include "ui.h"

static const char *TAG = "app";

#define UI_REFRESH_MS 1000
#define DATA_STALE_MS (60 * 1000)
#define IDLE_SCREEN_TIMEOUT_MS (60 * 1000)
#define BUTTON_DEBOUNCE_MS 35
#define AI_BUTTON_GPIO 7       // AI_BUTTON (middle), active low
#define BOOT_BUTTON_GPIO 61    // BOOT, active low
#define BOOT_CLEAR_PAIRING_MS 3000
#define AI_LONG_PRESS_MS 1000

static app_state_t s_state;
static uint32_t s_state_received_at_ms;
static uint32_t s_last_ui_refresh_ms;
static uint32_t s_last_activity_ms;
static lv_disp_draw_buf_t s_draw_buffer;
static lv_color_t *s_draw_buf_a;
static lv_color_t *s_draw_buf_b;

static uint32_t now_ms(void) {
  return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static void increase_buffer(lv_disp_drv_t *drv, lv_area_t *area) {
  (void)drv;
  // Match the official Mosaico QSPI area alignment.
  area->x1 = (area->x1 / 4) * 4;
  area->x2 = ((area->x2 + 4) / 4) * 4 - 1;
  if (area->x2 >= MOSAICO_LCD_H_RES) area->x2 = MOSAICO_LCD_H_RES - 1;
  if (area->y1 % 2 != 0) --area->y1;
  if (area->y2 % 2 == 0) ++area->y2;
}

static void display_flush(lv_disp_drv_t *drv, const lv_area_t *area,
                          lv_color_t *pixels) {
  const size_t count = (size_t)(area->x2 - area->x1 + 1) *
                       (size_t)(area->y2 - area->y1 + 1);
#if LV_COLOR_DEPTH != 16
#error "The display transport requires RGB565."
#endif
#if !LV_COLOR_16_SWAP
  rgb565_swap_bytes(pixels, count);
#endif
  ESP_ERROR_CHECK(board_draw_bitmap(
      area->x1, area->y1, area->x2 + 1, area->y2 + 1, pixels));
#if !LV_COLOR_16_SWAP
  // board_draw_bitmap waits for DMA, so it is now safe to restore the buffer.
  rgb565_swap_bytes(pixels, count);
#endif
  lv_disp_flush_ready(drv);
}

static void touch_read(lv_indev_drv_t *drv, lv_indev_data_t *data) {
  (void)drv;
  uint16_t x = 0, y = 0;
  if (board_touch_read(&x, &y)) {
    s_last_activity_ms = now_ms();
    board_set_screen_on(true);
    data->state = LV_INDEV_STATE_PR;
    data->point.x = x;
    data->point.y = y;
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
}

static void handle_ui_action(ui_action_t action) {
  const char *command = NULL;
  switch (action) {
    case UI_ACTION_FOCUS_MAGPIE:
      command = "focus_magpie";
      break;
    case UI_ACTION_REFRESH:
      command = "refresh";
      break;
    case UI_ACTION_RECONNECT:
      ble_start_advertising();
      ui_show_toast(ble_connected() ? "BLE CONNECTED" : "BLE ADVERTISING");
      return;
  }
  ESP_LOGI(TAG, "action %s", command);
  if (ble_send_action(command)) {
    ui_show_toast("REQUEST SENT");
  } else {
    ui_show_toast("COMPANION OFFLINE");
  }
}

static bool init_lvgl(void) {
  lv_init();
  const size_t pixels = MOSAICO_LCD_H_RES * 40;
  s_draw_buf_a = heap_caps_malloc(pixels * sizeof(lv_color_t),
                                  MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  s_draw_buf_b = heap_caps_malloc(pixels * sizeof(lv_color_t),
                                  MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  if (s_draw_buf_a == NULL || s_draw_buf_b == NULL) {
    ESP_LOGE(TAG, "LVGL draw buffer allocation failed");
    return false;
  }
  lv_disp_draw_buf_init(&s_draw_buffer, s_draw_buf_a, s_draw_buf_b, pixels);

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = MOSAICO_LCD_H_RES;
  disp_drv.ver_res = MOSAICO_LCD_V_RES;
  disp_drv.flush_cb = display_flush;
  disp_drv.rounder_cb = increase_buffer;
  disp_drv.draw_buf = &s_draw_buffer;
  lv_disp_drv_register(&disp_drv);

  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = touch_read;
  lv_indev_drv_register(&indev_drv);

  ui_create(handle_ui_action);
  s_state_received_at_ms = now_ms();
  ui_update(&s_state, 0);
  return true;
}

static void handle_buttons(void) {
  static uint8_t ai_prev = 1, boot_prev = 1;
  static uint32_t ai_pressed_at = 0, boot_pressed_at = 0;
  static bool ai_long_sent = false, boot_long_sent = false;
  static uint32_t ai_changed_at = 0, boot_changed_at = 0;
  const uint32_t now = now_ms();

  const uint8_t ai = gpio_get_level(AI_BUTTON_GPIO);
  if (ai != ai_prev) {
    ai_prev = ai;
    ai_changed_at = now;
  }
  if (now - ai_changed_at >= BUTTON_DEBOUNCE_MS) {
    if (ai == 0) {
      if (ai_pressed_at == 0) {
        ai_pressed_at = now;
        ai_long_sent = false;
      } else if (!ai_long_sent && now - ai_pressed_at >= AI_LONG_PRESS_MS) {
        ai_long_sent = true;
        s_last_activity_ms = now;
        board_set_screen_on(true);
        ui_show_quick_actions();
      }
    } else if (ai_pressed_at != 0) {
      if (!ai_long_sent) {
        s_last_activity_ms = now;
        const bool was_on = board_screen_on();
        board_set_screen_on(true);
        if (was_on && !ui_modal_visible()) {
          ui_cycle_page();
        }
      }
      ai_pressed_at = 0;
    }
  }

  const uint8_t boot = gpio_get_level(BOOT_BUTTON_GPIO);
  if (boot != boot_prev) {
    boot_prev = boot;
    boot_changed_at = now;
  }
  if (now - boot_changed_at >= BUTTON_DEBOUNCE_MS) {
    if (boot == 0) {
      if (boot_pressed_at == 0) {
        boot_pressed_at = now;
        boot_long_sent = false;
      } else if (!boot_long_sent &&
                 now - boot_pressed_at >= BOOT_CLEAR_PAIRING_MS) {
        boot_long_sent = true;
        board_set_screen_on(true);
        const bool cleared = ble_clear_bonds();
        ui_show_toast(cleared ? "PAIRING CLEARED" : "CLEAR FAILED");
        lv_timer_handler();
        if (cleared) {
          vTaskDelay(pdMS_TO_TICKS(750));
          esp_restart();
        }
      }
    } else if (boot_pressed_at != 0) {
      if (!boot_long_sent) {
        s_last_activity_ms = now;
        board_set_screen_on(!board_screen_on());
      }
      boot_pressed_at = 0;
    }
  }
}

static void update_ble(void) {
  ble_maintain_connection();

  app_state_t received;
  if (ble_take_state(&received)) {
    s_state = received;
    s_state_received_at_ms = now_ms();
    ui_hide_pairing_code();
  }

  if (!ble_connected()) {
    s_state.connection = CONN_DISCONNECTED;
  } else if (!s_state.data_valid) {
    s_state.connection = CONN_SYNCING;
  } else if (now_ms() - s_state_received_at_ms >= DATA_STALE_MS) {
    s_state.connection = CONN_STALE;
  } else {
    s_state.connection = CONN_LINKED;
  }

  ble_action_result_t result;
  if (ble_take_action_result(&result)) {
    ui_show_toast(result.message);
  }

  uint32_t passkey = 0;
  if (ble_take_pairing_code(&passkey)) {
    board_set_screen_on(true);
    s_last_activity_ms = now_ms();
    ui_show_pairing_code(passkey);
  }

  bool pairing_success = false;
  if (ble_take_pairing_finished(&pairing_success)) {
    ui_hide_pairing_code();
    ui_show_toast(pairing_success ? "PAIRED" : "PAIRING FAILED");
  }
}

static void refresh_ui(void) {
  if (!board_screen_on()) {
    return;
  }
  const uint32_t now = now_ms();
  if (now - s_last_ui_refresh_ms < UI_REFRESH_MS) {
    return;
  }
  s_last_ui_refresh_ms = now;
  ui_update(&s_state, (now - s_state_received_at_ms) / 1000UL);
}

void app_main(void) {
  ESP_LOGI(TAG, "Magpie Usage Display starting (ESP-Mosaico / ESP32-S31)");

  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);

  gpio_config_t btn_cfg = {
      .pin_bit_mask = (1ULL << AI_BUTTON_GPIO) | (1ULL << BOOT_BUTTON_GPIO),
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_config(&btn_cfg);

  if (board_init() != ESP_OK) {
    ESP_LOGE(TAG, "board init failed");
    return;
  }
  if (!init_lvgl()) {
    ESP_LOGE(TAG, "LVGL init failed");
    return;
  }
  if (!ble_begin()) {
    ui_show_toast("BLE INIT FAILED");
  }

  s_last_activity_ms = now_ms();
  ESP_LOGI(TAG, "ready");

  while (true) {
    handle_buttons();
    update_ble();
    refresh_ui();
    lv_timer_handler();

    if (board_screen_on() && !ui_pairing_code_visible() &&
        now_ms() - s_last_activity_ms >= IDLE_SCREEN_TIMEOUT_MS) {
      board_set_screen_on(false);
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}
