#include "ink_cpfont.h"

#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"

#include "epd_gdey0426t82.h"

enum {
    INK_CPFONT_MAX_BITMAP_SCRATCH = 8192,
    INK_CPFONT_GLYPH_CACHE_CAPACITY = 96,
};

static const char *TAG = "ink_cpfont";
static const char kCpfontMagic[8] = { 'C', 'P', 'F', 'O', 'N', 'T', '\0', '\0' };
static const uint16_t kCpfontVersion = 4;

typedef struct {
    bool valid;
    uint32_t glyph_index;
    uint32_t last_used_tick;
    ink_cpfont_glyph_t glyph;
    uint8_t *bitmap;
    uint16_t bitmap_length;
    bool bitmap_is_1bit;
} ink_cpfont_cache_entry_t;

static uint16_t glyph_1bit_size_bytes(const ink_cpfont_glyph_t *glyph);
static void decode_glyph_2bit_to_1bit(const ink_cpfont_glyph_t *glyph, const uint8_t *src_bitmap, uint8_t *dst_bitmap);
static void draw_glyph_1bit_scaled(
    uint8_t *buffer,
    int x,
    int baseline_y,
    const ink_cpfont_glyph_t *glyph,
    const uint8_t *bitmap,
    uint8_t scale_divisor);

static uint16_t read_u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static int16_t read_i16(const uint8_t *p)
{
    return (int16_t)read_u16(p);
}

static uint32_t read_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t utf8_next_codepoint(const char **text)
{
    const unsigned char *p = (const unsigned char *)*text;
    uint32_t cp = 0;

    if (p == NULL || *p == 0) {
        return 0;
    }

    if (*p < 0x80U) {
        cp = *p++;
    } else if ((*p & 0xE0U) == 0xC0U) {
        cp = (uint32_t)(*p++ & 0x1FU) << 6;
        cp |= (uint32_t)(*p++ & 0x3FU);
    } else if ((*p & 0xF0U) == 0xE0U) {
        cp = (uint32_t)(*p++ & 0x0FU) << 12;
        cp |= (uint32_t)(*p++ & 0x3FU) << 6;
        cp |= (uint32_t)(*p++ & 0x3FU);
    } else if ((*p & 0xF8U) == 0xF0U) {
        cp = (uint32_t)(*p++ & 0x07U) << 18;
        cp |= (uint32_t)(*p++ & 0x3FU) << 12;
        cp |= (uint32_t)(*p++ & 0x3FU) << 6;
        cp |= (uint32_t)(*p++ & 0x3FU);
    } else {
        ++p;
        cp = '?';
    }

    *text = (const char *)p;
    return cp;
}

static inline void set_pixel_unchecked(uint8_t *buffer, int x, int y)
{
    const size_t index = (size_t)y * (EPD_GDEY0426T82_WIDTH / 8) + (size_t)(x / 8);
    buffer[index] &= (uint8_t)~(uint8_t)(0x80U >> (x % 8));
}

static bool interval_contains(const ink_cpfont_interval_t *intervals, uint32_t count, uint32_t cp, uint32_t *glyph_index)
{
    int left = 0;
    int right = (int)count - 1;

    while (left <= right) {
        const int mid = left + (right - left) / 2;
        const ink_cpfont_interval_t *interval = &intervals[mid];
        if (cp < interval->first) {
            right = mid - 1;
            continue;
        }
        if (cp > interval->last) {
            left = mid + 1;
            continue;
        }

        if (glyph_index != NULL) {
            *glyph_index = interval->offset + (cp - interval->first);
        }
        return true;
    }

    return false;
}

