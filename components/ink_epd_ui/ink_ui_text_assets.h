#ifndef INK_UI_TEXT_ASSETS_H
#define INK_UI_TEXT_ASSETS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *text;
    uint8_t pixel_size;
    uint16_t width;
    uint16_t height;
    const uint8_t *bitmap;
} ink_ui_text_asset_t;

const ink_ui_text_asset_t *ink_ui_text_asset_find(const char *text, uint8_t pixel_size);

#ifdef __cplusplus
}
#endif

#endif
