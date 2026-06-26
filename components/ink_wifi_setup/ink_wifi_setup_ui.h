#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "epd_gdey0426t82.h"
#include "ink_cpfont.h"
#include "ink_wifi_setup_input.h"
#include "ink_wifi_setup_state.h"

typedef struct {
    int column;
    int row;
} ink_wifi_setup_ui_cursor_t;

typedef struct {
    int x;
    int y;
    int w;
    int h;
} ink_wifi_setup_ui_region_t;

typedef struct {
    ink_cpfont_t *menu;
    ink_cpfont_t *footer;
} ink_wifi_setup_ui_fonts_t;

const char *ink_wifi_setup_keyboard_layer_name(ink_wifi_setup_keyboard_layer_t layer);

int ink_wifi_setup_ui_wifi_list_visible_first(const wifi_setup_state_t *wifi);
bool ink_wifi_setup_ui_wifi_list_row_region(
    const wifi_setup_state_t *wifi,
    int index,
    ink_wifi_setup_ui_region_t *region);
bool ink_wifi_setup_ui_keyboard_key_region(
    int column,
    int row,
    ink_wifi_setup_ui_region_t *region);
void ink_wifi_setup_ui_full_screen_region(ink_wifi_setup_ui_region_t *region);
void ink_wifi_setup_ui_list_body_region(ink_wifi_setup_ui_region_t *region);
void ink_wifi_setup_ui_password_screen_region(ink_wifi_setup_ui_region_t *region);
void ink_wifi_setup_ui_password_box_region(ink_wifi_setup_ui_region_t *region);
void ink_wifi_setup_ui_saved_menu_region(ink_wifi_setup_ui_region_t *region);
void ink_wifi_setup_ui_keyboard_footer_region(ink_wifi_setup_ui_region_t *region);

void ink_wifi_setup_ui_draw_wifi_list_row(
    uint8_t *buffer,
    const wifi_setup_state_t *wifi,
    const ink_wifi_setup_ui_fonts_t *fonts,
    int index);
void ink_wifi_setup_ui_draw_screen(
    uint8_t *buffer,
    ink_wifi_setup_keyboard_layer_t layer,
    const ink_wifi_setup_ui_cursor_t *keyboard_state,
    const ink_wifi_setup_keyboard_text_t *text,
    const wifi_setup_state_t *wifi,
    const ink_wifi_setup_ui_fonts_t *fonts);
void ink_wifi_setup_ui_draw_keyboard_key(
    uint8_t *buffer,
    ink_wifi_setup_keyboard_layer_t layer,
    int column,
    int row,
    bool selected);

void ink_wifi_setup_ui_expand_region(
    ink_wifi_setup_ui_region_t *area,
    const ink_wifi_setup_ui_region_t *region);
bool ink_wifi_setup_ui_region_is_valid(const ink_wifi_setup_ui_region_t *region);
void ink_wifi_setup_ui_pad_align_region(ink_wifi_setup_ui_region_t *region, int pad);

bool ink_wifi_setup_ui_self_test(void);