static bool read_glyph(ink_cpfont_t *font, uint32_t glyph_index, ink_cpfont_glyph_t *glyph)
{
    if (font->last_glyph_valid && font->last_glyph_index == glyph_index) {
        *glyph = font->last_glyph;
        return true;
    }

    const uint32_t offset = font->glyphs_file_offset + glyph_index * 16U;
    uint8_t buf[16];

    if (fseek(font->file, (long)offset, SEEK_SET) != 0) {
        return false;
    }
    if (fread(buf, 1, sizeof(buf), font->file) != sizeof(buf)) {
        return false;
    }

    glyph->width = buf[0];
    glyph->height = buf[1];
    glyph->advance_x = read_u16(buf + 2);
    glyph->left = read_i16(buf + 4);
    glyph->top = read_i16(buf + 6);
    glyph->data_length = read_u16(buf + 8);
    glyph->data_offset = read_u32(buf + 12);
    font->last_glyph_index = glyph_index;
    font->last_glyph = *glyph;
    font->last_glyph_valid = true;
    return true;
}

static ink_cpfont_cache_entry_t *cache_entries(ink_cpfont_t *font)
{
    return (ink_cpfont_cache_entry_t *)font->glyph_cache_entries;
}

static ink_cpfont_cache_entry_t *cache_find_entry(ink_cpfont_t *font, uint32_t glyph_index)
{
    ink_cpfont_cache_entry_t *entries = cache_entries(font);
    if (entries == NULL) {
        return NULL;
    }

    for (uint32_t i = 0; i < font->glyph_cache_capacity; ++i) {
        if (entries[i].valid && entries[i].glyph_index == glyph_index) {
            entries[i].last_used_tick = ++font->glyph_cache_tick;
            ++font->cache_stats.hits;
            font->last_glyph_index = glyph_index;
            font->last_glyph = entries[i].glyph;
            font->last_glyph_valid = true;
            return &entries[i];
        }
    }
    return NULL;
}

static ink_cpfont_cache_entry_t *cache_allocate_entry(ink_cpfont_t *font)
{
    ink_cpfont_cache_entry_t *entries = cache_entries(font);
    if (entries == NULL) {
        return NULL;
    }

    for (uint32_t i = 0; i < font->glyph_cache_capacity; ++i) {
        if (!entries[i].valid) {
            entries[i].valid = true;
            entries[i].last_used_tick = ++font->glyph_cache_tick;
            if (font->glyph_cache_used < font->glyph_cache_capacity) {
                ++font->glyph_cache_used;
            }
            return &entries[i];
        }
    }

    ink_cpfont_cache_entry_t *lru = &entries[0];
    for (uint32_t i = 1; i < font->glyph_cache_capacity; ++i) {
        if (entries[i].last_used_tick < lru->last_used_tick) {
            lru = &entries[i];
        }
    }

    ++font->cache_stats.evictions;
    lru->last_used_tick = ++font->glyph_cache_tick;
    return lru;
}

static bool read_bitmap(ink_cpfont_t *font, const ink_cpfont_glyph_t *glyph, uint8_t **bitmap_out)
{
    if (glyph->data_length == 0) {
        *bitmap_out = NULL;
        return true;
    }
    if (glyph->data_length > font->bitmap_scratch_size) {
        return false;
    }

    if (fseek(font->file, (long)(font->bitmap_file_offset + glyph->data_offset), SEEK_SET) != 0) {
        return false;
    }
    if (fread(font->bitmap_scratch, 1, glyph->data_length, font->file) != glyph->data_length) {
        return false;
    }

    *bitmap_out = font->bitmap_scratch;
    return true;
}

