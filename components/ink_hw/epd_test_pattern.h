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

typedef struct {
    int x;
    int y;
    int w;
    int h;
} epd_test_pattern_rect_t;

typedef struct {
    int header_h;
    int gutter_x;
    int title_y;
    int meta_y;
    int divider_y;
} epd_test_pattern_header_layout_t;

typedef struct {
    int list_x;
    int list_y;
    int row_w;
    int row_h;
    int row_gap;
    int visible_rows;
    bool compact_rows;
    int content_x_inset;
    int marker_top_inset;
    int marker_bottom_inset;
    int title_y_offset;
    int line1_y_offset;
    int line2_y_offset;
} epd_test_pattern_list_layout_t;

typedef struct {
    const char *title;
    const char *meta;
    const ink_cpfont_t *title_font;
    const ink_cpfont_t *meta_font;
} epd_test_pattern_header_spec_t;

typedef struct {
    const char *title;
    const char *line1;
    const char *line2;
    bool selected;
    bool emphasized;
} epd_test_pattern_list_row_t;

typedef struct {
    const char *title;
    const char *meta;
    const epd_test_pattern_list_row_t *rows;
    size_t row_count;
    const ink_cpfont_t *title_font;
    const ink_cpfont_t *meta_font;
    const ink_cpfont_t *row_title_font;
    const ink_cpfont_t *row_meta_font;
} epd_test_pattern_rows_page_spec_t;

#define EPD_TEST_PATTERN_MENU_TAB_CAPACITY 3
#define EPD_TEST_PATTERN_MENU_CARD_CAPACITY 8
#define EPD_TEST_PATTERN_MENU_ACTION_CAPACITY 4

typedef struct {
    char label[16];
    bool active;
    bool focused;
} epd_test_pattern_menu_tab_t;

typedef struct {
    char title[96];
    char line1[64];
    char line2[64];
    bool selected;
    bool trailing_favorite;
} epd_test_pattern_menu_card_t;

typedef struct {
    char label[24];
    bool selected;
} epd_test_pattern_menu_action_t;

typedef struct {
    bool tabs_focus;
    bool compact_cards;
    bool bookmark_cards_tall;
    bool frameless_panel;
    char header_title[32];
    char header_meta[24];
    size_t tab_count;
    epd_test_pattern_menu_tab_t tabs[EPD_TEST_PATTERN_MENU_TAB_CAPACITY];
    size_t card_count;
    epd_test_pattern_menu_card_t cards[EPD_TEST_PATTERN_MENU_CARD_CAPACITY];
    bool action_popup_open;
    char action_popup_title[96];
    size_t action_count;
    epd_test_pattern_menu_action_t actions[EPD_TEST_PATTERN_MENU_ACTION_CAPACITY];
} epd_test_pattern_reader_menu_overlay_t;

epd_test_pattern_header_layout_t epd_test_pattern_crosspoint_header_layout(void);
epd_test_pattern_list_layout_t epd_test_pattern_crosspoint_list_layout(void);
void epd_test_pattern_truncate_text_middle(
    const char *src,
    char *dst,
    size_t dst_size,
    size_t max_chars
);
void epd_test_pattern_truncate_text_tail(
    const char *src,
    char *dst,
    size_t dst_size,
    size_t max_chars
);
void epd_test_pattern_draw_crosspoint_header(
    uint8_t *buffer,
    const epd_test_pattern_header_spec_t *spec
);
void epd_test_pattern_draw_crosspoint_list_row(
    uint8_t *buffer,
    const epd_test_pattern_list_layout_t *layout,
    size_t row_index,
    const epd_test_pattern_list_row_t *row,
    const ink_cpfont_t *title_font,
    const ink_cpfont_t *meta_font
);
void epd_test_pattern_fill_crosspoint_rows_page(
    uint8_t *buffer,
    size_t length,
    const epd_test_pattern_rows_page_spec_t *spec
);

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
void epd_test_pattern_fill_gray_calibration_page(uint8_t *buffer, size_t length);
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
void epd_test_pattern_draw_reader_menu_overlay(
    uint8_t *buffer,
    size_t length,
    const ink_cpfont_t *menu_font,
    const ink_cpfont_t *footer_font,
    const epd_test_pattern_reader_menu_overlay_t *overlay
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
