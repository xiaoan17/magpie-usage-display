/*
 * Hardware mapping and panel init adapted from esp-mosaico/esp-mosaico-bsp.
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 * Local integration modifications: see repository ATTRIBUTION.md.
 */
#include "board.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_efuse.h"
#include "esp_efuse_table.h"
#include "esp_lcd_co5300.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_touch_cst9220.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "board";

// ESP-Mosaico CoreBoard pin map. v1.0 boards swap the I2C pair and the
// LCD clock/reset pair; the factory firmware reads that from eFuse USER_DATA.
#define HW_VERSION_V1_0 0x0100

typedef struct {
  gpio_num_t lcd_sclk, lcd_d0, lcd_d1, lcd_d2, lcd_d3;
  gpio_num_t lcd_cs, lcd_rst;
  gpio_num_t i2c_sda, i2c_scl;
} board_pins_t;

static const board_pins_t kPinsV1_0 = {
    .lcd_sclk = GPIO_NUM_44, .lcd_d0 = GPIO_NUM_36, .lcd_d1 = GPIO_NUM_51,
    .lcd_d2 = GPIO_NUM_35,   .lcd_d3 = GPIO_NUM_9,
    .lcd_cs = GPIO_NUM_50,   .lcd_rst = GPIO_NUM_42,
    .i2c_sda = GPIO_NUM_0,   .i2c_scl = GPIO_NUM_1,
};

static const board_pins_t kPinsV1_2 = {
    .lcd_sclk = GPIO_NUM_42, .lcd_d0 = GPIO_NUM_36, .lcd_d1 = GPIO_NUM_51,
    .lcd_d2 = GPIO_NUM_35,   .lcd_d3 = GPIO_NUM_9,
    .lcd_cs = GPIO_NUM_50,   .lcd_rst = GPIO_NUM_44,
    .i2c_sda = GPIO_NUM_56,  .i2c_scl = GPIO_NUM_3,
};

#define LCD_VCC_3V3_CTRL GPIO_NUM_60  // low-active 3.3 V rail for panels
#define TOUCH_INT_GPIO GPIO_NUM_6
#define LCD_TE_GPIO GPIO_NUM_43

static const board_pins_t *s_pins = &kPinsV1_2;
static i2c_master_bus_handle_t s_i2c_bus;
static esp_lcd_panel_handle_t s_panel;
static esp_lcd_touch_handle_t s_touch;
static SemaphoreHandle_t s_flush_done;
static bool s_screen_on;
static volatile bool s_touch_irq;

// Panel-specific sequence from the official ESP-Mosaico BSP onboard/display.c.
// The generic CO5300 default is for a different panel window.
static const co5300_lcd_init_cmd_t s_panel_init[] = {
    {0x11, NULL, 0, 600},
    {0xFE, (uint8_t[]){0x20}, 1, 0},
    {0x19, (uint8_t[]){0x10}, 1, 0},
    {0x1C, (uint8_t[]){0xA0}, 1, 0},
    {0xFE, (uint8_t[]){0x00}, 1, 0},
    {0xC4, (uint8_t[]){0x80}, 1, 0},
    {0x3A, (uint8_t[]){0x55}, 1, 0},
    {0x53, (uint8_t[]){0x20}, 1, 0},
    {0x51, (uint8_t[]){0xFF}, 1, 0},
    {0x63, (uint8_t[]){0xFF}, 1, 0},
    {0x2A, (uint8_t[]){0x00, 0x00, 0x01, 0xDF}, 4, 0},
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0xDF}, 4, 0},
    {0x29, NULL, 0, 600},
};

static bool IRAM_ATTR color_transfer_done(esp_lcd_panel_io_handle_t io,
                                          esp_lcd_panel_io_event_data_t *event,
                                          void *context) {
  (void)io;
  (void)event;
  (void)context;
  BaseType_t wake = pdFALSE;
  xSemaphoreGiveFromISR(s_flush_done, &wake);
  return wake == pdTRUE;
}

static void IRAM_ATTR touch_isr(void *arg) { s_touch_irq = true; }

