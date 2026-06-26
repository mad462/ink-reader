#include "ink_wifi_setup_input.h"

#include <stdio.h>
#include <string.h>

static const char *const s_keyboard_layers[INK_WIFI_SETUP_KEYBOARD_LAYER_COUNT]
                                           [INK_WIFI_SETUP_KEYBOARD_ROWS]
                                           [INK_WIFI_SETUP_KEYBOARD_COLS] = {
    [INK_WIFI_SETUP_KEYBOARD_LAYER_LOWER] = {
        {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-", "+"},
        {"q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "[", "]"},
        {"a", "s", "d", "f", "g", "h", "j", "k", "l", ";", "'"},
        {"z", "x", "c", "v", "b", "n", "m", ",", ".", "/"},
        {"del", "sp", "ok"},
    },
    [INK_WIFI_SETUP_KEYBOARD_LAYER_UPPER] = {
        {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-", "+"},
        {"Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "{", "}"},
        {"A", "S", "D", "F", "G", "H", "J", "K", "L", ":", "\""},
        {"Z", "X", "C", "V", "B", "N", "M", "<", ">", "?"},
        {"del", "sp", "ok"},
    },
    [INK_WIFI_SETUP_KEYBOARD_LAYER_SYMBOL] = {
        {"!", "@", "#", "$", "%", "^", "&", "*", "(", ")", "_", "="},
        {"~", "`", "|", "\\", "/", "[", "]", "{", "}", "<", ">", "?"},
        {":", ";", "'", "\"", ",", ".", "+", "-", "=", "abc", "ABC"},
        {"del", "clr", "sym", "q", "w", "e", "r", "a", "s", "d"},
        {"del", "sp", "ok"},
    },
};

static const uint8_t s_keyboard_row_key_count[INK_WIFI_SETUP_KEYBOARD_ROWS] = {12, 12, 11, 10, 3};

enum {
    INK_WIFI_SETUP_DIRECTION_NONE = 0,
    INK_WIFI_SETUP_DIRECTION_LEFT,
    INK_WIFI_SETUP_DIRECTION_RIGHT,
    INK_WIFI_SETUP_DIRECTION_UP,
    INK_WIFI_SETUP_DIRECTION_DOWN,
    INK_WIFI_SETUP_DIRECTION_UP_LEFT,
    INK_WIFI_SETUP_DIRECTION_UP_RIGHT,
    INK_WIFI_SETUP_DIRECTION_DOWN_LEFT,
    INK_WIFI_SETUP_DIRECTION_DOWN_RIGHT,
};

static bool keyboard_direction_delta(int direction, int *dx, int *dy)
{
    if (dx == NULL || dy == NULL) {
        return false;
    }
    *dx = 0;
    *dy = 0;

    switch (direction) {
        case INK_WIFI_SETUP_DIRECTION_LEFT:
            *dx = -1;
            return true;
        case INK_WIFI_SETUP_DIRECTION_RIGHT:
            *dx = 1;
            return true;
        case INK_WIFI_SETUP_DIRECTION_UP:
            *dy = -1;
            return true;
        case INK_WIFI_SETUP_DIRECTION_DOWN:
            *dy = 1;
            return true;
        case INK_WIFI_SETUP_DIRECTION_UP_LEFT:
            *dx = -1;
            *dy = -1;
            return true;
        case INK_WIFI_SETUP_DIRECTION_UP_RIGHT:
            *dx = 1;
            *dy = -1;
            return true;
        case INK_WIFI_SETUP_DIRECTION_DOWN_LEFT:
            *dx = -1;
            *dy = 1;
            return true;
        case INK_WIFI_SETUP_DIRECTION_DOWN_RIGHT:
            *dx = 1;
            *dy = 1;
            return true;
        case INK_WIFI_SETUP_DIRECTION_NONE:
        default:
            return false;
    }
}

const char *ink_wifi_setup_keyboard_label(
    ink_wifi_setup_keyboard_layer_t layer,
    int column,
    int row)
{
    if (layer < 0 || layer >= INK_WIFI_SETUP_KEYBOARD_LAYER_COUNT) {
        return "";
    }
    if (row < 0 || row >= INK_WIFI_SETUP_KEYBOARD_ROWS) {
        return "";
    }
    if (column < 0 || column >= s_keyboard_row_key_count[row]) {
        return "";
    }
    return s_keyboard_layers[layer][row][column];
}

int ink_wifi_setup_keyboard_row_key_count(int row)
{
    if (row < 0 || row >= INK_WIFI_SETUP_KEYBOARD_ROWS) {
        return 0;
    }
    return s_keyboard_row_key_count[row];
}

void ink_wifi_setup_normalize_selection(
    int *column,
    int *row,
    int direction,
    int old_row)
{
    if (column == NULL || row == NULL) {
        return;
    }
    if (*row < 0) {
        *row = INK_WIFI_SETUP_KEYBOARD_ROWS - 1;
    } else if (*row >= INK_WIFI_SETUP_KEYBOARD_ROWS) {
        *row = 0;
    }

    const int key_count = s_keyboard_row_key_count[*row];
    int dx = 0;
    int dy = 0;
    (void)keyboard_direction_delta(direction, &dx, &dy);

    if (*column < 0) {
        *column = key_count - 1;
    } else if (*column >= key_count && old_row == *row && dx > 0 && dy == 0) {
        *column = 0;
    } else if (*column >= key_count) {
        *column = key_count - 1;
    }
}

void ink_wifi_setup_keyboard_text_append(
    ink_wifi_setup_keyboard_text_t *text,
    const char *value)
{
    if (text == NULL || value == NULL) {
        return;
    }

    while (*value != '\0' && text->len < INK_WIFI_SETUP_PASSWORD_TEXT_MAX) {
        text->text[text->len++] = *value++;
    }
    text->text[text->len] = '\0';
}

void ink_wifi_setup_keyboard_text_backspace(ink_wifi_setup_keyboard_text_t *text)
{
    if (text == NULL || text->len == 0) {
        return;
    }
    text->text[--text->len] = '\0';
}

void ink_wifi_setup_keyboard_text_clear(ink_wifi_setup_keyboard_text_t *text)
{
    if (text == NULL) {
        return;
    }
    text->len = 0;
    text->text[0] = '\0';
}

bool ink_wifi_setup_keyboard_activate_label(
    ink_wifi_setup_keyboard_text_t *text,
    ink_wifi_setup_keyboard_layer_t *layer,
    const char *label)
{
    if (text == NULL || layer == NULL || label == NULL || label[0] == '\0') {
        return false;
    }
    if (strcmp(label, "sp") == 0) {
        ink_wifi_setup_keyboard_text_append(text, " ");
    } else if (strcmp(label, "del") == 0) {
        ink_wifi_setup_keyboard_text_backspace(text);
    } else if (strcmp(label, "clr") == 0) {
        ink_wifi_setup_keyboard_text_clear(text);
    } else if (strcmp(label, "abc") == 0) {
        *layer = INK_WIFI_SETUP_KEYBOARD_LAYER_LOWER;
    } else if (strcmp(label, "ABC") == 0) {
        *layer = INK_WIFI_SETUP_KEYBOARD_LAYER_UPPER;
    } else if (strcmp(label, "sym") == 0) {
        *layer = INK_WIFI_SETUP_KEYBOARD_LAYER_SYMBOL;
    } else if (strcmp(label, "ok") == 0) {
        return true;
    } else {
        ink_wifi_setup_keyboard_text_append(text, label);
    }
    return true;
}

bool ink_wifi_setup_prepare_scan_request(ink_wifi_setup_request_t *request)
{
    if (request == NULL) {
        return false;
    }

    memset(request, 0, sizeof(*request));
    request->type = INK_WIFI_SETUP_REQUEST_SCAN;
    return true;
}

bool ink_wifi_setup_prepare_selected_connect_request(
    const ink_wifi_setup_keyboard_text_t *text,
    const wifi_setup_state_t *wifi,
    ink_wifi_setup_request_t *request)
{
    const ink_wifi_scan_result_t *ap = ink_wifi_setup_selected_ap(wifi);

    if (text == NULL || wifi == NULL || ap == NULL || request == NULL) {
        return false;
    }

    memset(request, 0, sizeof(*request));
    request->type = INK_WIFI_SETUP_REQUEST_CONNECT_PASSWORD;
    snprintf(request->ssid, sizeof(request->ssid), "%s", ap->ssid);
    snprintf(request->password, sizeof(request->password), "%s", text->text);
    return true;
}

bool ink_wifi_setup_prepare_selected_saved_request(
    const wifi_setup_state_t *wifi,
    ink_wifi_setup_request_type_t type,
    ink_wifi_setup_request_t *request)
{
    const ink_wifi_scan_result_t *ap = ink_wifi_setup_selected_ap(wifi);

    if (wifi == NULL || ap == NULL || !ap->saved || request == NULL) {
        return false;
    }

    memset(request, 0, sizeof(*request));
    request->type = type;
    snprintf(request->ssid, sizeof(request->ssid), "%s", ap->ssid);
    return true;
}
