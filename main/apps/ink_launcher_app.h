#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "ink_app_iface.h"
#include "ink_cpfont.h"

typedef struct {
    size_t selected_app_index;
} ink_launcher_app_state_t;

typedef struct {
    const ink_launcher_app_state_t *state;
    const ink_cpfont_t *menu_font;
    const ink_cpfont_t *footer_font;
    char header_meta[24];
} ink_launcher_app_render_state_t;

const ink_app_descriptor_t *ink_launcher_app_descriptor(void);
bool ink_launcher_app_self_test(void);