static uint16_t read_hardware_version(void) {
  uint16_t version = 0;
  if (esp_efuse_read_field_blob(ESP_EFUSE_USER_DATA, &version,
                                sizeof(version) * 8U) != ESP_OK) {
    return 0;
  }
  return version;
}

static esp_err_t lcd_power_on(void) {
  // VCC_3V3 for the panel is gated by GPIO60 (active low).
  gpio_config_t cfg = {
      .pin_bit_mask = 1ULL << LCD_VCC_3V3_CTRL,
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  ESP_RETURN_ON_ERROR(gpio_config(&cfg), TAG, "LCD rail GPIO config failed");
  ESP_RETURN_ON_ERROR(gpio_set_level(LCD_VCC_3V3_CTRL, 1), TAG,
                      "LCD rail high failed");
  vTaskDelay(pdMS_TO_TICKS(5));
  ESP_RETURN_ON_ERROR(gpio_set_level(LCD_VCC_3V3_CTRL, 0), TAG,
                      "LCD rail enable failed");
  vTaskDelay(pdMS_TO_TICKS(20));
  return ESP_OK;
}

static esp_err_t display_init(void) {
  s_flush_done = xSemaphoreCreateBinary();
  ESP_RETURN_ON_FALSE(s_flush_done, ESP_ERR_NO_MEM, TAG, "flush semaphore failed");
  spi_bus_config_t bus_cfg = CO5300_PANEL_BUS_QSPI_CONFIG(
      s_pins->lcd_sclk, s_pins->lcd_d0, s_pins->lcd_d1, s_pins->lcd_d2,
      s_pins->lcd_d3, 480 * 20 * sizeof(uint16_t));
  ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO),
                      TAG, "QSPI bus init failed");

  esp_lcd_panel_io_handle_t io = NULL;
  esp_lcd_panel_io_spi_config_t io_cfg = {
      .cs_gpio_num = s_pins->lcd_cs,
      .dc_gpio_num = -1,
      .spi_mode = 0,
      .pclk_hz = 40 * 1000 * 1000,
      .trans_queue_depth = 2,
      .on_color_trans_done = color_transfer_done,
      .user_ctx = NULL,
      .lcd_cmd_bits = 32,
      .lcd_param_bits = 8,
      .cs_ena_pretrans = 0,
      .cs_ena_posttrans = 0,
      .flags = {
          .quad_mode = true,
          .psram_dma_direct = true,
      },
  };
  ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST,
                                               &io_cfg, &io),
                      TAG, "panel IO failed");

  co5300_vendor_config_t vendor = {
      .init_cmds = s_panel_init,
      .init_cmds_size = sizeof(s_panel_init) / sizeof(s_panel_init[0]),
      .flags = {.use_qspi_interface = 1},
  };
  esp_lcd_panel_dev_config_t panel_cfg = {
      .reset_gpio_num = s_pins->lcd_rst,
      .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
      .data_endian = LCD_RGB_DATA_ENDIAN_BIG,
      .bits_per_pixel = 16,
      .vendor_config = &vendor,
  };
  ESP_RETURN_ON_ERROR(esp_lcd_new_panel_co5300(io, &panel_cfg, &s_panel), TAG,
                      "CO5300 panel create failed");
  ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "panel reset failed");
  ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "panel init failed");
  ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG,
                      "panel on failed");
  ESP_RETURN_ON_ERROR(esp_lcd_panel_co5300_set_brightness(s_panel, 70), TAG,
                      "brightness failed");
  ESP_LOGI(TAG, "CO5300 480x480 QSPI ready SCLK=%d RST=%d",
           s_pins->lcd_sclk, s_pins->lcd_rst);
  return ESP_OK;
}

