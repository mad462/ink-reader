#include "epd_test_pattern.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "epd_gdey0426t82.h"

typedef struct {
    char c;
    uint8_t rows[7];
} epd_glyph_t;

static const epd_glyph_t s_font[] = {
    {'#', {0x0A, 0x0A, 0x1F, 0x0A, 0x1F, 0x0A, 0x0A}},
    {'%', {0x19, 0x19, 0x02, 0x04, 0x08, 0x13, 0x13}},
    {' ', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {'-', {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}},
    {'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C}},
    {'/', {0x01, 0x02, 0x02, 0x04, 0x08, 0x10, 0x10}},
    {':', {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00}},
    {'_', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F}},
    {'0', {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}},
    {'1', {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}},
    {'3', {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E}},
    {'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}},
    {'5', {0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E}},
    {'6', {0x0E, 0x10, 0x10, 0x1E, 0x11, 0x11, 0x0E}},
    {'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
    {'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}},
    {'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x0E}},
    {'A', {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'B', {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}},
    {'C', {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}},
    {'D', {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}},
    {'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}},
    {'F', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}},
    {'G', {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0E}},
    {'H', {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'I', {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'J', {0x01, 0x01, 0x01, 0x01, 0x11, 0x11, 0x0E}},
    {'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}},
    {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}},
    {'M', {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}},
    {'N', {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}},
    {'O', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}},
    {'Q', {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}},
    {'R', {0x1E, 0x11, 0x11, 0x1E, 0x12, 0x11, 0x11}},
    {'S', {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}},
    {'T', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
    {'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'V', {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}},
    {'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}},
    {'X', {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}},
    {'Y', {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}},
    {'Z', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}}
};

enum {
    GRAY_BAR_X = 44,
    GRAY_BAR_WIDTH = 392,
    GRAY_BAR_HEIGHT = 92,
    GRAY_BAR_GAP = 28,
    GRAY_BAR_Y0 = 118,
    GRAY_LABEL_Y = 534,
    GRAY_LOADER_BOX_W = 68,
    GRAY_LOADER_BOX_H = 28,
    GRAY_LOADER_BOX_GAP = 24,
    GRAY_LOADER_BOX_Y = EPD_TEST_PATTERN_GRAY_LOADER_Y + 28,
    GRAY_LOADER_BOX_X0 = 110,
    READER_SUBLINE_COUNT = 8,
    READER_SUBLINE_UNITS = 20,
    FOOTER_BAND_Y = 780,
    FOOTER_BAND_H = 20,
    FOOTER_TEXT_Y = 782,
    FOOTER_LEFT_X = 8,
    FOOTER_RIGHT_MARGIN = 8,
    FOOTER_BITMAP_SCALE = 2,
    FOOTER_RIGHT_RESERVED_W = 156,
    FOOTER_PROBE_X = 436,
    FOOTER_PROBE_Y = 782,
    FOOTER_PROBE_W = 36,
    FOOTER_PROBE_H = 14,
    GRID_COMPARE_COLS = 4,
    GRID_COMPARE_ROWS = 4,
    GRID_COMPARE_X0 = 16,
    GRID_COMPARE_Y0 = 72,
    GRID_COMPARE_W = 104,
    GRID_COMPARE_H = 166,
    GRID_COMPARE_GAP_X = 6,
    GRID_COMPARE_GAP_Y = 8,
    GRID_COMPARE_LABEL_SCALE = 2,
    GRID_COMPARE_LABEL_X_PAD = 8,
    GRID_COMPARE_LABEL_Y_PAD = 8,
    GRID_COMPARE_TEXT_X_PAD = 10,
    GRID_COMPARE_TEXT_Y_PAD = 58,
    GRID_COMPARE_TEXT_LINE_H = 22,
};

static const int s_reader_line_y[READER_SUBLINE_COUNT] = {
    110, 178, 246, 314, 382, 450, 518, 586
};

static size_t epd_utf8_codepoint_length(unsigned char lead);
static void draw_ui_text(
    uint8_t *buffer,
    const ink_cpfont_t *font,
    int x,
    int y,
    const char *text,
    int fallback_scale,
    uint8_t font_scale_divisor,
    bool right_aligned
);
static void fill_rect(uint8_t *buffer, int x, int y, int w, int h, bool black);
static void epd_draw_text_right_aligned_maybe_font_scaled(
    uint8_t *buffer,
    const ink_cpfont_t *font,
    int right_edge_x,
    int y,
    const char *text,
    int fallback_scale,
    uint8_t font_scale_divisor
);
static void epd_draw_text_maybe_font_scaled(
    uint8_t *buffer,
    const ink_cpfont_t *font,
    int x,
    int y,
    const char *text,
    int fallback_scale,
    uint8_t font_scale_divisor
);
static void epd_draw_text_maybe_font_scaled_inverted(
    uint8_t *buffer,
    const ink_cpfont_t *font,
    int x,
    int y,
    const char *text,
    int fallback_scale,
    uint8_t font_scale_divisor
);
static size_t epd_min_size(size_t a, size_t b);

static uint32_t epd_hash_text(const char *text)
{
    uint32_t hash = 5381U;

    if (text == NULL) {
        return hash;
    }
    for (const unsigned char *p = (const unsigned char *)text; *p != '\0'; ++p) {
        hash = ((hash << 5) + hash) ^ (uint32_t)(*p);
    }
    return hash;
}

epd_test_pattern_header_layout_t epd_test_pattern_crosspoint_header_layout(void)
{
    epd_test_pattern_header_layout_t layout = {
        .header_h = 68,
        .gutter_x = 24,
        .title_y = 24,
        .meta_y = 28,
        .divider_y = 66,
    };

    return layout;
}

epd_test_pattern_list_layout_t epd_test_pattern_crosspoint_list_layout(void)
{
    epd_test_pattern_list_layout_t layout = {
        .list_x = 24,
        .list_y = 84,
        .row_w = EPD_GDEY0426T82_WIDTH - 48,
        .row_h = 48,
        .row_gap = 6,
        .visible_rows = 10,
    };

    return layout;
}

void epd_test_pattern_truncate_text_middle(
    const char *src,
    char *dst,
    size_t dst_size,
    size_t max_chars)
{
    size_t src_len = src != NULL ? strlen(src) : 0U;

    if (dst == NULL || dst_size == 0U) {
        return;
    }
    if (src == NULL || src_len == 0U || max_chars == 0U) {
        dst[0] = '\0';
        return;
    }
    if (src_len <= max_chars) {
        snprintf(dst, dst_size, "%s", src);
        return;
    }

    if (max_chars <= 3U) {
        snprintf(dst, dst_size, "%.*s", (int)epd_min_size(src_len, max_chars), src);
        return;
    }

    {
        const size_t visible_chars = max_chars - 3U;
        const size_t head = visible_chars / 2U + (visible_chars % 2U);
        const size_t tail = visible_chars / 2U;
        const size_t tail_start = src_len > tail ? src_len - tail : 0U;

        snprintf(dst, dst_size, "%.*s...%s", (int)head, src, src + tail_start);
    }
}

void epd_test_pattern_draw_crosspoint_header(
    uint8_t *buffer,
    const epd_test_pattern_header_spec_t *spec)
{
    const char *title = "";
    const char *meta = "";
    const ink_cpfont_t *title_font = NULL;
    const ink_cpfont_t *meta_font = NULL;
    epd_test_pattern_header_layout_t layout = epd_test_pattern_crosspoint_header_layout();

    if (buffer == NULL) {
        return;
    }
    if (spec != NULL) {
        title = spec->title != NULL ? spec->title : "";
        meta = spec->meta != NULL ? spec->meta : "";
        title_font = spec->title_font;
        meta_font = spec->meta_font;
    }

    draw_ui_text(buffer, title_font, layout.gutter_x, layout.title_y, title, 2, 1U, false);
    if (meta[0] != '\0') {
        draw_ui_text(
            buffer,
            meta_font,
            EPD_GDEY0426T82_WIDTH - 96,
            layout.meta_y,
            meta,
            2,
            1U,
            true);
    }
    fill_rect(
        buffer,
        layout.gutter_x,
        layout.divider_y,
        EPD_GDEY0426T82_WIDTH - layout.gutter_x * 2,
        1,
        true);
}

void epd_test_pattern_draw_crosspoint_list_row(
    uint8_t *buffer,
    const epd_test_pattern_list_layout_t *layout,
    size_t row_index,
    const epd_test_pattern_list_row_t *row,
    const ink_cpfont_t *title_font,
    const ink_cpfont_t *meta_font)
{
    epd_test_pattern_list_layout_t fallback_layout = epd_test_pattern_crosspoint_list_layout();
    const epd_test_pattern_list_layout_t *resolved_layout = layout != NULL ? layout : &fallback_layout;
    const int row_y = resolved_layout->list_y + (int)row_index * (resolved_layout->row_h + resolved_layout->row_gap);
    const bool selected = row != NULL && row->selected;
    const bool emphasized = row != NULL && row->emphasized;
    const char *title = row != NULL && row->title != NULL ? row->title : "";
    const char *line1 = row != NULL && row->line1 != NULL ? row->line1 : "";
    const char *line2 = row != NULL && row->line2 != NULL ? row->line2 : "";
    const int inner_x = resolved_layout->list_x + 12;

    if (buffer == NULL) {
        return;
    }

    if (selected) {
        fill_rect(
            buffer,
            resolved_layout->list_x,
            row_y,
            resolved_layout->row_w,
            resolved_layout->row_h,
            true);
    } else if (emphasized) {
        fill_rect(buffer, resolved_layout->list_x, row_y, resolved_layout->row_w, 1, true);
        fill_rect(
            buffer,
            resolved_layout->list_x,
            row_y + resolved_layout->row_h - 1,
            resolved_layout->row_w,
            1,
            true);
    }

    if (selected) {
        epd_draw_text_maybe_font_scaled_inverted(buffer, title_font, inner_x, row_y + 10, title, 2, 1U);
        if (line1[0] != '\0') {
            epd_draw_text_maybe_font_scaled_inverted(buffer, meta_font, inner_x, row_y + 24, line1, 1, 1U);
        }
        if (line2[0] != '\0') {
            epd_draw_text_maybe_font_scaled_inverted(buffer, meta_font, inner_x, row_y + 36, line2, 1, 1U);
        }
        return;
    }

    epd_draw_text_maybe_font_scaled(buffer, title_font, inner_x, row_y + 10, title, 2, 1U);
    if (line1[0] != '\0') {
        epd_draw_text_maybe_font_scaled(buffer, meta_font, inner_x, row_y + 24, line1, 1, 1U);
    }
    if (line2[0] != '\0') {
        epd_draw_text_maybe_font_scaled(buffer, meta_font, inner_x, row_y + 36, line2, 1, 1U);
    }
}

static char epd_normalize_glyph_char(char c)
{
    if (c >= 'a' && c <= 'z') {
        return (char)(c - 'a' + 'A');
    }
    return c;
}

static const epd_glyph_t *epd_find_glyph(char c)
{
    c = epd_normalize_glyph_char(c);

    for (size_t i = 0; i < sizeof(s_font) / sizeof(s_font[0]); ++i) {
        if (s_font[i].c == c) {
            return &s_font[i];
        }
    }
    return &s_font[0];
}

static void epd_set_pixel(uint8_t *buffer, int x, int y, bool black)
{
    if (buffer == NULL) {
        return;
    }
    if (x < 0 || x >= EPD_GDEY0426T82_WIDTH || y < 0 || y >= EPD_GDEY0426T82_HEIGHT) {
        return;
    }

    const size_t index = (size_t)y * (EPD_GDEY0426T82_WIDTH / 8) + (size_t)(x / 8);
    const uint8_t mask = (uint8_t)(0x80 >> (x % 8));

    if (black) {
        buffer[index] &= (uint8_t)~mask;
    } else {
        buffer[index] |= mask;
    }
}

static bool epd_pixel_is_black(const uint8_t *buffer, int x, int y)
{
    if (buffer == NULL) {
        return false;
    }
    if (x < 0 || x >= EPD_GDEY0426T82_WIDTH || y < 0 || y >= EPD_GDEY0426T82_HEIGHT) {
        return false;
    }

    const size_t index = (size_t)y * (EPD_GDEY0426T82_WIDTH / 8) + (size_t)(x / 8);
    const uint8_t mask = (uint8_t)(0x80 >> (x % 8));
    return (buffer[index] & mask) == 0;
}

static void epd_fill_rect(uint8_t *buffer, int x, int y, int w, int h, bool black)
{
    for (int yy = y; yy < y + h; ++yy) {
        for (int xx = x; xx < x + w; ++xx) {
            epd_set_pixel(buffer, xx, yy, black);
        }
    }
}

static void fill_rect(uint8_t *buffer, int x, int y, int w, int h, bool black)
{
    epd_fill_rect(buffer, x, y, w, h, black);
}

static void epd_draw_glyph(uint8_t *buffer, int x, int y, char c, int scale)
{
    const epd_glyph_t *glyph = epd_find_glyph(c);

    for (int row = 0; row < 7; ++row) {
        for (int col = 0; col < 5; ++col) {
            if (glyph->rows[row] & (1U << (4 - col))) {
                epd_fill_rect(buffer, x + col * scale, y + row * scale, scale, scale, true);
            }
        }
    }
}

static void epd_draw_text(uint8_t *buffer, int x, int y, const char *text, int scale)
{
    if (text == NULL) {
        return;
    }

    int cursor_x = x;
    for (const char *p = text; *p != '\0'; ++p) {
        epd_draw_glyph(buffer, cursor_x, y, *p, scale);
        cursor_x += 6 * scale;
    }
}

static void epd_draw_text_maybe_font(
    uint8_t *buffer,
    const ink_cpfont_t *font,
    int x,
    int y,
    const char *text,
    int scale)
{
    if (ink_cpfont_is_loaded(font)) {
        int width_px = 0;
        if (ink_cpfont_draw_text_bw((ink_cpfont_t *)font, buffer, x, y, text, &width_px) == ESP_OK) {
            return;
        }
    }

    epd_draw_text(buffer, x, y, text, scale);
}

static void epd_draw_text_maybe_font_scaled(
    uint8_t *buffer,
    const ink_cpfont_t *font,
    int x,
    int y,
    const char *text,
    int fallback_scale,
    uint8_t font_scale_divisor)
{
    if (ink_cpfont_is_loaded(font)) {
        int width_px = 0;
        if (ink_cpfont_draw_text_bw_scaled(
                (ink_cpfont_t *)font,
                buffer,
                x,
                y,
                text,
                font_scale_divisor,
                &width_px) == ESP_OK) {
            return;
        }
    }

    epd_draw_text(buffer, x, y, text, fallback_scale);
}

static void epd_draw_text_maybe_font_inverted(
    uint8_t *buffer,
    const ink_cpfont_t *font,
    int x,
    int y,
    const char *text,
    int scale)
{
    if (ink_cpfont_is_loaded(font)) {
        int width_px = 0;
        if (ink_cpfont_draw_text_bw_inverted((ink_cpfont_t *)font, buffer, x, y, text, &width_px) == ESP_OK) {
            return;
        }
    }

    epd_draw_text(buffer, x, y, text, scale);
}

static void epd_draw_text_maybe_font_scaled_inverted(
    uint8_t *buffer,
    const ink_cpfont_t *font,
    int x,
    int y,
    const char *text,
    int fallback_scale,
    uint8_t font_scale_divisor)
{
    if (ink_cpfont_is_loaded(font)) {
        int width_px = 0;
        if (ink_cpfont_draw_text_bw_scaled_inverted(
                (ink_cpfont_t *)font,
                buffer,
                x,
                y,
                text,
                font_scale_divisor,
                &width_px) == ESP_OK) {
            return;
        }
    }

    epd_draw_text(buffer, x, y, text, fallback_scale);
}

static void draw_ui_text(
    uint8_t *buffer,
    const ink_cpfont_t *font,
    int x,
    int y,
    const char *text,
    int fallback_scale,
    uint8_t font_scale_divisor,
    bool right_aligned)
{
    if (right_aligned) {
        epd_draw_text_right_aligned_maybe_font_scaled(
            buffer,
            font,
            x,
            y,
            text != NULL ? text : "",
            fallback_scale,
            font_scale_divisor);
        return;
    }

    epd_draw_text_maybe_font_scaled(
        buffer,
        font,
        x,
        y,
        text != NULL ? text : "",
        fallback_scale,
        font_scale_divisor);
}

static size_t epd_min_size(size_t a, size_t b)
{
    return a < b ? a : b;
}

static int epd_measure_text_width_ascii(const char *text, int scale)
{
    if (text == NULL || scale <= 0) {
        return 0;
    }

    return (int)strlen(text) * 6 * scale;
}

static int epd_measure_text_width_maybe_font_scaled(
    const ink_cpfont_t *font,
    const char *text,
    int fallback_scale,
    uint8_t font_scale_divisor)
{
    int width_px = 0;

    if (text == NULL || text[0] == '\0') {
        return 0;
    }

    if (ink_cpfont_is_loaded(font)) {
        if (ink_cpfont_draw_text_bw_scaled(
                (ink_cpfont_t *)font,
                NULL,
                0,
                0,
                text,
                font_scale_divisor,
                &width_px) == ESP_OK) {
            return width_px;
        }
    }

    return epd_measure_text_width_ascii(text, fallback_scale);
}

static size_t epd_utf8_copy_prefix(char *dst, size_t dst_size, const char *src, size_t byte_count)
{
    size_t copied = 0U;

    if (dst == NULL || dst_size == 0U) {
        return 0U;
    }
    dst[0] = '\0';
    if (src == NULL || byte_count == 0U) {
        return 0U;
    }

    while (copied < byte_count && copied + 1U < dst_size && src[copied] != '\0') {
        dst[copied] = src[copied];
        ++copied;
    }
    dst[copied] = '\0';
    return copied;
}

static void epd_copy_text_with_ellipsis(
    char *dst,
    size_t dst_size,
    const ink_cpfont_t *font,
    const char *src,
    int max_width_px,
    int fallback_scale,
    uint8_t font_scale_divisor)
{
    static const char *kEllipsis = "...";
    const int ellipsis_width = epd_measure_text_width_maybe_font_scaled(
        font,
        kEllipsis,
        fallback_scale,
        font_scale_divisor);
    size_t last_good_bytes = 0U;
    const char *cursor = src;

    if (dst == NULL || dst_size == 0U) {
        return;
    }
    dst[0] = '\0';
    if (src == NULL || src[0] == '\0') {
        return;
    }

    if (epd_measure_text_width_maybe_font_scaled(font, src, fallback_scale, font_scale_divisor) <= max_width_px) {
        snprintf(dst, dst_size, "%s", src);
        return;
    }

    while (*cursor != '\0') {
        const char *before = cursor;
        char candidate[96];
        const size_t cp_len = epd_utf8_codepoint_length((unsigned char)*cursor);
        size_t actual_len = 0U;
        while (actual_len < cp_len && cursor[actual_len] != '\0') {
            ++actual_len;
        }
        cursor += actual_len;
        if (before == cursor) {
            break;
        }

        last_good_bytes = (size_t)(cursor - src);
        epd_utf8_copy_prefix(candidate, sizeof(candidate), src, last_good_bytes);
        if (epd_measure_text_width_maybe_font_scaled(font, candidate, fallback_scale, font_scale_divisor) + ellipsis_width > max_width_px) {
            last_good_bytes = (size_t)(before - src);
            break;
        }
    }

    epd_utf8_copy_prefix(dst, dst_size, src, last_good_bytes);
    if (dst[0] != '\0' && strlen(dst) + 3U < dst_size) {
        strcat(dst, kEllipsis);
    }
}

static void epd_draw_text_right_aligned_maybe_font_scaled(
    uint8_t *buffer,
    const ink_cpfont_t *font,
    int right_edge_x,
    int y,
    const char *text,
    int fallback_scale,
    uint8_t font_scale_divisor)
{
    const int width_px = epd_measure_text_width_maybe_font_scaled(
        font,
        text,
        fallback_scale,
        font_scale_divisor);
    int draw_x = right_edge_x - width_px;

    if (draw_x < FOOTER_LEFT_X) {
        draw_x = FOOTER_LEFT_X;
    }

    epd_draw_text_maybe_font_scaled(
        buffer,
        font,
        draw_x,
        y,
        text,
        fallback_scale,
        font_scale_divisor);
}

static void epd_draw_text_clipped_maybe_font_scaled(
    uint8_t *buffer,
    const ink_cpfont_t *font,
    int x,
    int y,
    const char *text,
    int max_width_px,
    int fallback_scale,
    uint8_t font_scale_divisor,
    bool inverted)
{
    char clipped[96];

    epd_copy_text_with_ellipsis(
        clipped,
        sizeof(clipped),
        font,
        text,
        max_width_px,
        fallback_scale,
        font_scale_divisor);
    if (inverted) {
        epd_draw_text_maybe_font_scaled_inverted(
            buffer,
            font,
            x,
            y,
            clipped,
            fallback_scale,
            font_scale_divisor);
    } else {
        epd_draw_text_maybe_font_scaled(
            buffer,
            font,
            x,
            y,
            clipped,
            fallback_scale,
            font_scale_divisor);
    }
}

static void epd_draw_text_left_clipped_maybe_font_scaled(
    uint8_t *buffer,
    const ink_cpfont_t *font,
    int x,
    int y,
    const char *text,
    int fallback_scale,
    uint8_t font_scale_divisor,
    int right_reserved_width_px)
{
    int max_width_px = EPD_GDEY0426T82_WIDTH - FOOTER_RIGHT_MARGIN - right_reserved_width_px - x;

    if (max_width_px < 24) {
        max_width_px = 24;
    }

    epd_draw_text_clipped_maybe_font_scaled(
        buffer,
        font,
        x,
        y,
        text,
        max_width_px,
        fallback_scale,
        font_scale_divisor,
        false);
}

static uint8_t epd_footer_font_scale_divisor(const ink_cpfont_t *font)
{
    if (!ink_cpfont_is_loaded(font)) {
        return 1U;
    }
    return font->advance_y > 24U ? 2U : 1U;
}

static bool epd_should_abort_draw(
    epd_test_pattern_should_abort_fn should_abort,
    void *should_abort_ctx)
{
    return should_abort != NULL && should_abort(should_abort_ctx);
}

static size_t epd_utf8_codepoint_length(unsigned char lead)
{
    if (lead < 0x80U) {
        return 1U;
    }
    if ((lead & 0xE0U) == 0xC0U) {
        return 2U;
    }
    if ((lead & 0xF0U) == 0xE0U) {
        return 3U;
    }
    if ((lead & 0xF8U) == 0xF0U) {
        return 4U;
    }
    return 1U;
}

static size_t epd_utf8_display_units(unsigned char lead)
{
    return lead < 0x80U ? 1U : 2U;
}

static void epd_reader_split_line(
    const char *source,
    char out[2][64])
{
    size_t subline = 0;
    size_t column = 0;
    size_t display_units = 0;
    const unsigned char *cursor = (const unsigned char *)source;

    out[0][0] = '\0';
    out[1][0] = '\0';
    if (source == NULL) {
        return;
    }

    while (*cursor != '\0' && subline < 2U) {
        const size_t cp_len = epd_utf8_codepoint_length(*cursor);
        const size_t cp_units = epd_utf8_display_units(*cursor);
        size_t actual_len = 0;

        while (actual_len < cp_len && cursor[actual_len] != '\0') {
            ++actual_len;
        }

        if (actual_len == 0U) {
            break;
        }

        if (display_units > 0U && display_units + cp_units > READER_SUBLINE_UNITS) {
            out[subline][column] = '\0';
            ++subline;
            column = 0;
            display_units = 0;
            if (subline >= 2U) {
                break;
            }
        }

        if (column + actual_len >= sizeof(out[0])) {
            out[subline][column] = '\0';
            ++subline;
            column = 0;
            display_units = 0;
            if (subline >= 2U) {
                break;
            }
        }

        memcpy(out[subline] + column, cursor, actual_len);
        column += actual_len;
        out[subline][column] = '\0';
        display_units += cp_units;
        cursor += actual_len;
    }
}

static void epd_draw_rect_outline(uint8_t *buffer, int x, int y, int w, int h, int thickness)
{
    epd_fill_rect(buffer, x, y, w, thickness, true);
    epd_fill_rect(buffer, x, y + h - thickness, w, thickness, true);
    epd_fill_rect(buffer, x, y, thickness, h, true);
    epd_fill_rect(buffer, x + w - thickness, y, thickness, h, true);
}

static void epd_draw_heart_icon(uint8_t *buffer, int x, int y, bool inverted)
{
    static const uint16_t kRows[] = {
        0b0011110011110000,
        0b0111111011111000,
        0b1111111111111100,
        0b1111111111111100,
        0b1111111111111100,
        0b0111111111111000,
        0b0011111111110000,
        0b0001111111100000,
        0b0000111111000000,
        0b0000011110000000,
        0b0000001100000000,
        0b0000000000000000,
    };

    for (int row = 0; row < 12; ++row) {
        for (int col = 0; col < 16; ++col) {
            if ((kRows[row] & (uint16_t)(0x8000U >> col)) == 0U) {
                continue;
            }
            epd_set_pixel(buffer, x + col, y + row, !inverted);
        }
    }
}

static void epd_gray_mark_region(uint8_t *buffer, int x, int y, int w, int h)
{
    epd_fill_rect(buffer, x, y, w, h, true);
}

static void epd_draw_loader_boxes(uint8_t *buffer, uint8_t loader_step)
{
    epd_draw_text(buffer, 92, EPD_TEST_PATTERN_GRAY_LOADER_Y - 8, "PARTIAL LOADER", 2);

    for (int box = 0; box < 3; ++box) {
        const int box_x = GRAY_LOADER_BOX_X0 + box * (GRAY_LOADER_BOX_W + GRAY_LOADER_BOX_GAP);
        const bool active = box == loader_step;

        epd_draw_rect_outline(buffer, box_x, GRAY_LOADER_BOX_Y, GRAY_LOADER_BOX_W, GRAY_LOADER_BOX_H, 3);
        if (active) {
            epd_fill_rect(buffer, box_x + 6, GRAY_LOADER_BOX_Y + 6, GRAY_LOADER_BOX_W - 12, GRAY_LOADER_BOX_H - 12, true);
        }
    }
}

static void epd_fill_gray_demo_common(uint8_t *buffer)
{
    epd_draw_rect_outline(buffer, 0, 0, EPD_GDEY0426T82_WIDTH, EPD_GDEY0426T82_HEIGHT, 8);
    epd_draw_text(buffer, 34, 28, "4 LEVEL DIAG", 3);
    epd_draw_text(buffer, 42, 76, "X4 SSD1677 PORTRAIT", 2);

    for (int band = 0; band < 4; ++band) {
        const int top = GRAY_BAR_Y0 + band * (GRAY_BAR_HEIGHT + GRAY_BAR_GAP);
        epd_draw_rect_outline(buffer, GRAY_BAR_X, top, GRAY_BAR_WIDTH, GRAY_BAR_HEIGHT, 4);
    }

    epd_draw_text(buffer, 60, GRAY_LABEL_Y, "WHITE", 2);
    epd_draw_text(buffer, 162, GRAY_LABEL_Y, "LIGHT", 2);
    epd_draw_text(buffer, 270, GRAY_LABEL_Y, "DARK", 2);
    epd_draw_text(buffer, 364, GRAY_LABEL_Y, "BLACK", 2);

    epd_fill_rect(buffer, 40, 610, EPD_GDEY0426T82_WIDTH - 80, 3, true);
}

void epd_test_pattern_fill_stripes(uint8_t *buffer, size_t length)
{
    if (buffer == NULL) {
        return;
    }

    for (size_t i = 0; i < length; ++i) {
        buffer[i] = (i % 2 == 0) ? 0xAA : 0x55;
    }
}

void epd_test_pattern_fill_layout(uint8_t *buffer, size_t length)
{
    if (buffer == NULL || length < EPD_GDEY0426T82_BUFFER_SIZE) {
        return;
    }

    memset(buffer, 0xFF, EPD_GDEY0426T82_BUFFER_SIZE);

    epd_fill_rect(buffer, 0, 0, EPD_GDEY0426T82_WIDTH, 8, true);
    epd_fill_rect(buffer, 0, EPD_GDEY0426T82_HEIGHT - 8, EPD_GDEY0426T82_WIDTH, 8, true);
    epd_fill_rect(buffer, 0, 0, 8, EPD_GDEY0426T82_HEIGHT, true);
    epd_fill_rect(buffer, EPD_GDEY0426T82_WIDTH - 8, 0, 8, EPD_GDEY0426T82_HEIGHT, true);

    epd_fill_rect(buffer, 16, 16, 64, 64, true);
    epd_fill_rect(buffer, EPD_GDEY0426T82_WIDTH - 80, 16, 64, 64, true);
    epd_fill_rect(buffer, 16, EPD_GDEY0426T82_HEIGHT - 80, 64, 64, true);
    epd_fill_rect(buffer, EPD_GDEY0426T82_WIDTH - 80, EPD_GDEY0426T82_HEIGHT - 80, 64, 64, true);

    epd_fill_rect(buffer, EPD_GDEY0426T82_WIDTH / 2 - 4, 40, 8, EPD_GDEY0426T82_HEIGHT - 80, true);
    epd_fill_rect(buffer, 40, EPD_GDEY0426T82_HEIGHT / 2 - 4, EPD_GDEY0426T82_WIDTH - 80, 8, true);

    for (int x = 120; x < EPD_GDEY0426T82_WIDTH - 120; x += 40) {
        epd_fill_rect(buffer, x, 96, 20, 40, true);
    }
    for (int y = 140; y < EPD_GDEY0426T82_HEIGHT - 140; y += 32) {
        epd_fill_rect(buffer, 120, y, 48, 16, true);
    }
}

void epd_test_pattern_fill_text_demo(uint8_t *buffer, size_t length)
{
    if (buffer == NULL || length < EPD_GDEY0426T82_BUFFER_SIZE) {
        return;
    }

    memset(buffer, 0xFF, EPD_GDEY0426T82_BUFFER_SIZE);

    epd_fill_rect(buffer, 0, 0, EPD_GDEY0426T82_WIDTH, 8, true);
    epd_fill_rect(buffer, 0, EPD_GDEY0426T82_HEIGHT - 8, EPD_GDEY0426T82_WIDTH, 8, true);
    epd_fill_rect(buffer, 0, 0, 8, EPD_GDEY0426T82_HEIGHT, true);
    epd_fill_rect(buffer, EPD_GDEY0426T82_WIDTH - 8, 0, 8, EPD_GDEY0426T82_HEIGHT, true);

    epd_draw_text(buffer, 36, 70, "ESP32-S3", 5);
    epd_draw_text(buffer, 36, 160, "GDEQ0426T82", 4);
    epd_draw_text(buffer, 36, 280, "HELLO", 7);
    epd_draw_text(buffer, 36, 430, "1234567890", 4);
}

void epd_test_pattern_fill_gray_demo_bw(uint8_t *buffer, size_t length, uint8_t loader_step)
{
    if (buffer == NULL || length < EPD_GDEY0426T82_BUFFER_SIZE) {
        return;
    }

    memset(buffer, 0xFF, EPD_GDEY0426T82_BUFFER_SIZE);
    epd_fill_gray_demo_common(buffer);

    epd_fill_rect(buffer, GRAY_BAR_X + 5, GRAY_BAR_Y0 + (GRAY_BAR_HEIGHT + GRAY_BAR_GAP) + 5, GRAY_BAR_WIDTH - 10, GRAY_BAR_HEIGHT - 10, true);
    epd_fill_rect(buffer, GRAY_BAR_X + 5, GRAY_BAR_Y0 + 2 * (GRAY_BAR_HEIGHT + GRAY_BAR_GAP) + 5, GRAY_BAR_WIDTH - 10, GRAY_BAR_HEIGHT - 10, true);
    epd_fill_rect(buffer, GRAY_BAR_X + 5, GRAY_BAR_Y0 + 3 * (GRAY_BAR_HEIGHT + GRAY_BAR_GAP) + 5, GRAY_BAR_WIDTH - 10, GRAY_BAR_HEIGHT - 10, true);

    epd_draw_loader_boxes(buffer, (uint8_t)(loader_step % 3));
}

void epd_test_pattern_fill_gray_demo_planes(
    uint8_t *lsb_buffer,
    size_t lsb_length,
    uint8_t *msb_buffer,
    size_t msb_length)
{
    if (lsb_buffer == NULL || msb_buffer == NULL) {
        return;
    }
    if (lsb_length < EPD_GDEY0426T82_GRAY_PLANE_SIZE || msb_length < EPD_GDEY0426T82_GRAY_PLANE_SIZE) {
        return;
    }

    memset(lsb_buffer, 0xFF, EPD_GDEY0426T82_GRAY_PLANE_SIZE);
    memset(msb_buffer, 0xFF, EPD_GDEY0426T82_GRAY_PLANE_SIZE);

    const int light_top = GRAY_BAR_Y0 + (GRAY_BAR_HEIGHT + GRAY_BAR_GAP) + 5;
    const int dark_top = GRAY_BAR_Y0 + 2 * (GRAY_BAR_HEIGHT + GRAY_BAR_GAP) + 5;
    const int black_top = GRAY_BAR_Y0 + 3 * (GRAY_BAR_HEIGHT + GRAY_BAR_GAP) + 5;

    epd_gray_mark_region(msb_buffer, GRAY_BAR_X + 5, light_top, GRAY_BAR_WIDTH - 10, GRAY_BAR_HEIGHT - 10);
    epd_gray_mark_region(lsb_buffer, GRAY_BAR_X + 5, dark_top, GRAY_BAR_WIDTH - 10, GRAY_BAR_HEIGHT - 10);
    epd_gray_mark_region(lsb_buffer, GRAY_BAR_X + 5, black_top, GRAY_BAR_WIDTH - 10, GRAY_BAR_HEIGHT - 10);
    epd_gray_mark_region(msb_buffer, GRAY_BAR_X + 5, black_top, GRAY_BAR_WIDTH - 10, GRAY_BAR_HEIGHT - 10);
}

void epd_test_pattern_fill_gray_calibration_page(uint8_t *buffer, size_t length)
{
    const int panel_x = 20;
    const int panel_y = 72;
    const int panel_w = EPD_GDEY0426T82_WIDTH - 40;
    const int panel_h = EPD_GDEY0426T82_HEIGHT - 150;
    const int cols = 2;
    const int rows = 4;
    const int gap_x = 18;
    const int gap_y = 14;
    const int cell_w = (panel_w - 40 - gap_x) / cols;
    const int cell_h = (panel_h - 60 - gap_y * (rows - 1)) / rows;
    static const char *const labels[8] = {
        "RAW 00", "INV 11",
        "RAW 01", "INV 10",
        "RAW 10", "INV 01",
        "RAW 11", "INV 00",
    };

    if (buffer == NULL || length < EPD_GDEY0426T82_BUFFER_SIZE) {
        return;
    }

    memset(buffer, 0xFF, EPD_GDEY0426T82_BUFFER_SIZE);
    epd_draw_rect_outline(buffer, 0, 0, EPD_GDEY0426T82_WIDTH, EPD_GDEY0426T82_HEIGHT, 8);
    epd_draw_text(buffer, 26, 24, "GRAY CAL", 3);
    epd_draw_text(buffer, 28, 58, "SSD1677 RAW / INVERT", 2);
    epd_draw_rect_outline(buffer, panel_x, panel_y, panel_w, panel_h, 3);

    for (int index = 0; index < 8; ++index) {
        const int row = index / cols;
        const int col = index % cols;
        const int x = panel_x + 18 + col * (cell_w + gap_x);
        const int y = panel_y + 18 + row * (cell_h + gap_y);

        epd_draw_rect_outline(buffer, x, y, cell_w, cell_h, 2);
        epd_draw_text(buffer, x + 10, y + 10, labels[index], 2);
        epd_draw_text(buffer, x + 10, y + cell_h - 30, "OBSERVE SHADE", 2);
    }
}

bool epd_test_pattern_gray_demo_self_test(void)
{
    const size_t plane_size = EPD_GDEY0426T82_GRAY_PLANE_SIZE;
    uint8_t *bw = malloc(plane_size);
    uint8_t *lsb = malloc(plane_size);
    uint8_t *msb = malloc(plane_size);
    bool ok = false;

    if (bw == NULL || lsb == NULL || msb == NULL) {
        goto cleanup;
    }

    epd_test_pattern_fill_gray_demo_bw(bw, plane_size, 0);
    epd_test_pattern_fill_gray_demo_planes(lsb, plane_size, msb, plane_size);

    const int bar_center_x = EPD_GDEY0426T82_WIDTH / 2;
    const int white_y = GRAY_BAR_Y0 + GRAY_BAR_HEIGHT / 2;
    const int light_y = GRAY_BAR_Y0 + (GRAY_BAR_HEIGHT + GRAY_BAR_GAP) + GRAY_BAR_HEIGHT / 2;
    const int dark_y = GRAY_BAR_Y0 + 2 * (GRAY_BAR_HEIGHT + GRAY_BAR_GAP) + GRAY_BAR_HEIGHT / 2;
    const int black_y = GRAY_BAR_Y0 + 3 * (GRAY_BAR_HEIGHT + GRAY_BAR_GAP) + GRAY_BAR_HEIGHT / 2;

    if (epd_pixel_is_black(bw, bar_center_x, white_y)) {
        goto cleanup;
    }
    if (!epd_pixel_is_black(bw, bar_center_x, light_y)) {
        goto cleanup;
    }
    if (!epd_pixel_is_black(bw, bar_center_x, dark_y)) {
        goto cleanup;
    }
    if (!epd_pixel_is_black(bw, bar_center_x, black_y)) {
        goto cleanup;
    }

    if (epd_pixel_is_black(lsb, bar_center_x, light_y)) {
        goto cleanup;
    }
    if (!epd_pixel_is_black(msb, bar_center_x, light_y)) {
        goto cleanup;
    }
    if (!epd_pixel_is_black(lsb, bar_center_x, dark_y)) {
        goto cleanup;
    }
    if (epd_pixel_is_black(msb, bar_center_x, dark_y)) {
        goto cleanup;
    }
    if (!epd_pixel_is_black(lsb, bar_center_x, black_y) || !epd_pixel_is_black(msb, bar_center_x, black_y)) {
        goto cleanup;
    }

    for (uint8_t step = 0; step < 3; ++step) {
        epd_test_pattern_fill_gray_demo_bw(bw, plane_size, step);
        for (uint8_t box = 0; box < 3; ++box) {
            const int sample_x = GRAY_LOADER_BOX_X0 + box * (GRAY_LOADER_BOX_W + GRAY_LOADER_BOX_GAP) + GRAY_LOADER_BOX_W / 2;
            const int sample_y = GRAY_LOADER_BOX_Y + GRAY_LOADER_BOX_H / 2;
            const bool is_black = epd_pixel_is_black(bw, sample_x, sample_y);
            if ((box == step) != is_black) {
                goto cleanup;
            }
        }
    }

    ok = true;

cleanup:
    free(bw);
    free(lsb);
    free(msb);
    return ok;
}

bool epd_test_pattern_reader_page_self_test(void)
{
    uint8_t *buffer = malloc(EPD_GDEY0426T82_BUFFER_SIZE);
    bool ok = false;
    static const char *kDualLine =
        "AAAAAAAAAAAAAAAAAAAAA";

    if (buffer == NULL) {
        return false;
    }

    epd_test_pattern_fill_reader_page_with_font(
        buffer,
        EPD_GDEY0426T82_BUFFER_SIZE,
        NULL,
        NULL,
        NULL,
        "READER",
        kDualLine,
        kDualLine,
        kDualLine,
        kDualLine,
        "AAAA");

    for (size_t i = 0; i < READER_SUBLINE_COUNT; ++i) {
        if (!epd_pixel_is_black(buffer, 35, s_reader_line_y[i] + 1)) {
            goto cleanup;
        }
    }
    if (!epd_pixel_is_black(buffer, 35, 691)) {
        goto cleanup;
    }

    ok = true;

cleanup:
    free(buffer);
    return ok;
}

bool epd_test_pattern_copy_page_buffer(
    uint8_t *buffer,
    size_t length,
    const uint8_t *page_buffer,
    size_t page_buffer_length)
{
    if (buffer == NULL
        || page_buffer == NULL
        || length < EPD_GDEY0426T82_BUFFER_SIZE
        || page_buffer_length < EPD_GDEY0426T82_BUFFER_SIZE) {
        return false;
    }

    memcpy(buffer, page_buffer, EPD_GDEY0426T82_BUFFER_SIZE);
    return true;
}

bool epd_test_pattern_copy_page_buffer_self_test(void)
{
    const size_t page_size = EPD_GDEY0426T82_BUFFER_SIZE;
    uint8_t *source = malloc(EPD_GDEY0426T82_BUFFER_SIZE);
    uint8_t *target = malloc(EPD_GDEY0426T82_BUFFER_SIZE);

    if (source == NULL || target == NULL) {
        free(source);
        free(target);
        return false;
    }
    memset(source, 0xA5, page_size);
    memset(target, 0x5A, page_size);

    if (epd_test_pattern_copy_page_buffer(NULL, page_size, source, page_size)) {
        free(source);
        free(target);
        return false;
    }
    if (epd_test_pattern_copy_page_buffer(target, page_size, NULL, page_size)) {
        free(source);
        free(target);
        return false;
    }
    if (epd_test_pattern_copy_page_buffer(target, page_size - 1U, source, page_size)) {
        free(source);
        free(target);
        return false;
    }
    if (epd_test_pattern_copy_page_buffer(target, page_size, source, page_size - 1U)) {
        free(source);
        free(target);
        return false;
    }
    if (!epd_test_pattern_copy_page_buffer(target, page_size, source, page_size)) {
        free(source);
        free(target);
        return false;
    }
    if (memcmp(target, source, page_size) != 0) {
        free(source);
        free(target);
        return false;
    }

    free(source);
    free(target);
    return true;
}

bool epd_test_pattern_footer_overlay_self_test(void)
{
    uint8_t *buffer = malloc(EPD_GDEY0426T82_BUFFER_SIZE);
    bool left_ok = false;
    bool right_ok = false;
    bool middle_gap_ok = true;

    if (buffer == NULL) {
        return false;
    }

    memset(buffer, 0xFF, EPD_GDEY0426T82_BUFFER_SIZE);
    epd_test_pattern_draw_footer_overlay(
        buffer,
        EPD_GDEY0426T82_BUFFER_SIZE,
        NULL,
        "CHAPTER",
        "12% 12/100");

    for (int x = FOOTER_LEFT_X; x < FOOTER_LEFT_X + 96; ++x) {
        for (int y = FOOTER_TEXT_Y; y < FOOTER_TEXT_Y + 14; ++y) {
            if (epd_pixel_is_black(buffer, x, y)) {
                left_ok = true;
                break;
            }
        }
        if (left_ok) {
            break;
        }
    }
    for (int x = EPD_GDEY0426T82_WIDTH - 56; x < EPD_GDEY0426T82_WIDTH - FOOTER_RIGHT_MARGIN; ++x) {
        for (int y = FOOTER_TEXT_Y; y < FOOTER_TEXT_Y + 14; ++y) {
            if (epd_pixel_is_black(buffer, x, y)) {
                right_ok = true;
                break;
            }
        }
        if (right_ok) {
            break;
        }
    }
    for (int x = EPD_GDEY0426T82_WIDTH - FOOTER_RIGHT_RESERVED_W;
         x < EPD_GDEY0426T82_WIDTH - FOOTER_RIGHT_RESERVED_W + 24;
         ++x) {
        for (int y = FOOTER_TEXT_Y; y < FOOTER_TEXT_Y + 14; ++y) {
            if (epd_pixel_is_black(buffer, x, y)) {
                middle_gap_ok = false;
                break;
            }
        }
        if (!middle_gap_ok) {
            break;
        }
    }

    free(buffer);
    return left_ok && right_ok && middle_gap_ok;
}

void epd_test_pattern_fill_text_page(
    uint8_t *buffer,
    size_t length,
    const char *title,
    const char *line1,
    const char *line2,
    const char *line3,
    const char *line4,
    const char *line5)
{
    epd_test_pattern_fill_text_page_with_font(
        buffer,
        length,
        NULL,
        NULL,
        NULL,
        title,
        line1,
        line2,
        line3,
        line4,
        line5);
}

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
    const char *line5)
{
    static const char *kFallbackTitle = "TXT PREVIEW";
    static const char *kFallbackLine = "#";

    if (buffer == NULL || length < EPD_GDEY0426T82_BUFFER_SIZE) {
        return;
    }

    memset(buffer, 0xFF, EPD_GDEY0426T82_BUFFER_SIZE);

    epd_fill_rect(buffer, 0, 0, EPD_GDEY0426T82_WIDTH, 8, true);
    epd_fill_rect(buffer, 0, EPD_GDEY0426T82_HEIGHT - 8, EPD_GDEY0426T82_WIDTH, 8, true);
    epd_fill_rect(buffer, 0, 0, 8, EPD_GDEY0426T82_HEIGHT, true);
    epd_fill_rect(buffer, EPD_GDEY0426T82_WIDTH - 8, 0, 8, EPD_GDEY0426T82_HEIGHT, true);

    epd_fill_rect(buffer, 24, 78, EPD_GDEY0426T82_WIDTH - 48, 4, true);
    epd_fill_rect(buffer, 24, 730, EPD_GDEY0426T82_WIDTH - 48, 4, true);

    epd_draw_text_maybe_font(buffer, font, 28, 30, title != NULL ? title : kFallbackTitle, 3);
    if (epd_should_abort_draw(should_abort, should_abort_ctx)) {
        return;
    }
    epd_draw_text_maybe_font(buffer, font, 32, 115, line1 != NULL ? line1 : kFallbackLine, 3);
    if (epd_should_abort_draw(should_abort, should_abort_ctx)) {
        return;
    }
    epd_draw_text_maybe_font(buffer, font, 32, 185, line2 != NULL ? line2 : kFallbackLine, 3);
    if (epd_should_abort_draw(should_abort, should_abort_ctx)) {
        return;
    }
    epd_draw_text_maybe_font(buffer, font, 32, 255, line3 != NULL ? line3 : kFallbackLine, 3);
    if (epd_should_abort_draw(should_abort, should_abort_ctx)) {
        return;
    }
    epd_draw_text_maybe_font(buffer, font, 32, 325, line4 != NULL ? line4 : kFallbackLine, 3);
    if (epd_should_abort_draw(should_abort, should_abort_ctx)) {
        return;
    }
    epd_draw_text_maybe_font(buffer, font, 32, 690, line5 != NULL ? line5 : kFallbackLine, 3);
}

void epd_test_pattern_draw_footer_overlay(
    uint8_t *buffer,
    size_t length,
    const ink_cpfont_t *font,
    const char *left_text,
    const char *right_text)
{
    if (buffer == NULL || length < EPD_GDEY0426T82_BUFFER_SIZE) {
        return;
    }

    epd_fill_rect(buffer, 0, FOOTER_BAND_Y, EPD_GDEY0426T82_WIDTH, FOOTER_BAND_H, false);
    epd_fill_rect(buffer, 0, FOOTER_BAND_Y - 1, EPD_GDEY0426T82_WIDTH, 1, true);
    epd_draw_text_left_clipped_maybe_font_scaled(
        buffer,
        font,
        FOOTER_LEFT_X,
        FOOTER_TEXT_Y,
        left_text != NULL ? left_text : "",
        FOOTER_BITMAP_SCALE,
        epd_footer_font_scale_divisor(font),
        FOOTER_RIGHT_RESERVED_W);
    epd_draw_text_right_aligned_maybe_font_scaled(
        buffer,
        font,
        EPD_GDEY0426T82_WIDTH - FOOTER_RIGHT_MARGIN,
        FOOTER_TEXT_Y,
        right_text != NULL ? right_text : "",
        FOOTER_BITMAP_SCALE,
        epd_footer_font_scale_divisor(font));
}

static void epd_grid_compare_cell_rect(
    uint8_t cell_index,
    int *x,
    int *y,
    int *w,
    int *h)
{
    const int col = (int)(cell_index % GRID_COMPARE_COLS);
    const int row = (int)(cell_index / GRID_COMPARE_COLS);

    if (x != NULL) {
        *x = GRID_COMPARE_X0 + col * (GRID_COMPARE_W + GRID_COMPARE_GAP_X);
    }
    if (y != NULL) {
        *y = GRID_COMPARE_Y0 + row * (GRID_COMPARE_H + GRID_COMPARE_GAP_Y);
    }
    if (w != NULL) {
        *w = GRID_COMPARE_W;
    }
    if (h != NULL) {
        *h = GRID_COMPARE_H;
    }
}

static void epd_draw_grid_compare_cell_frame(
    uint8_t *buffer,
    uint8_t cell_index,
    const char *sweep_tag)
{
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    char label[8];
    char row_text[16];
    const char *tag = (sweep_tag != NULL && sweep_tag[0] != '\0') ? sweep_tag : "G0";

    epd_grid_compare_cell_rect(cell_index, &x, &y, &w, &h);
    epd_fill_rect(buffer, x, y, w, 2, true);
    epd_fill_rect(buffer, x, y + h - 2, w, 2, true);
    epd_fill_rect(buffer, x, y, 2, h, true);
    epd_fill_rect(buffer, x + w - 2, y, 2, h, true);

    snprintf(label, sizeof(label), "%s%02u", tag, (unsigned)cell_index);
    snprintf(row_text, sizeof(row_text), "CELL-%u", (unsigned)(cell_index + 1U));
    epd_draw_text(buffer, x + GRID_COMPARE_LABEL_X_PAD, y + GRID_COMPARE_LABEL_Y_PAD, label, GRID_COMPARE_LABEL_SCALE);
    epd_draw_text(buffer, x + GRID_COMPARE_TEXT_X_PAD, y + GRID_COMPARE_TEXT_Y_PAD, "OLD PAGE", 2);
    epd_draw_text(buffer, x + GRID_COMPARE_TEXT_X_PAD, y + GRID_COMPARE_TEXT_Y_PAD + GRID_COMPARE_TEXT_LINE_H, "WHITE PRIME", 2);
    epd_draw_text(buffer, x + GRID_COMPARE_TEXT_X_PAD, y + GRID_COMPARE_TEXT_Y_PAD + GRID_COMPARE_TEXT_LINE_H * 2, row_text, 2);
    epd_draw_text(buffer, x + GRID_COMPARE_TEXT_X_PAD, y + GRID_COMPARE_TEXT_Y_PAD + GRID_COMPARE_TEXT_LINE_H * 3, "TAG P1", 2);
}

static void epd_draw_grid_compare_cell_variant(
    uint8_t *buffer,
    uint8_t cell_index,
    const char *sweep_tag)
{
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    char label[8];
    const char *tag = (sweep_tag != NULL && sweep_tag[0] != '\0') ? sweep_tag : "G0";

    epd_grid_compare_cell_rect(cell_index, &x, &y, &w, &h);
    epd_fill_rect(buffer, x, y, w, h, true);
    snprintf(label, sizeof(label), "%s%02u", tag, (unsigned)cell_index);
    epd_draw_text(buffer, x + 10, y + 10, label, 2);
}

void epd_test_pattern_draw_footer_probe(
    uint8_t *buffer,
    size_t length,
    const char *seed_text)
{
    const uint32_t hash = epd_hash_text(seed_text);

    if (buffer == NULL || length < EPD_GDEY0426T82_BUFFER_SIZE) {
        return;
    }

    epd_fill_rect(buffer, FOOTER_PROBE_X, FOOTER_PROBE_Y, FOOTER_PROBE_W, FOOTER_PROBE_H, false);
    epd_draw_rect_outline(buffer, FOOTER_PROBE_X, FOOTER_PROBE_Y, FOOTER_PROBE_W, FOOTER_PROBE_H, 1);

    for (int slot = 0; slot < 4; ++slot) {
        const int slot_x = FOOTER_PROBE_X + 3 + slot * 8;
        if (((hash >> slot) & 0x1U) != 0U) {
            epd_fill_rect(buffer, slot_x, FOOTER_PROBE_Y + 3, 5, 8, true);
        }
    }
}

void epd_test_pattern_draw_reader_menu_overlay(
    uint8_t *buffer,
    size_t length,
    const ink_cpfont_t *menu_font,
    const ink_cpfont_t *footer_font,
    const epd_test_pattern_reader_menu_overlay_t *overlay)
{
    const bool frameless_panel = overlay != NULL && overlay->frameless_panel;
    const int panel_x = frameless_panel ? 8 : 24;
    const int panel_y = frameless_panel ? 8 : 118;
    const int panel_w = frameless_panel ? (EPD_GDEY0426T82_WIDTH - 16) : (EPD_GDEY0426T82_WIDTH - 48);
    const int panel_h = frameless_panel ? (EPD_GDEY0426T82_HEIGHT - 24) : 534;
    const int tab_y = panel_y + (frameless_panel ? 8 : 18);
    const int tab_h = 42;
    const int tab_gap = frameless_panel ? 8 : 10;
    const int tab_count = overlay != NULL && overlay->tab_count > 0U ? (int)overlay->tab_count : 1;
    const int tab_side_pad = frameless_panel ? 4 : 24;
    const int tab_w = (panel_w - tab_side_pad * 2 - tab_gap * (tab_count - 1)) / tab_count;
    const int tab_x0 = panel_x + tab_side_pad;
    const int cards_y0 = frameless_panel ? (panel_y + 62) : (panel_y + 82);
    const bool bookmark_cards_tall = overlay != NULL && overlay->bookmark_cards_tall;
    const int card_h = overlay != NULL && overlay->compact_cards
        ? (frameless_panel ? 44 : 48)
        : (bookmark_cards_tall ? 58 : (frameless_panel ? 78 : 82));
    const int card_gap = overlay != NULL && overlay->compact_cards
        ? (frameless_panel ? 6 : 8)
        : (bookmark_cards_tall ? 8 : (frameless_panel ? 8 : 10));
    const int card_x = panel_x + (frameless_panel ? 4 : 24);
    const int card_w = panel_w - (frameless_panel ? 8 : 48);
    const int popup_w = panel_w - 92;
    const int popup_h =
        frameless_panel && overlay->action_popup_open && overlay->action_count <= 2U ? 164 : 208;
    const int popup_x = panel_x + (panel_w - popup_w) / 2;
    const int popup_y = panel_y + (panel_h - popup_h) / 2;
    const int popup_action_h = 34;
    const int popup_action_gap =
        frameless_panel && overlay->action_popup_open && overlay->action_count <= 2U ? 10 : 12;

    if (buffer == NULL || length < EPD_GDEY0426T82_BUFFER_SIZE || overlay == NULL) {
        return;
    }

    if (!frameless_panel) {
        epd_fill_rect(buffer, panel_x, panel_y, panel_w, panel_h, false);
        epd_draw_rect_outline(buffer, panel_x, panel_y, panel_w, panel_h, 2);
    }

    for (size_t i = 0; i < overlay->tab_count && i < EPD_TEST_PATTERN_MENU_TAB_CAPACITY; ++i) {
        const int x = tab_x0 + (int)i * (tab_w + tab_gap);
        const epd_test_pattern_menu_tab_t *tab = &overlay->tabs[i];
        if (tab->active) {
            epd_fill_rect(buffer, x, tab_y, tab_w, tab_h, true);
        } else {
            epd_draw_rect_outline(buffer, x, tab_y, tab_w, tab_h, 2);
        }
        if (tab->active) {
            epd_draw_text_maybe_font_inverted(buffer, footer_font, x + 18, tab_y + 10, tab->label, 2);
        } else {
            epd_draw_text_maybe_font(buffer, footer_font, x + 18, tab_y + 10, tab->label, 2);
        }
    }

    for (size_t i = 0; i < overlay->card_count && i < EPD_TEST_PATTERN_MENU_CARD_CAPACITY; ++i) {
        const int y = cards_y0 + (int)i * (card_h + card_gap);
        const epd_test_pattern_menu_card_t *card = &overlay->cards[i];

        if (card->selected) {
            epd_fill_rect(buffer, card_x, y, card_w, card_h, true);
        } else {
            if (frameless_panel) {
                epd_fill_rect(buffer, card_x, y, card_w, card_h, false);
            }
            epd_draw_rect_outline(buffer, card_x, y, card_w, card_h, 1);
        }

        if (overlay->compact_cards) {
            epd_draw_text_clipped_maybe_font_scaled(
                buffer,
                menu_font,
                card_x + 12,
                y + (frameless_panel ? 10 : 14),
                card->title,
                card_w - 24,
                2,
                ink_cpfont_is_loaded(menu_font) && menu_font->advance_y > 24U ? 2U : 1U,
                card->selected);
            if (card->line1[0] != '\0') {
                if (card->selected) {
                    epd_draw_text_clipped_maybe_font_scaled(
                        buffer,
                        footer_font,
                        card_x + 12,
                        y + (frameless_panel ? 28 : 28),
                        card->line1,
                        card_w - 24,
                        2,
                        epd_footer_font_scale_divisor(footer_font),
                        true);
                } else {
                    epd_draw_text_clipped_maybe_font_scaled(
                        buffer,
                        footer_font,
                        card_x + 12,
                        y + (frameless_panel ? 28 : 28),
                        card->line1,
                        card_w - 24,
                        2,
                        epd_footer_font_scale_divisor(footer_font),
                        false);
                }
            }
            continue;
        }

        const bool library_card = frameless_panel;
        const int favorite_icon_x = card_x + card_w - 28;
        const int title_max_width = library_card ? (card_w - 28) : (card_w - 52);
        const int line1_max_width = card_w - (card->trailing_favorite ? 42 : 20);

        if (card->selected) {
            epd_draw_text_clipped_maybe_font_scaled(
                buffer,
                menu_font,
                card_x + 14,
                y + 10,
                card->title,
                title_max_width,
                2,
                ink_cpfont_is_loaded(menu_font) && menu_font->advance_y > 22U ? 2U : 1U,
                true);
            if (!library_card) {
                epd_draw_text_maybe_font_scaled_inverted(
                    buffer,
                    footer_font,
                    card_x + 14,
                    y + 58,
                    card->line2,
                    2,
                    epd_footer_font_scale_divisor(footer_font));
            } else {
                epd_draw_text_clipped_maybe_font_scaled(
                    buffer,
                    footer_font,
                    card_x + 14,
                    y + 44,
                    card->line1,
                    line1_max_width,
                    2,
                    epd_footer_font_scale_divisor(footer_font),
                    true);
            }
            if (card->trailing_favorite) {
                epd_draw_heart_icon(buffer, favorite_icon_x, y + (library_card ? 42 : 34), true);
            }
        } else {
            epd_draw_text_clipped_maybe_font_scaled(
                buffer,
                menu_font,
                card_x + 14,
                y + 10,
                card->title,
                title_max_width,
                2,
                ink_cpfont_is_loaded(menu_font) && menu_font->advance_y > 22U ? 2U : 1U,
                false);
            if (!library_card) {
                epd_draw_text_maybe_font_scaled(
                    buffer,
                    footer_font,
                    card_x + 14,
                    y + 38,
                    card->line1,
                    2,
                    epd_footer_font_scale_divisor(footer_font));
                epd_draw_text_maybe_font_scaled(
                    buffer,
                    footer_font,
                    card_x + 14,
                    y + 58,
                    card->line2,
                    2,
                    epd_footer_font_scale_divisor(footer_font));
            } else {
                epd_draw_text_clipped_maybe_font_scaled(
                    buffer,
                    footer_font,
                    card_x + 14,
                    y + 44,
                    card->line1,
                    line1_max_width,
                    2,
                    epd_footer_font_scale_divisor(footer_font),
                    false);
            }
            if (card->trailing_favorite) {
                epd_draw_heart_icon(buffer, favorite_icon_x, y + (library_card ? 42 : 34), false);
            }
        }
    }

    if (overlay->action_popup_open) {
        epd_fill_rect(buffer, popup_x, popup_y, popup_w, popup_h, false);
        epd_draw_rect_outline(buffer, popup_x, popup_y, popup_w, popup_h, 2);
        epd_draw_text_clipped_maybe_font_scaled(
            buffer,
            menu_font,
            popup_x + 18,
            popup_y + 14,
            overlay->action_popup_title,
            popup_w - 36,
            2,
            ink_cpfont_is_loaded(menu_font) && menu_font->advance_y > 22U ? 2U : 1U,
            false);
        for (size_t i = 0; i < overlay->action_count && i < EPD_TEST_PATTERN_MENU_ACTION_CAPACITY; ++i) {
            const int action_y = popup_y + 52 + (int)i * (popup_action_h + popup_action_gap);
            const epd_test_pattern_menu_action_t *action = &overlay->actions[i];
            if (action->selected) {
                epd_fill_rect(buffer, popup_x + 18, action_y, popup_w - 36, popup_action_h, true);
                epd_draw_text_maybe_font_inverted(buffer, footer_font, popup_x + 30, action_y + 8, action->label, 2);
            } else {
                epd_draw_rect_outline(buffer, popup_x + 18, action_y, popup_w - 36, popup_action_h, 1);
                epd_draw_text_maybe_font(buffer, footer_font, popup_x + 30, action_y + 8, action->label, 2);
            }
        }
    }
}

void epd_test_pattern_fill_grid_compare_base_page(
    uint8_t *buffer,
    size_t length,
    const char *sweep_tag)
{
    if (buffer == NULL || length < EPD_GDEY0426T82_BUFFER_SIZE) {
        return;
    }

    memset(buffer, 0xFF, EPD_GDEY0426T82_BUFFER_SIZE);
    epd_fill_rect(buffer, 0, 0, EPD_GDEY0426T82_WIDTH, 6, true);
    epd_fill_rect(buffer, 0, EPD_GDEY0426T82_HEIGHT - 6, EPD_GDEY0426T82_WIDTH, 6, true);
    epd_draw_text(buffer, 20, 20, "GRID COMPARE 4X4", 3);
    epd_draw_text(buffer, 20, 48, "OLD PAGE -> NEW PAGE", 2);

    for (uint8_t i = 0; i < (GRID_COMPARE_COLS * GRID_COMPARE_ROWS); ++i) {
        epd_draw_grid_compare_cell_frame(buffer, i, sweep_tag);
    }
}

void epd_test_pattern_apply_grid_compare_cell(
    uint8_t *buffer,
    size_t length,
    uint8_t cell_index,
    const char *sweep_tag)
{
    if (buffer == NULL
        || length < EPD_GDEY0426T82_BUFFER_SIZE
        || cell_index >= (GRID_COMPARE_COLS * GRID_COMPARE_ROWS)) {
        return;
    }

    epd_draw_grid_compare_cell_variant(buffer, cell_index, sweep_tag);
}

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
    const char *status)
{
    static const char *kFallbackTitle = "TXT READER";
    static const char *kFallbackLine = "";
    const char *logical_lines[4] = {
        line1 != NULL ? line1 : kFallbackLine,
        line2 != NULL ? line2 : kFallbackLine,
        line3 != NULL ? line3 : kFallbackLine,
        line4 != NULL ? line4 : kFallbackLine,
    };
    char visual_lines[READER_SUBLINE_COUNT][64];

    if (buffer == NULL || length < EPD_GDEY0426T82_BUFFER_SIZE) {
        return;
    }

    memset(buffer, 0xFF, EPD_GDEY0426T82_BUFFER_SIZE);
    memset(visual_lines, 0, sizeof(visual_lines));

    epd_fill_rect(buffer, 0, 0, EPD_GDEY0426T82_WIDTH, 8, true);
    epd_fill_rect(buffer, 0, EPD_GDEY0426T82_HEIGHT - 8, EPD_GDEY0426T82_WIDTH, 8, true);
    epd_fill_rect(buffer, 0, 0, 8, EPD_GDEY0426T82_HEIGHT, true);
    epd_fill_rect(buffer, EPD_GDEY0426T82_WIDTH - 8, 0, 8, EPD_GDEY0426T82_HEIGHT, true);
    epd_fill_rect(buffer, 24, 78, EPD_GDEY0426T82_WIDTH - 48, 4, true);
    epd_fill_rect(buffer, 24, 730, EPD_GDEY0426T82_WIDTH - 48, 4, true);

    epd_draw_text_maybe_font(buffer, font, 28, 30, title != NULL ? title : kFallbackTitle, 3);
    if (epd_should_abort_draw(should_abort, should_abort_ctx)) {
        return;
    }

    for (size_t logical = 0; logical < 4U; ++logical) {
        char split[2][64];
        epd_reader_split_line(logical_lines[logical], split);
        memcpy(visual_lines[logical * 2U], split[0], sizeof(split[0]));
        memcpy(visual_lines[logical * 2U + 1U], split[1], sizeof(split[1]));
    }

    for (size_t i = 0; i < READER_SUBLINE_COUNT; ++i) {
        epd_draw_text_maybe_font(buffer, font, 32, s_reader_line_y[i], visual_lines[i], 3);
        if (epd_should_abort_draw(should_abort, should_abort_ctx)) {
            return;
        }
    }

    epd_draw_text_maybe_font(buffer, font, 32, 690, status != NULL ? status : kFallbackLine, 3);
}

