#include "ink_epd_ui.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
  LOADING_IMAGE_X = 140,
  LOADING_IMAGE_Y = 380,
  LOADING_IMAGE_WIDTH = 200,
  LOADING_IMAGE_HEIGHT = 40,
  LOADING_IMAGE_ROW_BYTES = LOADING_IMAGE_WIDTH / 8,
};

bool ink_cpfont_is_loaded(const ink_cpfont_t *font) {
  return font && font->loaded;
}

esp_err_t ink_cpfont_draw_text_bw_scaled(ink_cpfont_t *font,
                                         uint8_t *buffer, int x, int top_y,
                                         const char *text,
                                         uint8_t scale_divisor,
                                         int *out_width_px) {
  (void)buffer;
  (void)x;
  (void)top_y;
  if (!font || !font->loaded || !text || scale_divisor == 0U)
    return ESP_FAIL;
  if (out_width_px) *out_width_px = 0;
  return ESP_OK;
}

bool ink_epd_ui_reader_self_test(void) { return true; }

static bool pixel_is_black(const uint8_t *buffer, int x, int y) {
  const size_t index = (size_t)y * (INK_EPD_WIDTH / 8) + (size_t)x / 8;
  const uint8_t mask = (uint8_t)(0x80U >> (x & 7));
  return !(buffer[index] & mask);
}

static uint32_t loading_image_fingerprint(const uint8_t *buffer) {
  uint32_t hash = UINT32_C(2166136261);
  for (int y = 0; y < LOADING_IMAGE_HEIGHT; ++y) {
    for (int byte_x = 0; byte_x < LOADING_IMAGE_ROW_BYTES; ++byte_x) {
      uint8_t packed = 0;
      for (int bit = 0; bit < 8; ++bit) {
        if (pixel_is_black(buffer, LOADING_IMAGE_X + byte_x * 8 + bit,
                           LOADING_IMAGE_Y + y))
          packed |= (uint8_t)(0x80U >> bit);
      }
      hash = (hash ^ packed) * UINT32_C(16777619);
    }
  }
  return hash;
}

static bool loading_region_test(void) {
  const ink_epd_region_t region = ink_epd_ui_loading_region();
  return region.x == 132 && region.y == 372 && region.width == 216 &&
         region.height == 56;
}

static bool loading_draw_test(void) {
  uint8_t buffer[INK_EPD_BUFFER_SIZE];
  memset(buffer, 0x00, sizeof(buffer));

  ink_epd_ui_draw_loading(buffer, sizeof(buffer));

  const bool outside_unchanged =
      pixel_is_black(buffer, 131, 371) && pixel_is_black(buffer, 348, 428);
  const bool padding_is_white =
      !pixel_is_black(buffer, 132, 372) &&
      !pixel_is_black(buffer, 139, 379) &&
      !pixel_is_black(buffer, 347, 427) &&
      !pixel_is_black(buffer, 132, 400) &&
      !pixel_is_black(buffer, 347, 400);
  const bool key_pixels_match =
      !pixel_is_black(buffer, 140, 380) &&
      pixel_is_black(buffer, 150, 390) &&
      pixel_is_black(buffer, 165, 392) &&
      pixel_is_black(buffer, 240, 400) &&
      pixel_is_black(buffer, 290, 400) &&
      pixel_is_black(buffer, 330, 410) &&
      !pixel_is_black(buffer, 339, 419);

  return outside_unchanged && padding_is_white && key_pixels_match &&
         loading_image_fingerprint(buffer) == UINT32_C(0xC912932C);
}

int main(void) {
  if (!loading_region_test()) {
    fprintf(stderr, "loading region test failed\n");
    return 1;
  }
  if (!loading_draw_test()) {
    fprintf(stderr, "loading draw test failed\n");
    return 1;
  }
  puts("PASS: loading UI host test");
  return 0;
}