esp_err_t board_init(void) {
  const uint16_t version = read_hardware_version();
  s_pins = (version == HW_VERSION_V1_0) ? &kPinsV1_0 : &kPinsV1_2;
  ESP_LOGI(TAG, "hardware version 0x%04X, using %s pin map", version,
           s_pins == &kPinsV1_0 ? "v1.0" : "v1.2");

  ESP_RETURN_ON_ERROR(lcd_power_on(), TAG, "LCD power failed");
  ESP_RETURN_ON_ERROR(display_init(), TAG, "display init failed");

  i2c_master_bus_config_t i2c_cfg = {
      .i2c_port = I2C_NUM_0,
      .sda_io_num = s_pins->i2c_sda,
      .scl_io_num = s_pins->i2c_scl,
      .clk_source = I2C_CLK_SRC_DEFAULT,
      .glitch_ignore_cnt = 7,
      .flags = {.enable_internal_pullup = 1},
  };
  ESP_RETURN_ON_ERROR(i2c_new_master_bus(&i2c_cfg, &s_i2c_bus), TAG,
                      "I2C bus init failed");

  esp_lcd_panel_io_handle_t touch_io = NULL;
  esp_lcd_panel_io_i2c_config_t touch_io_cfg =
      ESP_LCD_TOUCH_IO_I2C_CST9220_CONFIG();
  touch_io_cfg.transaction_timeout_ms = 50;
  ESP_RETURN_ON_ERROR(
      esp_lcd_new_panel_io_i2c(s_i2c_bus, &touch_io_cfg, &touch_io),
      TAG, "touch IO failed");

  esp_lcd_touch_config_t touch_cfg = {
      .x_max = MOSAICO_LCD_H_RES - 1,
      .y_max = MOSAICO_LCD_V_RES - 1,
      .rst_gpio_num = -1,
      .int_gpio_num = TOUCH_INT_GPIO,
      .levels = {.reset = 0, .interrupt = 0},
      .flags = {.swap_xy = 0, .mirror_x = 0, .mirror_y = 0},
  };
  ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_cst9220(touch_io, &touch_cfg,
                                                    &s_touch),
                      TAG, "CST9220 init failed");

  gpio_config_t int_cfg = {
      .pin_bit_mask = 1ULL << TOUCH_INT_GPIO,
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .intr_type = GPIO_INTR_NEGEDGE,
  };
  ESP_RETURN_ON_ERROR(gpio_config(&int_cfg), TAG, "touch INT config failed");
  gpio_install_isr_service(0);
  gpio_isr_handler_add(TOUCH_INT_GPIO, touch_isr, NULL);
  ESP_LOGI(TAG, "CST9220 touch ready");
  s_screen_on = true;
  return ESP_OK;
}

void board_set_screen_on(bool on) {
  if (s_screen_on == on) {
    return;
  }
  s_screen_on = on;
  if (s_panel == NULL) {
    return;
  }
  if (on) {
    esp_lcd_panel_disp_on_off(s_panel, true);
    esp_lcd_panel_co5300_set_brightness(s_panel, 70);
  } else {
    esp_lcd_panel_co5300_set_brightness(s_panel, 0);
    esp_lcd_panel_disp_on_off(s_panel, false);
  }
}

bool board_screen_on(void) { return s_screen_on; }

bool board_take_touch_irq(void) {
  if (!s_touch_irq) {
    return false;
  }
  s_touch_irq = false;
  return true;
}

uint8_t board_battery_percent(void) { return 0xFF; }

bool board_battery_charging(void) { return false; }

esp_err_t board_draw_bitmap(int x1, int y1, int x2, int y2, const void *data) {
  ESP_RETURN_ON_ERROR(
      esp_lcd_panel_draw_bitmap(s_panel, x1, y1, x2, y2, data),
      TAG, "panel draw failed");
  // LVGL may reuse its buffer only after SPI DMA has consumed all pixels.
  ESP_RETURN_ON_FALSE(xSemaphoreTake(s_flush_done, pdMS_TO_TICKS(2000)) == pdTRUE,
                      ESP_ERR_TIMEOUT, TAG, "panel transfer timed out");
  return ESP_OK;
}

bool board_touch_read(uint16_t *x, uint16_t *y) {
  if (s_touch == NULL) {
    return false;
  }
  if (esp_lcd_touch_read_data(s_touch) != ESP_OK) {
    return false;
  }
  esp_lcd_touch_point_data_t point;
  uint8_t count = 0;
  if (esp_lcd_touch_get_data(s_touch, &point, &count, 1) != ESP_OK ||
      count == 0) {
    return false;
  }
  *x = point.x;
  *y = point.y;
  return true;
}
