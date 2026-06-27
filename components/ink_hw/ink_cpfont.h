#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "esp_err.h"

#define INK_CPFONT_PATH_LENGTH 255

typedef struct {
    uint32_t first;
    uint32_t last;
    uint32_t offset;
} ink_cpfont_interval_t;

typedef struct {
    uint8_t width;
    uint8_t height;
    uint16_t advance_x;
    int16_t left;
    int16_t top;
    uint16_t data_length;
    uint32_t data_offset;
} ink_cpfont_glyph_t;

typedef struct {
    uint32_t hits;
    uint32_t misses;
    uint32_t evictions;
} ink_cpfont_cache_stats_t;

typedef struct {
    bool loaded;
    bool is_2bit;
    char path[INK_CPFONT_PATH_LENGTH + 1];
    FILE *file;
    ink_cpfont_interval_t *intervals;
    uint32_t interval_count;
    uint32_t glyph_count;
    uint8_t advance_y;
    int16_t ascender;
    int16_t descender;
    uint32_t glyphs_file_offset;
    uint32_t bitmap_file_offset;
    uint8_t *bitmap_scratch;
    size_t bitmap_scratch_size;
    uint32_t last_glyph_index;
    ink_cpfont_glyph_t last_glyph;
    bool last_glyph_valid;
    void *glyph_cache_entries;
    uint32_t glyph_cache_capacity;
    uint32_t glyph_cache_used;
    uint32_t glyph_cache_tick;
    ink_cpfont_cache_stats_t cache_stats;
} ink_cpfont_t;

void ink_cpfont_init(ink_cpfont_t *font);
void ink_cpfont_close(ink_cpfont_t *font);
esp_err_t ink_cpfont_load(ink_cpfont_t *font, const char *path);
bool ink_cpfont_is_loaded(const ink_cpfont_t *font);
void ink_cpfont_cache_snapshot(const ink_cpfont_t *font, ink_cpfont_cache_stats_t *out_stats);
esp_err_t ink_cpfont_draw_text_bw(
    ink_cpfont_t *font,
    uint8_t *buffer,
    int x,
    int top_y,
    const char *text,
    int *out_width_px
);
esp_err_t ink_cpfont_draw_text_bw_scaled(
    ink_cpfont_t *font,
    uint8_t *buffer,
    int x,
    int top_y,
    const char *text,
    uint8_t scale_divisor,
    int *out_width_px
);
esp_err_t ink_cpfont_draw_text_bw_inverted(
    ink_cpfont_t *font,
    uint8_t *buffer,
    int x,
    int top_y,
    const char *text,
    int *out_width_px
);
esp_err_t ink_cpfont_draw_text_bw_scaled_inverted(
    ink_cpfont_t *font,
    uint8_t *buffer,
    int x,
    int top_y,
    const char *text,
    uint8_t scale_divisor,
    int *out_width_px
);
bool ink_cpfont_self_test(void);
