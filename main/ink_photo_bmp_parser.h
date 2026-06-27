#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t white_index;
    uint8_t light_index;
    uint8_t dark_index;
    uint8_t black_index;
} ink_photo_bmp_palette_map_t;

bool ink_photo_parse_4gray_bmp_to_planes(
    const char *path,
    uint8_t *lsb_plane,
    size_t lsb_size,
    uint8_t *msb_plane,
    size_t msb_size,
    char *error_text,
    size_t error_text_size);
bool ink_photo_bmp_file_looks_supported(const char *path);
bool ink_photo_bmp_parser_self_test(void);
