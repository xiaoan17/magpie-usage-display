#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "rgb565_transport.h"

int main(void) {
  // Native little-endian RGB565 for red, green, blue, white, black.
  uint8_t pixels[] = {0x00, 0xf8, 0xe0, 0x07, 0x1f, 0x00,
                      0xff, 0xff, 0x00, 0x00, 0x5a};
  const uint8_t wire[] = {0xf8, 0x00, 0x07, 0xe0, 0x00, 0x1f,
                          0xff, 0xff, 0x00, 0x00, 0x5a};
  uint8_t original[sizeof(pixels)];
  memcpy(original, pixels, sizeof(pixels));
  rgb565_swap_bytes(pixels, 5);
  assert(memcmp(pixels, wire, sizeof(pixels)) == 0);
  rgb565_swap_bytes(pixels, 5);
  assert(memcmp(pixels, original, sizeof(pixels)) == 0);
  rgb565_swap_bytes(pixels, 0);
  assert(memcmp(pixels, original, sizeof(pixels)) == 0);
  puts("RGB565 primary colors, bounds and LVGL buffer restoration: PASS");
}
