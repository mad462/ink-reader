#pragma once

#include <stdbool.h>

#include "apps/ink_app_iface.h"
#include "ink_cpfont.h"

typedef enum {
    INK_USB_MSC_APP_VIEW_PROMPT = 0,
    INK_USB_MSC_APP_VIEW_ACTIVE,
    INK_USB_MSC_APP_VIEW_ERROR,
} ink_usb_msc_app_view_t;

typedef struct {
    bool initialized;
    ink_usb_msc_app_view_t view;
    char title[32];
    char line1[64];
    char line2[64];
    char line3[64];
} ink_usb_msc_app_state_t;

typedef struct {
    const ink_usb_msc_app_state_t *state;
    const ink_cpfont_t *menu_font;
    const ink_cpfont_t *footer_font;
    char header_meta[24];
} ink_usb_msc_app_render_state_t;

const ink_app_descriptor_t *ink_usb_msc_app_descriptor(void);
bool ink_usb_msc_app_self_test(void);