static bool load_glyph_cached(
    ink_cpfont_t *font,
    uint32_t glyph_index,
    ink_cpfont_glyph_t *glyph,
    const uint8_t **bitmap_out,
    bool *bitmap_is_1bit_out)
{
    if (font == NULL || glyph == NULL || bitmap_out == NULL || bitmap_is_1bit_out == NULL) {
        return false;
    }

    ink_cpfont_cache_entry_t *cached = cache_find_entry(font, glyph_index);
    if (cached != NULL) {
        *glyph = cached->glyph;
        *bitmap_out = cached->bitmap;
        *bitmap_is_1bit_out = cached->bitmap_is_1bit;
        return true;
    }

    ++font->cache_stats.misses;
    ink_cpfont_glyph_t loaded_glyph;
    uint8_t *scratch_bitmap = NULL;
    if (!read_glyph(font, glyph_index, &loaded_glyph) || !read_bitmap(font, &loaded_glyph, &scratch_bitmap)) {
        return false;
    }

    ink_cpfont_cache_entry_t *slot = cache_allocate_entry(font);
    if (slot == NULL) {
        *glyph = loaded_glyph;
        *bitmap_out = scratch_bitmap;
        *bitmap_is_1bit_out = false;
        return true;
    }

    if (loaded_glyph.data_length == 0) {
        free(slot->bitmap);
        slot->bitmap = NULL;
        slot->bitmap_length = 0;
        slot->bitmap_is_1bit = true;
    } else {
        const uint16_t bitmap_length = glyph_1bit_size_bytes(&loaded_glyph);
        uint8_t *bitmap_copy = realloc(slot->bitmap, bitmap_length);
        if (bitmap_copy == NULL) {
            ESP_LOGW(TAG, "glyph cache realloc failed glyph=%u bytes=%u", (unsigned)glyph_index, (unsigned)bitmap_length);
            *glyph = loaded_glyph;
            *bitmap_out = scratch_bitmap;
            *bitmap_is_1bit_out = false;
            return true;
        }
        slot->bitmap = bitmap_copy;
        slot->bitmap_length = bitmap_length;
        slot->bitmap_is_1bit = true;
        decode_glyph_2bit_to_1bit(&loaded_glyph, scratch_bitmap, slot->bitmap);
    }

    slot->glyph_index = glyph_index;
    slot->glyph = loaded_glyph;
    *glyph = slot->glyph;
    *bitmap_out = slot->bitmap;
    *bitmap_is_1bit_out = slot->bitmap_is_1bit;
    return true;
}

static uint16_t glyph_1bit_size_bytes(const ink_cpfont_glyph_t *glyph)
{
    if (glyph == NULL || glyph->width == 0 || glyph->height == 0) {
        return 0;
    }
    return (uint16_t)(((uint32_t)glyph->width * (uint32_t)glyph->height + 7U) >> 3);
}

static void decode_glyph_2bit_to_1bit(const ink_cpfont_glyph_t *glyph, const uint8_t *src_bitmap, uint8_t *dst_bitmap)
{
    if (glyph == NULL || src_bitmap == NULL || dst_bitmap == NULL) {
        return;
    }

    const uint32_t pixel_count = (uint32_t)glyph->width * (uint32_t)glyph->height;
    memset(dst_bitmap, 0, glyph_1bit_size_bytes(glyph));
    for (uint32_t pixel = 0; pixel < pixel_count; ++pixel) {
        const uint8_t packed = src_bitmap[pixel >> 2];
        const uint8_t shift = (uint8_t)((3U - (pixel & 3U)) * 2U);
        const uint8_t raw = (packed >> shift) & 0x3U;
        if (raw > 0U) {
            dst_bitmap[pixel >> 3] |= (uint8_t)(0x80U >> (pixel & 7U));
        }
    }
}

static void draw_glyph_2bit(uint8_t *buffer, int x, int baseline_y, const ink_cpfont_glyph_t *glyph, const uint8_t *bitmap)
{
    int pixel_pos = 0;
    const int base_x = x + glyph->left;
    const int base_y = baseline_y - glyph->top;

    for (int gy = 0; gy < glyph->height; ++gy) {
        for (int gx = 0; gx < glyph->width; ++gx, ++pixel_pos) {
            const uint8_t byte = bitmap[pixel_pos >> 2];
            const uint8_t shift = (uint8_t)((3 - (pixel_pos & 3)) * 2);
            const uint8_t raw = (byte >> shift) & 0x3U;
            if (raw > 0) {
                const int px = base_x + gx;
                const int py = base_y + gy;
                if (px >= 0 && px < EPD_GDEY0426T82_WIDTH && py >= 0 && py < EPD_GDEY0426T82_HEIGHT) {
                    set_pixel_unchecked(buffer, px, py);
                }
            }
        }
    }
}

