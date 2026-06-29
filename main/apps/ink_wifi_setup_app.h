#pragma once

#include "ink_cpfont.h"
#include "ink_wifi_setup_ui.h"
#include "ink_app_iface.h"
#include "ink_wifi_setup_input.h"
#include "ink_wifi_setup_state.h"

typedef struct {
    wifi_setup_state_t wifi;
    ink_wifi_setup_keyboard_text_t keyboard_text;
    ink_wifi_setup_keyboard_layer_t keyboard_layer;
    int keyboard_column;
    int keyboard_row;
} ink_wifi_setup_app_view_t;

typedef struct {
    ink_wifi_setup_app_view_t *view;
    ink_wifi_setup_ui_fonts_t fonts;
    char header_meta[24];
} ink_wifi_setup_app_render_state_t;

const ink_app_descriptor_t *ink_wifi_setup_app_descriptor(void);
bool ink_wifi_setup_app_should_accept_tilt(
    const struct ink_system_runtime *runtime,
    const ink_app_descriptor_t *app);
bool ink_wifi_setup_app_self_test(void);
