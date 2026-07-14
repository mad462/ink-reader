#include "ink_epd_ui.h"

#include <stdio.h>

bool ink_cpfont_is_loaded(const ink_cpfont_t *font) {
  return font && font->loaded;
}

esp_err_t ink_cpfont_draw_text_bw_scaled(ink_cpfont_t *font,
                                         uint8_t *buffer, int x, int top_y,
                                         const char *text,
                                         uint8_t scale_divisor,
                                         int *out_width_px) {
  (void)font;
  (void)buffer;
  (void)x;
  (void)top_y;
  (void)text;
  (void)scale_divisor;
  (void)out_width_px;
  return ESP_FAIL;
}

int main(void) {
  if (!ink_epd_ui_reader_self_test()) {
    fprintf(stderr, "reader UI self test failed\n");
    return 1;
  }
  puts("PASS: reader UI host self test");
  return 0;
}
