#pragma once

#include <stddef.h>
#include <stdint.h>

// The CO5300 SPI wire format is most-significant byte first. Keep LVGL and
// image assets in native byte order, and swap only at the transport boundary.
static inline void rgb565_swap_bytes(void *pixels, size_t count) {
  uint8_t *bytes = pixels;
  for (size_t i = 0; i < count; ++i) {
    const uint8_t low = bytes[2 * i];
    bytes[2 * i] = bytes[2 * i + 1];
    bytes[2 * i + 1] = low;
  }
}
