#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#define EPD_TEST_PATTERN_GRAY_LOADER_X 86
#define EPD_TEST_PATTERN_GRAY_LOADER_Y 664
#define EPD_TEST_PATTERN_GRAY_LOADER_W 308
#define EPD_TEST_PATTERN_GRAY_LOADER_H 72

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
