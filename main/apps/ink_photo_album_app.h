#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ink_app_iface.h"
#include "ink_photo_catalog.h"

typedef enum {
    INK_PHOTO_ALBUM_VIEW_PREVIEW = 0,
    INK_PHOTO_ALBUM_VIEW_LIST,
} ink_photo_album_view_mode_t;

typedef struct {
    bool allocated;
    bool valid;
    bool load_failed;
    size_t index;
    char path[256];
    uint8_t *lsb_plane;
    uint8_t *msb_plane;
} ink_photo_album_cache_slot_t;

typedef struct {
    bool initialized;
    bool catalog_ready;
    bool image_loaded;
    bool load_failed;
    bool tf_unavailable;
    bool partial_refresh_pending;
    bool preview_interrupt_refresh_pending;
    size_t current_index;
    size_t list_selected_index;
    size_t total_count;
    size_t current_slot;
    int partial_x;
    int partial_y;
    int partial_w;
    int partial_h;
    ink_photo_album_view_mode_t view_mode;
    char current_name[64];
    char current_path[256];
    char status_text[64];
    uint8_t *lsb_plane;
    uint8_t *msb_plane;
    size_t plane_size;
    ink_photo_album_cache_slot_t cache_slots[3];
} ink_photo_album_app_state_t;

typedef struct {
    const ink_photo_album_app_state_t *state;
    const ink_photo_catalog_t *catalog;
    const void *menu_font;
    const void *footer_font;
} ink_photo_album_render_state_t;

const ink_app_descriptor_t *ink_photo_album_app_descriptor(void);
bool ink_photo_album_app_self_test(void);