static void draw_glyph_1bit(uint8_t *buffer, int x, int baseline_y, const ink_cpfont_glyph_t *glyph, const uint8_t *bitmap)
{
    const int base_x = x + glyph->left;
    const int base_y = baseline_y - glyph->top;
    const uint32_t pixel_count = (uint32_t)glyph->width * (uint32_t)glyph->height;

    for (uint32_t pixel = 0; pixel < pixel_count; ++pixel) {
        if ((bitmap[pixel >> 3] & (uint8_t)(0x80U >> (pixel & 7U))) == 0U) {
            continue;
        }

        const int gx = (int)(pixel % glyph->width);
        const int gy = (int)(pixel / glyph->width);
        const int px = base_x + gx;
        const int py = base_y + gy;
        if (px >= 0 && px < EPD_GDEY0426T82_WIDTH && py >= 0 && py < EPD_GDEY0426T82_HEIGHT) {
            set_pixel_unchecked(buffer, px, py);
        }
    }
}

static void draw_glyph_1bit_scaled(
    uint8_t *buffer,
    int x,
    int baseline_y,
    const ink_cpfont_glyph_t *glyph,
    const uint8_t *bitmap,
    uint8_t scale_divisor)
{
    if (scale_divisor <= 1U) {
        draw_glyph_1bit(buffer, x, baseline_y, glyph, bitmap);
        return;
    }

    const uint32_t pixel_count = (uint32_t)glyph->width * (uint32_t)glyph->height;
    const int scaled_left = glyph->left / (int16_t)scale_divisor;
    const int scaled_top = glyph->top / (int16_t)scale_divisor;
    const int base_x = x + scaled_left;
    const int base_y = baseline_y - scaled_top;

    for (uint32_t pixel = 0; pixel < pixel_count; ++pixel) {
        if ((bitmap[pixel >> 3] & (uint8_t)(0x80U >> (pixel & 7U))) == 0U) {
            continue;
        }

        const int gx = (int)(pixel % glyph->width);
        const int gy = (int)(pixel / glyph->width);
        if ((gx % scale_divisor) != 0 || (gy % scale_divisor) != 0) {
            continue;
        }

        const int px = base_x + (gx / scale_divisor);
        const int py = base_y + (gy / scale_divisor);
        if (px >= 0 && px < EPD_GDEY0426T82_WIDTH && py >= 0 && py < EPD_GDEY0426T82_HEIGHT) {
            set_pixel_unchecked(buffer, px, py);
        }
    }
}

void ink_cpfont_init(ink_cpfont_t *font)
{
    if (font == NULL) {
        return;
    }
    memset(font, 0, sizeof(*font));
}

void ink_cpfont_close(ink_cpfont_t *font)
{
    if (font == NULL) {
        return;
    }

    if (font->file != NULL) {
        fclose(font->file);
        font->file = NULL;
    }
    free(font->intervals);
    font->intervals = NULL;
    free(font->bitmap_scratch);
    font->bitmap_scratch = NULL;
    font->bitmap_scratch_size = 0;
    if (font->glyph_cache_entries != NULL) {
        ink_cpfont_cache_entry_t *entries = cache_entries(font);
        for (uint32_t i = 0; i < font->glyph_cache_capacity; ++i) {
            free(entries[i].bitmap);
            entries[i].bitmap = NULL;
        }
        free(font->glyph_cache_entries);
        font->glyph_cache_entries = NULL;
    }
    font->glyph_cache_capacity = 0;
    font->glyph_cache_used = 0;
    font->glyph_cache_tick = 0;
    memset(&font->cache_stats, 0, sizeof(font->cache_stats));
    font->last_glyph_valid = false;
    font->loaded = false;
}

