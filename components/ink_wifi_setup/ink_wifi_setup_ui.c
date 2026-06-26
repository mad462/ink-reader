#include "ink_wifi_setup_ui.h"

#include <stdio.h>
#include <string.h>

enum {
    KEY_H = 44,
    KEY_GAP_X = 4,
    KEY_GAP_Y = 8,
    KEY_X0 = 16,
    KEY_Y0 = 520,
    KEY_TEXT_SCALE = 3,
    KEY_SMALL_TEXT_SCALE = 2,
    KEY_SELECTION_BAR_H = 6,
    KEYBOARD_W = EPD_GDEY0426T82_WIDTH - 2 * KEY_X0,
    KEYBOARD_H = INK_WIFI_SETUP_KEYBOARD_ROWS * KEY_H + (INK_WIFI_SETUP_KEYBOARD_ROWS - 1) * KEY_GAP_Y,
    WIFI_LIST_TOP_Y = 100,
    WIFI_LIST_ROW_H = 58,
    WIFI_LIST_BOTTOM_Y = EPD_GDEY0426T82_HEIGHT - 8,
    WIFI_LIST_CARD_X = 18,
    WIFI_LIST_CARD_W = EPD_GDEY0426T82_WIDTH - 36,
    WIFI_LIST_CARD_INSET = 14,
    WIFI_SAVED_MENU_X = 48,
    WIFI_SAVED_MENU_Y = 210,
    WIFI_SAVED_MENU_W = EPD_GDEY0426T82_WIDTH - 96,
    WIFI_SAVED_MENU_H = 220,
    PASSWORD_BOX_X = 24,
    PASSWORD_BOX_Y = 136,
    PASSWORD_BOX_W = EPD_GDEY0426T82_WIDTH - 48,
    PASSWORD_BOX_H = 72,
};

typedef struct {
    char c;
    uint8_t rows[7];
} glyph5x7_t;

static const char *const s_keyboard_layer_names[INK_WIFI_SETUP_KEYBOARD_LAYER_COUNT] = {
    [INK_WIFI_SETUP_KEYBOARD_LAYER_LOWER] = "LOWER",
    [INK_WIFI_SETUP_KEYBOARD_LAYER_UPPER] = "UPPER",
    [INK_WIFI_SETUP_KEYBOARD_LAYER_SYMBOL] = "SYMBOL",
};

