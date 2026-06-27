#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ink_wifi_setup_state.h"

enum {
    INK_WIFI_SETUP_KEYBOARD_COLS = 12,
    INK_WIFI_SETUP_KEYBOARD_ROWS = 5,
    INK_WIFI_SETUP_PASSWORD_TEXT_MAX = 64,
};

typedef enum {
    INK_WIFI_SETUP_KEYBOARD_LAYER_LOWER = 0,
    INK_WIFI_SETUP_KEYBOARD_LAYER_UPPER,
    INK_WIFI_SETUP_KEYBOARD_LAYER_SYMBOL,
    INK_WIFI_SETUP_KEYBOARD_LAYER_COUNT,
} ink_wifi_setup_keyboard_layer_t;

typedef struct {
    char text[INK_WIFI_SETUP_PASSWORD_TEXT_MAX + 1];
    size_t len;
} ink_wifi_setup_keyboard_text_t;

typedef enum {
    INK_WIFI_SETUP_REQUEST_SCAN = 0,
    INK_WIFI_SETUP_REQUEST_CONNECT_PASSWORD,
    INK_WIFI_SETUP_REQUEST_CONNECT_SAVED,
    INK_WIFI_SETUP_REQUEST_DELETE_SAVED,
} ink_wifi_setup_request_type_t;

typedef struct {
    ink_wifi_setup_request_type_t type;
    char ssid[INK_WIFI_SSID_MAX_LEN + 1];
    char password[INK_WIFI_PASSWORD_MAX_LEN + 1];
} ink_wifi_setup_request_t;

const char *ink_wifi_setup_keyboard_label(
    ink_wifi_setup_keyboard_layer_t layer,
    int column,
    int row);
int ink_wifi_setup_keyboard_row_key_count(int row);
void ink_wifi_setup_normalize_selection(
    int *column,
    int *row,
    int direction,
    int old_row);
void ink_wifi_setup_keyboard_text_append(
    ink_wifi_setup_keyboard_text_t *text,
    const char *value);
void ink_wifi_setup_keyboard_text_backspace(ink_wifi_setup_keyboard_text_t *text);
void ink_wifi_setup_keyboard_text_clear(ink_wifi_setup_keyboard_text_t *text);
bool ink_wifi_setup_keyboard_activate_label(
    ink_wifi_setup_keyboard_text_t *text,
    ink_wifi_setup_keyboard_layer_t *layer,
    const char *label);
bool ink_wifi_setup_prepare_scan_request(ink_wifi_setup_request_t *request);
bool ink_wifi_setup_prepare_selected_connect_request(
    const ink_wifi_setup_keyboard_text_t *text,
    const wifi_setup_state_t *wifi,
    ink_wifi_setup_request_t *request);
bool ink_wifi_setup_prepare_selected_saved_request(
    const wifi_setup_state_t *wifi,
    ink_wifi_setup_request_type_t type,
    ink_wifi_setup_request_t *request);
bool ink_wifi_setup_input_self_test(void);