bool ink_cpfont_is_loaded(const ink_cpfont_t *font)
{
    return font != NULL && font->loaded && font->file != NULL && font->intervals != NULL;
}

esp_err_t ink_cpfont_load(ink_cpfont_t *font, const char *path)
{
    uint8_t header[32];
    uint8_t toc[32];

    if (font == NULL || path == NULL || path[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    ink_cpfont_close(font);
    snprintf(font->path, sizeof(font->path), "%s", path);
    font->file = fopen(path, "rb");
    if (font->file == NULL) {
        ESP_LOGW(TAG, "font open failed: %s", path);
        return ESP_FAIL;
    }

    if (fread(header, 1, sizeof(header), font->file) != sizeof(header)) {
        ESP_LOGW(TAG, "font header read failed: %s", path);
        ink_cpfont_close(font);
        return ESP_FAIL;
    }
    if (memcmp(header, kCpfontMagic, sizeof(kCpfontMagic)) != 0) {
        ESP_LOGW(TAG, "font magic mismatch: %s", path);
        ink_cpfont_close(font);
        return ESP_FAIL;
    }
    const uint16_t version = read_u16(header + 8);
    if (version != kCpfontVersion) {
        ESP_LOGW(TAG, "font version mismatch: %s version=%u expected=%u", path, (unsigned)version, (unsigned)kCpfontVersion);
        ink_cpfont_close(font);
        return ESP_FAIL;
    }

    const uint8_t style_count = header[12];
    if (style_count == 0) {
        ESP_LOGW(TAG, "font has no styles: %s", path);
        ink_cpfont_close(font);
        return ESP_FAIL;
    }

    bool found_regular = false;
    uint32_t data_offset = 0;
    uint16_t kern_left_entries = 0;
    uint16_t kern_right_entries = 0;
    uint8_t kern_left_class_count = 0;
    uint8_t kern_right_class_count = 0;
    uint8_t ligature_count = 0;

    for (uint8_t i = 0; i < style_count; ++i) {
        if (fread(toc, 1, sizeof(toc), font->file) != sizeof(toc)) {
            ESP_LOGW(TAG, "font toc read failed: %s", path);
            ink_cpfont_close(font);
            return ESP_FAIL;
        }
        if (toc[0] != 0 && !found_regular) {
            continue;
        }

        found_regular = true;
        font->interval_count = read_u32(toc + 4);
        font->glyph_count = read_u32(toc + 8);
        font->advance_y = toc[12];
        font->ascender = read_i16(toc + 13);
        font->descender = read_i16(toc + 15);
        kern_left_entries = read_u16(toc + 17);
        kern_right_entries = read_u16(toc + 19);
        kern_left_class_count = toc[21];
        kern_right_class_count = toc[22];
        ligature_count = toc[23];
        data_offset = read_u32(toc + 24);
        break;
    }

    if (!found_regular || font->interval_count == 0 || font->glyph_count == 0) {
        ESP_LOGW(
            TAG,
            "font regular style missing or empty: %s styles=%u intervals=%u glyphs=%u",
            path,
            (unsigned)style_count,
            (unsigned)font->interval_count,
            (unsigned)font->glyph_count);
        ink_cpfont_close(font);
        return ESP_FAIL;
    }

    font->intervals = calloc(font->interval_count, sizeof(*font->intervals));
    if (font->intervals == NULL) {
        ESP_LOGW(TAG, "font interval allocation failed: %s intervals=%u", path, (unsigned)font->interval_count);
        ink_cpfont_close(font);
        return ESP_ERR_NO_MEM;
    }

    if (fseek(font->file, (long)data_offset, SEEK_SET) != 0) {
        ESP_LOGW(TAG, "font seek failed: %s offset=%u", path, (unsigned)data_offset);
        ink_cpfont_close(font);
        return ESP_FAIL;
    }

    for (uint32_t i = 0; i < font->interval_count; ++i) {
        uint8_t buf[12];
        if (fread(buf, 1, sizeof(buf), font->file) != sizeof(buf)) {
            ESP_LOGW(TAG, "font interval read failed: %s index=%u", path, (unsigned)i);
            ink_cpfont_close(font);
            return ESP_FAIL;
        }
        font->intervals[i].first = read_u32(buf);
        font->intervals[i].last = read_u32(buf + 4);
        font->intervals[i].offset = read_u32(buf + 8);
    }

    font->glyphs_file_offset = data_offset + font->interval_count * 12U;
    const uint32_t kern_left_file_offset = font->glyphs_file_offset + font->glyph_count * 16U;
    const uint32_t kern_right_file_offset = kern_left_file_offset + (uint32_t)kern_left_entries * 3U;
    const uint32_t kern_matrix_file_offset = kern_right_file_offset + (uint32_t)kern_right_entries * 3U;
    const uint32_t ligature_file_offset =
        kern_matrix_file_offset + (uint32_t)kern_left_class_count * (uint32_t)kern_right_class_count;
    font->bitmap_file_offset = ligature_file_offset + (uint32_t)ligature_count * 8U;
    font->bitmap_scratch_size = INK_CPFONT_MAX_BITMAP_SCRATCH;
    font->bitmap_scratch = malloc(font->bitmap_scratch_size);
    if (font->bitmap_scratch == NULL) {
        ESP_LOGW(TAG, "font bitmap scratch allocation failed: %s bytes=%u", path, (unsigned)font->bitmap_scratch_size);
        ink_cpfont_close(font);
        return ESP_ERR_NO_MEM;
    }
    font->glyph_cache_capacity = INK_CPFONT_GLYPH_CACHE_CAPACITY;
    font->glyph_cache_entries = calloc(font->glyph_cache_capacity, sizeof(ink_cpfont_cache_entry_t));
    if (font->glyph_cache_entries == NULL) {
        ESP_LOGW(
            TAG,
            "font glyph cache allocation failed: %s entries=%u",
            path,
            (unsigned)font->glyph_cache_capacity);
        ink_cpfont_close(font);
        return ESP_ERR_NO_MEM;
    }

    font->loaded = true;
    font->is_2bit = true;
    ESP_LOGI(
        TAG,
        "font loaded: %s intervals=%u glyphs=%u advance_y=%u ascender=%d descender=%d bitmap_offset=%u cache=%u",
        path,
        (unsigned)font->interval_count,
        (unsigned)font->glyph_count,
        (unsigned)font->advance_y,
        (int)font->ascender,
        (int)font->descender,
        (unsigned)font->bitmap_file_offset,
        (unsigned)font->glyph_cache_capacity);
    return ESP_OK;
}

void ink_cpfont_cache_snapshot(const ink_cpfont_t *font, ink_cpfont_cache_stats_t *out_stats)
{
    if (out_stats == NULL) {
        return;
    }

    if (font == NULL) {
        memset(out_stats, 0, sizeof(*out_stats));
        return;
    }

    *out_stats = font->cache_stats;
}

esp_err_t ink_cpfont_draw_text_bw(
    ink_cpfont_t *font,
    uint8_t *buffer,
    int x,
    int top_y,
    const char *text,
    int *out_width_px)
{
    int cursor_x = x;
    int measured_width = 0;

    if (out_width_px != NULL) {
        *out_width_px = 0;
    }
    if (font == NULL || text == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!ink_cpfont_is_loaded(font)) {
        return ESP_FAIL;
    }

    const int baseline_y = top_y + font->ascender;
    const char *cursor = text;
    while (*cursor != '\0') {
        const char *before = cursor;
        const uint32_t cp = utf8_next_codepoint(&cursor);
        if (cp == 0) {
            break;
        }
        if (cp == '\r' || cp == '\n') {
            break;
        }

        uint32_t glyph_index = 0;
        if (!interval_contains(font->intervals, font->interval_count, cp, &glyph_index)) {
            measured_width += font->advance_y / 2;
            cursor_x += font->advance_y / 2;
            continue;
        }

        ink_cpfont_glyph_t glyph;
        const uint8_t *bitmap = NULL;
        bool bitmap_is_1bit = false;
        if (!load_glyph_cached(font, glyph_index, &glyph, &bitmap, &bitmap_is_1bit)) {
            ESP_LOGW(TAG, "glyph read failed cp=U+%04X", (unsigned)cp);
            measured_width += font->advance_y / 2;
            cursor_x += font->advance_y / 2;
            continue;
        }

        if (bitmap != NULL && buffer != NULL) {
            if (bitmap_is_1bit) {
                draw_glyph_1bit(buffer, cursor_x, baseline_y, &glyph, bitmap);
            } else {
                draw_glyph_2bit(buffer, cursor_x, baseline_y, &glyph, bitmap);
            }
        }
        cursor_x += (int)((glyph.advance_x + 8U) >> 4);
        measured_width = cursor_x - x;

        if (before == cursor) {
            break;
        }
    }

    if (out_width_px != NULL) {
        *out_width_px = measured_width;
    }
    return ESP_OK;
}

esp_err_t ink_cpfont_draw_text_bw_scaled(
    ink_cpfont_t *font,
    uint8_t *buffer,
    int x,
    int top_y,
    const char *text,
    uint8_t scale_divisor,
    int *out_width_px)
{
    int cursor_x = x;
    int measured_width = 0;

    if (out_width_px != NULL) {
        *out_width_px = 0;
    }
    if (font == NULL || text == NULL || scale_divisor == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!ink_cpfont_is_loaded(font)) {
        return ESP_FAIL;
    }
    if (scale_divisor == 1U) {
        return ink_cpfont_draw_text_bw(font, buffer, x, top_y, text, out_width_px);
    }

    const int baseline_y = top_y + (font->ascender / (int16_t)scale_divisor);
    const char *cursor = text;
    while (*cursor != '\0') {
        const char *before = cursor;
        const uint32_t cp = utf8_next_codepoint(&cursor);
        if (cp == 0U || cp == '\r' || cp == '\n') {
            break;
        }

        uint32_t glyph_index = 0;
        if (!interval_contains(font->intervals, font->interval_count, cp, &glyph_index)) {
            const int fallback_advance = (font->advance_y / (int)scale_divisor) / 2;
            measured_width += fallback_advance;
            cursor_x += fallback_advance;
            continue;
        }

        ink_cpfont_glyph_t glyph;
        const uint8_t *bitmap = NULL;
        bool bitmap_is_1bit = false;
        if (!load_glyph_cached(font, glyph_index, &glyph, &bitmap, &bitmap_is_1bit)) {
            const int fallback_advance = (font->advance_y / (int)scale_divisor) / 2;
            ESP_LOGW(TAG, "glyph read failed cp=U+%04X", (unsigned)cp);
            measured_width += fallback_advance;
            cursor_x += fallback_advance;
            continue;
        }

        if (bitmap != NULL && buffer != NULL) {
            if (!bitmap_is_1bit) {
                ESP_LOGW(TAG, "scaled draw requires 1bit glyph cp=U+%04X", (unsigned)cp);
            }
            draw_glyph_1bit_scaled(buffer, cursor_x, baseline_y, &glyph, bitmap, scale_divisor);
        }
        cursor_x += (int)(((glyph.advance_x + 8U) >> 4) / scale_divisor);
        measured_width = cursor_x - x;

        if (before == cursor) {
            break;
        }
    }

    if (out_width_px != NULL) {
        *out_width_px = measured_width;
    }
    return ESP_OK;
}

bool ink_cpfont_self_test(void)
{
    ink_cpfont_t font;
    bool ok = false;
    ink_cpfont_cache_stats_t stats;
    ink_cpfont_cache_entry_t *slot0 = NULL;
    ink_cpfont_cache_entry_t *slot1 = NULL;
    ink_cpfont_cache_entry_t *slot2 = NULL;
    ink_cpfont_glyph_t glyph;
    const uint8_t *bitmap = NULL;
    bool bitmap_is_1bit = false;
    uint8_t render_buffer[(EPD_GDEY0426T82_WIDTH / 8) * 32];
    ink_cpfont_glyph_t render_glyph = {
        .width = 4,
        .height = 4,
        .advance_x = 64,
        .left = 0,
        .top = 4,
        .data_length = 2,
        .data_offset = 0,
    };
    const uint8_t render_bitmap[2] = {0x99, 0x99};
    int pixel_count = 0;

    ink_cpfont_init(&font);
    ok = !ink_cpfont_is_loaded(&font);
    if (!ok) {
        goto cleanup;
    }

    font.glyph_cache_capacity = 2;
    font.glyph_cache_entries = calloc(font.glyph_cache_capacity, sizeof(ink_cpfont_cache_entry_t));
    if (font.glyph_cache_entries == NULL) {
        ok = false;
        goto cleanup;
    }

    slot0 = cache_allocate_entry(&font);
    if (slot0 == NULL) {
        ok = false;
        goto cleanup;
    }
    slot0->glyph_index = 10;
    slot0->glyph.data_length = 4;
    slot0->bitmap = malloc(4);
    if (slot0->bitmap == NULL) {
        ok = false;
        goto cleanup;
    }
    memcpy(slot0->bitmap, "ABCD", 4);
    slot0->bitmap_length = 4;
    slot0->bitmap_is_1bit = true;

    slot1 = cache_allocate_entry(&font);
    if (slot1 == NULL || slot1 == slot0) {
        ok = false;
        goto cleanup;
    }
    slot1->glyph_index = 20;
    slot1->glyph.data_length = 4;
    slot1->bitmap = malloc(4);
    if (slot1->bitmap == NULL) {
        ok = false;
        goto cleanup;
    }
    memcpy(slot1->bitmap, "EFGH", 4);
    slot1->bitmap_length = 4;
    slot1->bitmap_is_1bit = true;

    if (cache_find_entry(&font, 10) != slot0) {
        ok = false;
        goto cleanup;
    }
    if (!load_glyph_cached(&font, 10, &glyph, &bitmap, &bitmap_is_1bit) || bitmap != slot0->bitmap || !bitmap_is_1bit) {
        ok = false;
        goto cleanup;
    }
    slot2 = cache_allocate_entry(&font);
    if (slot2 == NULL) {
        ok = false;
        goto cleanup;
    }
    slot2->glyph_index = 30;
    slot2->glyph.data_length = 4;
    slot2->bitmap_length = 4;
    slot2->bitmap_is_1bit = true;
    if (slot2->bitmap != NULL) {
        memcpy(slot2->bitmap, "IJKL", 4);
    }

    if (cache_find_entry(&font, 20) != NULL) {
        ok = false;
        goto cleanup;
    }
    if (cache_find_entry(&font, 10) != slot0) {
        ok = false;
        goto cleanup;
    }
    ++font.cache_stats.misses;
    ink_cpfont_cache_snapshot(&font, &stats);
    if (stats.hits < 2 || stats.misses < 1 || stats.evictions < 1) {
        ok = false;
        goto cleanup;
    }

    memset(render_buffer, 0xFF, sizeof(render_buffer));
    draw_glyph_1bit_scaled(render_buffer, 10, 10, &render_glyph, render_bitmap, 2);
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < EPD_GDEY0426T82_WIDTH; ++x) {
            const size_t index = (size_t)y * (EPD_GDEY0426T82_WIDTH / 8) + (size_t)(x / 8);
            const uint8_t mask = (uint8_t)(0x80U >> (x % 8));
            if ((render_buffer[index] & mask) == 0U) {
                ++pixel_count;
            }
        }
    }
    if (pixel_count != 4) {
        ok = false;
        goto cleanup;
    }

    ok = true;

cleanup:
    ink_cpfont_close(&font);
    return ok;
}