static const glyph5x7_t s_font5x7[] = {
    {' ', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {'!', {0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04}},
    {'"', {0x0A, 0x0A, 0x0A, 0x00, 0x00, 0x00, 0x00}},
    {'#', {0x0A, 0x0A, 0x1F, 0x0A, 0x1F, 0x0A, 0x0A}},
    {'$', {0x04, 0x0F, 0x14, 0x0E, 0x05, 0x1E, 0x04}},
    {'%', {0x19, 0x19, 0x02, 0x04, 0x08, 0x13, 0x13}},
    {'&', {0x0C, 0x12, 0x14, 0x08, 0x15, 0x12, 0x0D}},
    {'\'', {0x0C, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00}},
    {'(', {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02}},
    {')', {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08}},
    {'*', {0x00, 0x04, 0x15, 0x0E, 0x15, 0x04, 0x00}},
    {'+', {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00}},
    {',', {0x00, 0x00, 0x00, 0x00, 0x0C, 0x04, 0x08}},
    {'-', {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}},
    {'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C}},
    {'/', {0x01, 0x02, 0x02, 0x04, 0x08, 0x10, 0x10}},
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
    {':', {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00}},
    {';', {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x04, 0x08}},
    {'=', {0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00}},
    {'?', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04}},
    {'@', {0x0E, 0x11, 0x17, 0x15, 0x17, 0x10, 0x0E}},
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
    {'Z', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}},
    {'[', {0x0E, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0E}},
    {'\\', {0x10, 0x08, 0x08, 0x04, 0x02, 0x01, 0x01}},
    {']', {0x0E, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0E}},
    {'_', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F}},
    {'`', {0x08, 0x04, 0x02, 0x00, 0x00, 0x00, 0x00}},
    {'a', {0x00, 0x00, 0x0E, 0x01, 0x0F, 0x11, 0x0F}},
    {'b', {0x10, 0x10, 0x16, 0x19, 0x11, 0x19, 0x16}},
    {'c', {0x00, 0x00, 0x0E, 0x10, 0x10, 0x11, 0x0E}},
    {'d', {0x01, 0x01, 0x0D, 0x13, 0x11, 0x13, 0x0D}},
    {'e', {0x00, 0x00, 0x0E, 0x11, 0x1F, 0x10, 0x0E}},
    {'f', {0x06, 0x09, 0x08, 0x1C, 0x08, 0x08, 0x08}},
    {'g', {0x00, 0x00, 0x0F, 0x11, 0x0F, 0x01, 0x0E}},
    {'h', {0x10, 0x10, 0x16, 0x19, 0x11, 0x11, 0x11}},
    {'i', {0x04, 0x00, 0x0C, 0x04, 0x04, 0x04, 0x0E}},
    {'j', {0x02, 0x00, 0x06, 0x02, 0x02, 0x12, 0x0C}},
    {'k', {0x10, 0x10, 0x12, 0x14, 0x18, 0x14, 0x12}},
    {'l', {0x0C, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'m', {0x00, 0x00, 0x1A, 0x15, 0x15, 0x15, 0x15}},
    {'n', {0x00, 0x00, 0x16, 0x19, 0x11, 0x11, 0x11}},
    {'o', {0x00, 0x00, 0x0E, 0x11, 0x11, 0x11, 0x0E}},
    {'p', {0x00, 0x00, 0x16, 0x19, 0x16, 0x10, 0x10}},
    {'q', {0x00, 0x00, 0x0D, 0x13, 0x0D, 0x01, 0x01}},
    {'r', {0x00, 0x00, 0x16, 0x19, 0x10, 0x10, 0x10}},
    {'s', {0x00, 0x00, 0x0F, 0x10, 0x0E, 0x01, 0x1E}},
    {'t', {0x08, 0x08, 0x1C, 0x08, 0x08, 0x09, 0x06}},
    {'u', {0x00, 0x00, 0x11, 0x11, 0x11, 0x13, 0x0D}},
    {'v', {0x00, 0x00, 0x11, 0x11, 0x11, 0x0A, 0x04}},
    {'w', {0x00, 0x00, 0x11, 0x15, 0x15, 0x15, 0x0A}},
    {'x', {0x00, 0x00, 0x11, 0x0A, 0x04, 0x0A, 0x11}},
    {'y', {0x00, 0x00, 0x11, 0x11, 0x0F, 0x01, 0x0E}},
    {'z', {0x00, 0x00, 0x1F, 0x02, 0x04, 0x08, 0x1F}},
    {'{', {0x02, 0x04, 0x04, 0x08, 0x04, 0x04, 0x02}},
    {'|', {0x04, 0x04, 0x04, 0x00, 0x04, 0x04, 0x04}},
    {'}', {0x08, 0x04, 0x04, 0x02, 0x04, 0x04, 0x08}},
};

