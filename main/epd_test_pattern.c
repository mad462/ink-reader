#include "epd_test_pattern.h"

#include <stdbool.h>
#include <string.h>

#include "epd_gdey0426t82.h"

typedef struct {
    char c;
    uint8_t rows[7];
} epd_glyph_t;

static const epd_glyph_t s_font[] = {
    {'#', {0x0A, 0x0A, 0x1F, 0x0A, 0x1F, 0x0A, 0x0A}},
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
};

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

static void epd_draw_rect_outline(uint8_t *buffer, int x, int y, int w, int h, int thickness)
{
    epd_fill_rect(buffer, x, y, w, thickness, true);
    epd_fill_rect(buffer, x, y + h - thickness, w, thickness, true);
    epd_fill_rect(buffer, x, y, thickness, h, true);
    epd_fill_rect(buffer, x + w - thickness, y, thickness, h, true);
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

    epd_gray_mark_region(msb_buffer, GRAY_BAR_X + 5, light_top, GRAY_BAR_WIDTH - 10, GRAY_BAR_HEIGHT - 10);
    epd_gray_mark_region(lsb_buffer, GRAY_BAR_X + 5, dark_top, GRAY_BAR_WIDTH - 10, GRAY_BAR_HEIGHT - 10);
    epd_gray_mark_region(msb_buffer, GRAY_BAR_X + 5, dark_top, GRAY_BAR_WIDTH - 10, GRAY_BAR_HEIGHT - 10);
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
    if (!epd_pixel_is_black(msb, bar_center_x, dark_y)) {
        goto cleanup;
    }
    if (epd_pixel_is_black(lsb, bar_center_x, black_y) || epd_pixel_is_black(msb, bar_center_x, black_y)) {
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

    epd_draw_text(buffer, 28, 30, title != NULL ? title : kFallbackTitle, 3);
    epd_draw_text(buffer, 32, 115, line1 != NULL ? line1 : kFallbackLine, 3);
    epd_draw_text(buffer, 32, 185, line2 != NULL ? line2 : kFallbackLine, 3);
    epd_draw_text(buffer, 32, 255, line3 != NULL ? line3 : kFallbackLine, 3);
    epd_draw_text(buffer, 32, 325, line4 != NULL ? line4 : kFallbackLine, 3);
    epd_draw_text(buffer, 32, 690, line5 != NULL ? line5 : kFallbackLine, 3);
}

