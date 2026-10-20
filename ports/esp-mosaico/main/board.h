#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "lvgl.h"

// ESP-Mosaico CoreBoard, ESP32-S31 + CO5300 480x480 QSPI AMOLED + CST9220.
//
// Pin map is selected from the hardware version burned into eFuse USER_DATA
// (unit under test reports 0x0102, i.e. CoreBoard v1.2).

#define MOSAICO_LCD_H_RES 480
#define MOSAICO_LCD_V_RES 480

esp_err_t board_init(void);
void board_set_screen_on(bool on);
bool board_screen_on(void);

// Returns true once per touch-interrupt edge, clearing the pending flag.
bool board_take_touch_irq(void);

uint8_t board_battery_percent(void);
bool board_battery_charging(void);

esp_err_t board_draw_bitmap(int x1, int y1, int x2, int y2, const void *data);
bool board_touch_read(uint16_t *x, uint16_t *y);