static void set_pixel(uint8_t *buffer, int x, int y, bool black)
{
    if (buffer == NULL || x < 0 || x >= EPD_GDEY0426T82_WIDTH || y < 0 || y >= EPD_GDEY0426T82_HEIGHT) {
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

static void fill_rect(uint8_t *buffer, int x, int y, int w, int h, bool black)
{
    for (int yy = y; yy < y + h; ++yy) {
        for (int xx = x; xx < x + w; ++xx) {
            set_pixel(buffer, xx, yy, black);
        }
    }
}

static void draw_rect_outline(uint8_t *buffer, int x, int y, int w, int h, int thickness)
{
    fill_rect(buffer, x, y, w, thickness, true);
    fill_rect(buffer, x, y + h - thickness, w, thickness, true);
    fill_rect(buffer, x, y, thickness, h, true);
    fill_rect(buffer, x + w - thickness, y, thickness, h, true);
}

static const glyph5x7_t *find_glyph(char c)
{
    for (size_t i = 0; i < sizeof(s_font5x7) / sizeof(s_font5x7[0]); ++i) {
        if (s_font5x7[i].c == c) {
            return &s_font5x7[i];
        }
    }
    return &s_font5x7[0];
}

static int measure_text_width(const char *text, int scale)
{
    if (text == NULL || text[0] == '\0') {
        return 0;
    }
    return (int)strlen(text) * 6 * scale - scale;
}

static void draw_glyph(uint8_t *buffer, int x, int y, char c, int scale, bool black)
{
    const glyph5x7_t *glyph = find_glyph(c);

    for (int row = 0; row < 7; ++row) {
        for (int col = 0; col < 5; ++col) {
            if ((glyph->rows[row] & (uint8_t)(1U << (4 - col))) != 0) {
                fill_rect(buffer, x + col * scale, y + row * scale, scale, scale, black);
            }
        }
    }
}

static void draw_text(uint8_t *buffer, int x, int y, const char *text, int scale, bool black)
{
    if (text == NULL) {
        return;
    }

    int cursor_x = x;
    for (const char *p = text; *p != '\0'; ++p) {
        draw_glyph(buffer, cursor_x, y, *p, scale, black);
        cursor_x += 6 * scale;
    }
}

static void draw_ui_text(
    uint8_t *buffer,
    ink_cpfont_t *font,
    int x,
    int y,
    const char *text,
    int fallback_scale,
    uint8_t font_scale_divisor,
    bool black)
{
    if (font != NULL && ink_cpfont_is_loaded(font)) {
        esp_err_t ret = black
            ? ink_cpfont_draw_text_bw_scaled(font, buffer, x, y, text, font_scale_divisor, NULL)
            : ink_cpfont_draw_text_bw_scaled_inverted(font, buffer, x, y, text, font_scale_divisor, NULL);
        if (ret == ESP_OK) {
            return;
        }
    }
    draw_text(buffer, x, y, text, fallback_scale, black);
}

static void draw_ui_text_inverted(
    uint8_t *buffer,
    ink_cpfont_t *font,
    int x,
    int y,
    const char *text,
    int fallback_scale,
    uint8_t font_scale_divisor)
{
    draw_ui_text(buffer, font, x, y, text, fallback_scale, font_scale_divisor, false);
}

static void copy_ascii_clipped(char *dst, size_t dst_size, const char *src, size_t max_chars)
{
    size_t written = 0;

    if (dst == NULL || dst_size == 0) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }

    while (src[written] != '\0' && written + 1 < dst_size && written < max_chars) {
        const unsigned char c = (unsigned char)src[written];
        dst[written] = c >= 0x20 && c < 0x7f ? (char)c : '?';
        ++written;
    }
    dst[written] = '\0';
}

static void draw_centered_text(uint8_t *buffer, int x, int y, int w, int h, const char *text, int scale, bool black)
{
    const int text_w = measure_text_width(text, scale);
    const int text_h = 7 * scale;
    draw_text(buffer, x + (w - text_w) / 2, y + (h - text_h) / 2, text, scale, black);
}

static int keyboard_row_key_width(int row)
{
    const int key_count = ink_wifi_setup_keyboard_row_key_count(row);
    return (KEYBOARD_W - (key_count - 1) * KEY_GAP_X) / key_count;
}

static int keyboard_row_extra_width(int row)
{
    const int key_count = ink_wifi_setup_keyboard_row_key_count(row);
    return (KEYBOARD_W - (key_count - 1) * KEY_GAP_X) % key_count;
}

const char *ink_wifi_setup_keyboard_layer_name(ink_wifi_setup_keyboard_layer_t layer)
{
    if (layer < 0 || layer >= INK_WIFI_SETUP_KEYBOARD_LAYER_COUNT) {
        return "";
    }
    return s_keyboard_layer_names[layer];
}

int ink_wifi_setup_ui_wifi_list_visible_first(const wifi_setup_state_t *wifi)
{
    const int count = ink_wifi_setup_selectable_count(wifi);
    if (count <= 0 || wifi == NULL) {
        return 0;
    }

    const int visible_capacity = (WIFI_LIST_BOTTOM_Y - WIFI_LIST_TOP_Y) / WIFI_LIST_ROW_H;
    const int max_visible = count < visible_capacity ? count : visible_capacity;
    int first = wifi->selected_index - max_visible / 2;
    if (first < 0) {
        first = 0;
    }
    if (first + max_visible > count) {
        first = count - max_visible;
    }
    return first < 0 ? 0 : first;
}

bool ink_wifi_setup_ui_wifi_list_row_region(
    const wifi_setup_state_t *wifi,
    int index,
    ink_wifi_setup_ui_region_t *region)
{
    const int count = ink_wifi_setup_selectable_count(wifi);
    if (region != NULL) {
        memset(region, 0, sizeof(*region));
    }
    if (wifi == NULL || index < 0 || index >= count) {
        return false;
    }

    const int visible_capacity = (WIFI_LIST_BOTTOM_Y - WIFI_LIST_TOP_Y) / WIFI_LIST_ROW_H;
    const int max_visible = count < visible_capacity ? count : visible_capacity;
    const int first = ink_wifi_setup_ui_wifi_list_visible_first(wifi);
    if (index < first || index >= first + max_visible) {
        return false;
    }

    if (region != NULL) {
        region->x = WIFI_LIST_CARD_X;
        region->y = WIFI_LIST_TOP_Y + (index - first) * WIFI_LIST_ROW_H - 4;
        region->w = WIFI_LIST_CARD_W;
        region->h = WIFI_LIST_ROW_H - 4;
    }
    return true;
}

bool ink_wifi_setup_ui_keyboard_key_region(int column, int row, ink_wifi_setup_ui_region_t *region)
{
    if (region != NULL) {
        memset(region, 0, sizeof(*region));
    }
    if (row < 0 || row >= INK_WIFI_SETUP_KEYBOARD_ROWS
        || column < 0 || column >= ink_wifi_setup_keyboard_row_key_count(row)) {
        return false;
    }

    int key_x = KEY_X0;
    const int base_w = keyboard_row_key_width(row);
    const int extra_w = keyboard_row_extra_width(row);
    const int key_w = base_w + (column < extra_w ? 1 : 0);
    for (int i = 0; i < column; ++i) {
        key_x += base_w + (i < extra_w ? 1 : 0) + KEY_GAP_X;
    }

    if (region != NULL) {
        region->x = key_x;
        region->y = KEY_Y0 + row * (KEY_H + KEY_GAP_Y);
        region->w = key_w;
        region->h = KEY_H;
    }
    return true;
}

void ink_wifi_setup_ui_full_screen_region(ink_wifi_setup_ui_region_t *region)
{
    if (region == NULL) {
        return;
    }
    region->x = 0;
    region->y = 0;
    region->w = EPD_GDEY0426T82_WIDTH;
    region->h = EPD_GDEY0426T82_HEIGHT;
}

void ink_wifi_setup_ui_list_body_region(ink_wifi_setup_ui_region_t *region)
{
    if (region == NULL) {
        return;
    }
    region->x = 0;
    region->y = 24;
    region->w = EPD_GDEY0426T82_WIDTH;
    region->h = WIFI_LIST_BOTTOM_Y - 24;
}

void ink_wifi_setup_ui_password_screen_region(ink_wifi_setup_ui_region_t *region)
{
    if (region == NULL) {
        return;
    }
    region->x = 0;
    region->y = 24;
    region->w = EPD_GDEY0426T82_WIDTH;
    region->h = EPD_GDEY0426T82_HEIGHT - 24;
}

void ink_wifi_setup_ui_password_box_region(ink_wifi_setup_ui_region_t *region)
{
    if (region == NULL) {
        return;
    }
    region->x = PASSWORD_BOX_X - 8;
    region->y = PASSWORD_BOX_Y - 8;
    region->w = PASSWORD_BOX_W + 16;
    region->h = PASSWORD_BOX_H + 16;
}

void ink_wifi_setup_ui_saved_menu_region(ink_wifi_setup_ui_region_t *region)
{
    if (region == NULL) {
        return;
    }
    region->x = WIFI_SAVED_MENU_X - 8;
    region->y = WIFI_SAVED_MENU_Y - 8;
    region->w = WIFI_SAVED_MENU_W + 16;
    region->h = WIFI_SAVED_MENU_H + 16;
}

void ink_wifi_setup_ui_keyboard_footer_region(ink_wifi_setup_ui_region_t *region)
{
    if (region == NULL) {
        return;
    }
    region->x = 0;
    region->y = 224;
    region->w = EPD_GDEY0426T82_WIDTH;
    region->h = EPD_GDEY0426T82_HEIGHT - 224;
}

void ink_wifi_setup_ui_draw_keyboard_key(
    uint8_t *buffer,
    ink_wifi_setup_keyboard_layer_t layer,
    int column,
    int row,
    bool selected)
{
    ink_wifi_setup_ui_region_t region;
    if (!ink_wifi_setup_ui_keyboard_key_region(column, row, &region)) {
        return;
    }

    const char *label = ink_wifi_setup_keyboard_label(layer, column, row);
    const int scale = strlen(label) > 2 ? KEY_SMALL_TEXT_SCALE : KEY_TEXT_SCALE;

    fill_rect(buffer, region.x, region.y, region.w, region.h, false);
    draw_rect_outline(buffer, region.x, region.y, region.w, region.h, 3);
    draw_centered_text(buffer, region.x, region.y, region.w, region.h, label, scale, true);
    if (selected) {
        fill_rect(
            buffer,
            region.x + 4,
            region.y + KEY_H - KEY_SELECTION_BAR_H - 4,
            region.w - 8,
            KEY_SELECTION_BAR_H,
            true);
    }
}

static void draw_keyboard_status(
    uint8_t *buffer,
    ink_wifi_setup_keyboard_layer_t layer,
    const ink_wifi_setup_ui_cursor_t *state,
    const ink_wifi_setup_keyboard_text_t *text,
    const wifi_setup_state_t *wifi,
    const ink_wifi_setup_ui_fonts_t *fonts)
{
    char preview[96];
    char wifi_line[96];
    char wifi_status[96];
    char ssid[24];
    const char *value = text != NULL ? text->text : "";
    const ink_wifi_scan_result_t *ap = ink_wifi_setup_selected_ap(wifi);
    ink_cpfont_t *menu = fonts != NULL ? fonts->menu : NULL;
    ink_cpfont_t *footer = fonts != NULL ? fonts->footer : NULL;

    (void)state;
    copy_ascii_clipped(preview, sizeof(preview), value, 24);
    if (ap != NULL && wifi != NULL) {
        copy_ascii_clipped(ssid, sizeof(ssid), ap->ssid, 18);
        snprintf(
            wifi_line,
            sizeof(wifi_line),
            "AP %d/%u %s %ddBm%s",
            wifi->selected_index + 1,
            (unsigned)wifi->scan.count,
            ssid,
            (int)ap->rssi,
            ap->saved ? " SAVED" : "");
    } else {
        snprintf(wifi_line, sizeof(wifi_line), "NO WIFI SELECTED");
    }
    if (wifi != NULL && wifi->status.connected) {
        copy_ascii_clipped(ssid, sizeof(ssid), wifi->status.ssid, 18);
        snprintf(wifi_status, sizeof(wifi_status), "CONNECTED %s %ddBm", ssid, (int)wifi->status.rssi);
    } else if (wifi != NULL && wifi->status.last_error != ESP_OK) {
        snprintf(wifi_status, sizeof(wifi_status), "WIFI %s", esp_err_to_name(wifi->status.last_error));
    } else {
        snprintf(wifi_status, sizeof(wifi_status), "OFFLINE");
    }

    draw_ui_text(buffer, menu, 24, 34, "WiFi Password", 2, 1, true);
    draw_ui_text(buffer, footer, 24, 76, wifi_line, 1, 1, true);
    draw_ui_text(buffer, footer, 24, 106, wifi_status, 1, 1, true);
    draw_rect_outline(buffer, PASSWORD_BOX_X, PASSWORD_BOX_Y, PASSWORD_BOX_W, PASSWORD_BOX_H, 3);
    if (preview[0] != '\0') {
        draw_text(buffer, 36, 156, preview, 3, true);
    }
    draw_ui_text(buffer, footer, 24, 224, ink_wifi_setup_keyboard_layer_name(layer), 1, 1, true);
}

static void draw_keyboard(
    uint8_t *buffer,
    ink_wifi_setup_keyboard_layer_t layer,
    const ink_wifi_setup_ui_cursor_t *state,
    const ink_wifi_setup_keyboard_text_t *text,
    const wifi_setup_state_t *wifi,
    const ink_wifi_setup_ui_fonts_t *fonts)
{
    memset(buffer, 0xFF, EPD_GDEY0426T82_BUFFER_SIZE);
    draw_keyboard_status(buffer, layer, state, text, wifi, fonts);
    draw_rect_outline(buffer, KEY_X0 - 10, KEY_Y0 - 10, KEYBOARD_W + 20, KEYBOARD_H + 20, 3);

    for (int row = 0; row < INK_WIFI_SETUP_KEYBOARD_ROWS; ++row) {
        for (int col = 0; col < ink_wifi_setup_keyboard_row_key_count(row); ++col) {
            const bool selected = state != NULL && row == state->row && col == state->column;
            ink_wifi_setup_ui_draw_keyboard_key(buffer, layer, col, row, selected);
        }
    }
}

void ink_wifi_setup_ui_draw_wifi_list_row(
    uint8_t *buffer,
    const wifi_setup_state_t *wifi,
    const ink_wifi_setup_ui_fonts_t *fonts,
    int index)
{
    const int count = ink_wifi_setup_selectable_count(wifi);
    ink_wifi_setup_ui_region_t region;
    char ssid[30];
    char meta[64];
    ink_cpfont_t *menu = fonts != NULL ? fonts->menu : NULL;
    ink_cpfont_t *footer = fonts != NULL ? fonts->footer : NULL;

    if (buffer == NULL || wifi == NULL || index < 0 || index >= count
        || !ink_wifi_setup_ui_wifi_list_row_region(wifi, index, &region)) {
        return;
    }

    if (index < wifi->scan.count) {
        const ink_wifi_scan_result_t *ap = &wifi->scan.results[index];
        const bool connected = wifi->status.connected && strcmp(wifi->status.ssid, ap->ssid) == 0;
        copy_ascii_clipped(ssid, sizeof(ssid), ap->ssid, 24);
        snprintf(
            meta,
            sizeof(meta),
            ap->ap_count > 1 ? "%s  %ddBm  AP x%u" : "%s  %ddBm",
            connected ? "CONNECTED" : (ap->saved ? "SAVED" : "NEW"),
            (int)ap->rssi,
            (unsigned)ap->ap_count);
    } else {
        snprintf(ssid, sizeof(ssid), "%s", "SCAN");
        snprintf(meta, sizeof(meta), "%s", wifi->scan_in_progress ? "Scanning ..." : "Refresh WiFi list");
    }

    const bool selected = index == wifi->selected_index;
    fill_rect(buffer, region.x, region.y, region.w, region.h, selected);
    draw_rect_outline(buffer, region.x, region.y, region.w, region.h, selected ? 3 : 1);
    if (selected) {
        draw_ui_text_inverted(buffer, menu, region.x + WIFI_LIST_CARD_INSET, region.y + 8, ssid, 2, 1);
        draw_ui_text_inverted(buffer, footer, region.x + WIFI_LIST_CARD_INSET, region.y + 34, meta, 1, 1);
    } else {
        draw_ui_text(buffer, menu, region.x + WIFI_LIST_CARD_INSET, region.y + 8, ssid, 2, 1, true);
        draw_ui_text(buffer, footer, region.x + WIFI_LIST_CARD_INSET, region.y + 34, meta, 1, 1, true);
    }
}

static void draw_wifi_list(
    uint8_t *buffer,
    const wifi_setup_state_t *wifi,
    const ink_wifi_setup_ui_fonts_t *fonts)
{
    const int count = ink_wifi_setup_selectable_count(wifi);
    ink_cpfont_t *menu = fonts != NULL ? fonts->menu : NULL;
    ink_cpfont_t *footer = fonts != NULL ? fonts->footer : NULL;

    memset(buffer, 0xFF, EPD_GDEY0426T82_BUFFER_SIZE);
    draw_ui_text(buffer, menu, 24, 34, "WiFi Setup", 2, 1, true);
    if (wifi != NULL && wifi->mode == WIFI_SETUP_UI_CONNECTING) {
        draw_ui_text(buffer, footer, 24, 70, "Connecting ...", 1, 1, true);
    } else if (wifi != NULL && wifi->scan_in_progress) {
        draw_ui_text(buffer, footer, 24, 70, "Scanning nearby WiFi ...", 1, 1, true);
    } else {
        draw_ui_text(buffer, footer, 24, 70, "WiFi networks", 1, 1, true);
    }

    if (wifi == NULL || count == 0) {
        return;
    }

    const int visible_capacity = (WIFI_LIST_BOTTOM_Y - WIFI_LIST_TOP_Y) / WIFI_LIST_ROW_H;
    const int max_visible = count < visible_capacity ? count : visible_capacity;
    const int first = ink_wifi_setup_ui_wifi_list_visible_first(wifi);
    for (int i = 0; i < max_visible; ++i) {
        ink_wifi_setup_ui_draw_wifi_list_row(buffer, wifi, fonts, first + i);
    }
}

static void draw_result_popup(
    uint8_t *buffer,
    const wifi_setup_state_t *wifi,
    const ink_wifi_setup_ui_fonts_t *fonts)
{
    char line[96];
    char ssid[24];
    const ink_wifi_scan_result_t *ap = ink_wifi_setup_selected_ap(wifi);
    ink_cpfont_t *menu = fonts != NULL ? fonts->menu : NULL;
    ink_cpfont_t *footer = fonts != NULL ? fonts->footer : NULL;

    draw_wifi_list(buffer, wifi, fonts);
    fill_rect(buffer, 48, 210, EPD_GDEY0426T82_WIDTH - 96, 190, false);
    draw_rect_outline(buffer, 48, 210, EPD_GDEY0426T82_WIDTH - 96, 190, 3);

    if (ap != NULL) {
        copy_ascii_clipped(ssid, sizeof(ssid), ap->ssid, 18);
    } else {
        snprintf(ssid, sizeof(ssid), "%s", "-");
    }

    if (wifi != NULL && wifi->mode == WIFI_SETUP_UI_CONNECTING) {
        snprintf(line, sizeof(line), "Connecting %s ...", ssid);
        draw_ui_text(buffer, menu, 76, 250, "Connecting", 2, 1, true);
        draw_ui_text(buffer, footer, 76, 302, line, 1, 1, true);
        draw_ui_text(buffer, footer, 76, 344, "Please wait ...", 1, 1, true);
        return;
    }

    const bool ok = wifi != NULL && wifi->result_error == ESP_OK;
    draw_ui_text(buffer, menu, 76, 250, ok ? "Success" : "Failed", 2, 1, true);
    snprintf(line, sizeof(line), "%s %s", ok ? "Connected" : "Not connected", ssid);
    draw_ui_text(buffer, footer, 76, 304, line, 1, 1, true);
    if (!ok && wifi != NULL) {
        snprintf(line, sizeof(line), "Reason: %s", esp_err_to_name(wifi->result_error));
        draw_ui_text(buffer, footer, 76, 334, line, 1, 1, true);
    }
    draw_ui_text(buffer, footer, 76, 364, "Press OK to return", 1, 1, true);
}

static void draw_saved_wifi_menu(
    uint8_t *buffer,
    const wifi_setup_state_t *wifi,
    const ink_wifi_setup_ui_fonts_t *fonts)
{
    char line[96];
    char ssid[24];
    const ink_wifi_scan_result_t *ap = ink_wifi_setup_selected_ap(wifi);
    ink_cpfont_t *menu = fonts != NULL ? fonts->menu : NULL;
    ink_cpfont_t *footer = fonts != NULL ? fonts->footer : NULL;

    draw_wifi_list(buffer, wifi, fonts);
    fill_rect(buffer, 48, 210, EPD_GDEY0426T82_WIDTH - 96, 220, false);
    draw_rect_outline(buffer, 48, 210, EPD_GDEY0426T82_WIDTH - 96, 220, 3);

    if (ap != NULL) {
        copy_ascii_clipped(ssid, sizeof(ssid), ap->ssid, 18);
    } else {
        snprintf(ssid, sizeof(ssid), "%s", "-");
    }

    draw_ui_text(buffer, menu, 76, 244, "Saved WiFi", 2, 1, true);
    snprintf(line, sizeof(line), "%s", ssid);
    draw_ui_text(buffer, footer, 76, 292, line, 1, 1, true);

    const bool connect_selected = wifi == NULL || wifi->menu_index == 0;
    const int btn_y = 334;
    fill_rect(buffer, 76, btn_y, 150, 46, connect_selected);
    draw_rect_outline(buffer, 76, btn_y, 150, 46, connect_selected ? 3 : 1);
    if (connect_selected) {
        draw_ui_text_inverted(buffer, footer, 96, btn_y + 14, "CONNECT", 1, 1);
    } else {
        draw_ui_text(buffer, footer, 96, btn_y + 14, "CONNECT", 1, 1, true);
    }

    fill_rect(buffer, 254, btn_y, 150, 46, !connect_selected);
    draw_rect_outline(buffer, 254, btn_y, 150, 46, connect_selected ? 1 : 3);
    if (!connect_selected) {
        draw_ui_text_inverted(buffer, footer, 282, btn_y + 14, "DELETE", 1, 1);
    } else {
        draw_ui_text(buffer, footer, 282, btn_y + 14, "DELETE", 1, 1, true);
    }
}

void ink_wifi_setup_ui_draw_screen(
    uint8_t *buffer,
    ink_wifi_setup_keyboard_layer_t layer,
    const ink_wifi_setup_ui_cursor_t *keyboard_state,
    const ink_wifi_setup_keyboard_text_t *text,
    const wifi_setup_state_t *wifi,
    const ink_wifi_setup_ui_fonts_t *fonts)
{
    if (wifi != NULL && wifi->mode == WIFI_SETUP_UI_SAVED_MENU) {
        draw_saved_wifi_menu(buffer, wifi, fonts);
    } else if (wifi != NULL && wifi->mode == WIFI_SETUP_UI_PASSWORD) {
        draw_keyboard(buffer, layer, keyboard_state, text, wifi, fonts);
    } else if (wifi != NULL && (wifi->mode == WIFI_SETUP_UI_CONNECTING || wifi->mode == WIFI_SETUP_UI_RESULT)) {
        draw_result_popup(buffer, wifi, fonts);
    } else {
        draw_wifi_list(buffer, wifi, fonts);
    }
}

void ink_wifi_setup_ui_expand_region(
    ink_wifi_setup_ui_region_t *area,
    const ink_wifi_setup_ui_region_t *region)
{
    if (area == NULL || region == NULL || region->w <= 0 || region->h <= 0) {
        return;
    }

    const int area_right = area->x + area->w;
    const int area_bottom = area->y + area->h;
    const int region_right = region->x + region->w;
    const int region_bottom = region->y + region->h;

    area->x = region->x < area->x ? region->x : area->x;
    area->y = region->y < area->y ? region->y : area->y;
    area->w = (region_right > area_right ? region_right : area_right) - area->x;
    area->h = (region_bottom > area_bottom ? region_bottom : area_bottom) - area->y;
}

bool ink_wifi_setup_ui_region_is_valid(const ink_wifi_setup_ui_region_t *region)
{
    return region != NULL && region->w > 0 && region->h > 0;
}

void ink_wifi_setup_ui_pad_align_region(ink_wifi_setup_ui_region_t *region, int pad)
{
    if (region == NULL) {
        return;
    }

    int x = region->x - pad;
    int y = region->y - pad;
    int right = region->x + region->w + pad;
    int bottom = region->y + region->h + pad;

    if (x < 0) {
        x = 0;
    }
    if (y < 0) {
        y = 0;
    }
    if (right > EPD_GDEY0426T82_WIDTH) {
        right = EPD_GDEY0426T82_WIDTH;
    }
    if (bottom > EPD_GDEY0426T82_HEIGHT) {
        bottom = EPD_GDEY0426T82_HEIGHT;
    }

    x &= ~7;
    y &= ~7;
    right = (right + 7) & ~7;
    bottom = (bottom + 7) & ~7;
    if (right > EPD_GDEY0426T82_WIDTH) {
        right = EPD_GDEY0426T82_WIDTH;
    }
    if (bottom > EPD_GDEY0426T82_HEIGHT) {
        bottom = EPD_GDEY0426T82_HEIGHT;
    }

    region->x = x;
    region->y = y;
    region->w = right - x;
    region->h = bottom - y;
}

static bool test_visible_first_centers_selection(void)
{
    wifi_setup_state_t state = {0};
    state.scan.count = 8;
    state.selected_index = 4;
    return ink_wifi_setup_ui_wifi_list_visible_first(&state) == 0;
}

static bool test_wifi_row_region_for_scan_action(void)
{
    wifi_setup_state_t state = {0};
    state.scan.count = 2;
    state.selected_index = 0;
    ink_wifi_setup_ui_region_t region;
    return ink_wifi_setup_ui_wifi_list_row_region(&state, 2, &region)
        && region.x == WIFI_LIST_CARD_X
        && region.y == WIFI_LIST_TOP_Y + 2 * WIFI_LIST_ROW_H - 4
        && region.w == WIFI_LIST_CARD_W
        && region.h == WIFI_LIST_ROW_H - 4;
}

static bool test_keyboard_key_region_matches_grid(void)
{
    ink_wifi_setup_ui_region_t region;
    return ink_wifi_setup_ui_keyboard_key_region(0, 0, &region)
        && region.x == KEY_X0
        && region.y == KEY_Y0
        && region.h == KEY_H;
}

static bool test_pad_align_region_expands_and_aligns(void)
{
    ink_wifi_setup_ui_region_t region = {.x = 25, .y = 27, .w = 11, .h = 13};
    ink_wifi_setup_ui_pad_align_region(&region, 4);
    return region.x == 16 && region.y == 16 && region.w == 24 && region.h == 32;
}

bool ink_wifi_setup_ui_self_test(void)
{
    return test_visible_first_centers_selection()
        && test_wifi_row_region_for_scan_action()
        && test_keyboard_key_region_matches_grid()
        && test_pad_align_region_expands_and_aligns();
}
