#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "ink_cpfont.h"

#define EPD_TEST_PATTERN_GRAY_LOADER_X 86
#define EPD_TEST_PATTERN_GRAY_LOADER_Y 664
#define EPD_TEST_PATTERN_GRAY_LOADER_W 308
#define EPD_TEST_PATTERN_GRAY_LOADER_H 72

typedef bool (*epd_test_pattern_should_abort_fn)(void *ctx);

void epd_test_pattern_fill_stripes(uint8_t *buffer, size_t length);
void epd_test_pattern_fill_layout(uint8_t *buffer, size_t length);
void epd_test_pattern_fill_text_demo(uint8_t *buffer, size_t length);
void epd_test_pattern_fill_gray_demo_bw(uint8_t *buffer, size_t length, uint8_t loader_step);
void epd_test_pattern_fill_gray_demo_planes(
    uint8_t *lsb_buffer,
    size_t lsb_length,
    uint8_t *msb_buffer,
    size_t msb_length
);
bool epd_test_pattern_gray_demo_self_test(void);
bool epd_test_pattern_reader_page_self_test(void);
bool epd_test_pattern_copy_page_buffer(
    uint8_t *buffer,
    size_t length,
    const uint8_t *page_buffer,
    size_t page_buffer_length
);
bool epd_test_pattern_copy_page_buffer_self_test(void);
bool epd_test_pattern_footer_overlay_self_test(void);
void epd_test_pattern_fill_text_page(
    uint8_t *buffer,
    size_t length,
    const char *title,
    const char *line1,
    const char *line2,
    const char *line3,
    const char *line4,
    const char *line5
);
void epd_test_pattern_fill_text_page_with_font(
    uint8_t *buffer,
    size_t length,
    const ink_cpfont_t *font,
    epd_test_pattern_should_abort_fn should_abort,
    void *should_abort_ctx,
    const char *title,
    const char *line1,
    const char *line2,
    const char *line3,
    const char *line4,
    const char *line5
);
void epd_test_pattern_fill_reader_page_with_font(
    uint8_t *buffer,
    size_t length,
    const ink_cpfont_t *font,
    epd_test_pattern_should_abort_fn should_abort,
    void *should_abort_ctx,
    const char *title,
    const char *line1,
    const char *line2,
    const char *line3,
    const char *line4,
    const char *status
);
void epd_test_pattern_draw_footer_overlay(
    uint8_t *buffer,
    size_t length,
    const ink_cpfont_t *font,
    const char *left_text,
    const char *right_text
);
void epd_test_pattern_draw_footer_probe(
    uint8_t *buffer,
    size_t length,
    const char *seed_text
);
void epd_test_pattern_fill_grid_compare_base_page(
    uint8_t *buffer,
    size_t length,
    const char *sweep_tag
);
void epd_test_pattern_apply_grid_compare_cell(
    uint8_t *buffer,
    size_t length,
    uint8_t cell_index,
    const char *sweep_tag
);
