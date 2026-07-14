#ifndef INK_EPD_UI_HOST_INK_CPFONT_H
#define INK_EPD_UI_HOST_INK_CPFONT_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct ink_cpfont {
  bool loaded;
  uint8_t advance_y;
} ink_cpfont_t;

bool ink_cpfont_is_loaded(const ink_cpfont_t *font);
esp_err_t ink_cpfont_draw_text_bw_scaled(ink_cpfont_t *font,
                                         uint8_t *buffer, int x, int top_y,
                                         const char *text,
                                         uint8_t scale_divisor,
                                         int *out_width_px);

#endif
