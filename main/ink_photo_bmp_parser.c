#include "ink_photo_bmp_parser.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "epd_gdey0426t82.h"

typedef struct {
    uint32_t width;
    int32_t height;
    uint16_t bit_count;
    uint32_t compression;
    uint32_t image_offset;
    uint32_t colors_used;
    bool bottom_up;
} ink_photo_bmp_header_t;

static uint16_t read_u16_le(const uint8_t *bytes);
static uint32_t read_u32_le(const uint8_t *bytes);
static int32_t read_i32_le(const uint8_t *bytes);
static bool validate_headers(
    const uint8_t *file_header,
    const uint8_t *dib_header,
    ink_photo_bmp_header_t *header,
    char *error_text,
    size_t error_text_size);
static void set_error(char *error_text, size_t error_text_size, const char *text);
static bool classify_palette(
    const uint8_t *palette,
    uint32_t colors_used,
    uint8_t *white_index,
    uint8_t *light_index,
    uint8_t *dark_index,
    uint8_t *black_index);
static void write_gray_pixel(uint8_t *lsb, uint8_t *msb, uint16_t x, uint16_t y, uint8_t gray2);
static uint8_t gray_code_from_palette_index(
    uint8_t palette_index,
    uint8_t white_index,
    uint8_t light_index,
    uint8_t dark_index,
    uint8_t black_index);
static bool bmp_header_validation_self_test(void);
static bool bmp_gray_write_self_test(void);
static bool bmp_palette_classify_self_test(void);
static bool bmp_export_contract_gray_index_self_test(void);
static bool bmp_aligns_with_xth_gray_semantics_self_test(void);

static void set_error(char *error_text, size_t error_text_size, const char *text)
{
    if (error_text == NULL || error_text_size == 0U) {
        return;
    }

    snprintf(error_text, error_text_size, "%s", text != NULL ? text : "");
}

static uint16_t read_u16_le(const uint8_t *bytes)
{
    return (uint16_t)(bytes[0] | ((uint16_t)bytes[1] << 8));
}

static uint32_t read_u32_le(const uint8_t *bytes)
{
    return (uint32_t)bytes[0]
        | ((uint32_t)bytes[1] << 8)
        | ((uint32_t)bytes[2] << 16)
        | ((uint32_t)bytes[3] << 24);
}

static int32_t read_i32_le(const uint8_t *bytes)
{
    return (int32_t)read_u32_le(bytes);
}

static bool validate_headers(
    const uint8_t *file_header,
    const uint8_t *dib_header,
    ink_photo_bmp_header_t *header,
    char *error_text,
    size_t error_text_size)
{
    if (file_header == NULL || dib_header == NULL || header == NULL) {
        set_error(error_text, error_text_size, "header invalid");
        return false;
    }

    memset(header, 0, sizeof(*header));
    if (file_header[0] != 'B' || file_header[1] != 'M') {
        set_error(error_text, error_text_size, "not bmp");
        return false;
    }

    header->image_offset = read_u32_le(file_header + 10);
    header->width = read_u32_le(dib_header + 4);
    header->height = read_i32_le(dib_header + 8);
    header->bit_count = read_u16_le(dib_header + 14);
    header->compression = read_u32_le(dib_header + 16);
    header->colors_used = read_u32_le(dib_header + 32);
    if (header->colors_used == 0U) {
        header->colors_used = 16U;
    }

    if (header->compression != 0U) {
        set_error(error_text, error_text_size, "bmp compressed");
        return false;
    }
    if (header->bit_count != 4U) {
        set_error(error_text, error_text_size, "bmp not 4bpp");
        return false;
    }
    if (header->width != EPD_GDEY0426T82_WIDTH) {
        set_error(error_text, error_text_size, "bmp width invalid");
        return false;
    }
    if (header->height == 0) {
        set_error(error_text, error_text_size, "bmp height invalid");
        return false;
    }
    if (header->height < 0) {
        set_error(error_text, error_text_size, "bmp top-down unsupported");
        return false;
    }
    if ((uint32_t)header->height != EPD_GDEY0426T82_HEIGHT) {
        set_error(error_text, error_text_size, "bmp height invalid");
        return false;
    }
    if (header->colors_used < 4U) {
        set_error(error_text, error_text_size, "bmp palette invalid");
        return false;
    }

    header->bottom_up = true;
    return true;
}

static bool classify_palette(
    const uint8_t *palette,
    uint32_t colors_used,
    uint8_t *white_index,
    uint8_t *light_index,
    uint8_t *dark_index,
    uint8_t *black_index)
{
    uint8_t ranked[16];
    uint16_t luminance[16];
    uint32_t limit = colors_used > 16U ? 16U : colors_used;

    if (palette == NULL
        || white_index == NULL
        || light_index == NULL
        || dark_index == NULL
        || black_index == NULL
        || limit < 4U) {
        return false;
    }

    for (uint32_t i = 0; i < limit; ++i) {
        const uint8_t blue = palette[i * 4U + 0U];
        const uint8_t green = palette[i * 4U + 1U];
        const uint8_t red = palette[i * 4U + 2U];
        ranked[i] = (uint8_t)i;
        luminance[i] = (uint16_t)red * 30U + (uint16_t)green * 59U + (uint16_t)blue * 11U;
    }

    for (uint32_t i = 0; i + 1U < limit; ++i) {
        for (uint32_t j = i + 1U; j < limit; ++j) {
            if (luminance[ranked[i]] > luminance[ranked[j]]) {
                const uint8_t tmp = ranked[i];
                ranked[i] = ranked[j];
                ranked[j] = tmp;
            }
        }
    }

    *black_index = ranked[0];
    *dark_index = ranked[1];
    *light_index = ranked[limit - 2U];
    *white_index = ranked[limit - 1U];
    return true;
}

static void write_gray_pixel(uint8_t *lsb, uint8_t *msb, uint16_t x, uint16_t y, uint8_t gray2)
{
    const size_t stride = EPD_GDEY0426T82_WIDTH / 8U;
    const size_t index = (size_t)y * stride + (x / 8U);
    const uint8_t mask = (uint8_t)(0x80U >> (x % 8U));
    const bool lsb_bit = (gray2 & 0x01U) != 0U;
    const bool msb_bit = (gray2 & 0x02U) != 0U;

    if (lsb_bit) {
        lsb[index] |= mask;
    } else {
        lsb[index] &= (uint8_t)~mask;
    }
    if (msb_bit) {
        msb[index] |= mask;
    } else {
        msb[index] &= (uint8_t)~mask;
    }
}

static uint8_t gray_code_from_palette_index(
    uint8_t palette_index,
    uint8_t white_index,
    uint8_t light_index,
    uint8_t dark_index,
    uint8_t black_index)
{
    if (palette_index == white_index) {
        return 0U;
    }
    if (palette_index == light_index) {
        return 1U;
    }
    if (palette_index == dark_index) {
        return 2U;
    }
    if (palette_index == black_index) {
        return 3U;
    }
    return 0U;
}

bool ink_photo_parse_4gray_bmp_to_planes(
    const char *path,
    uint8_t *lsb_plane,
    size_t lsb_size,
    uint8_t *msb_plane,
    size_t msb_size,
    char *error_text,
    size_t error_text_size)
{
    FILE *fp = NULL;
    uint8_t file_header[14];
    uint8_t dib_header[40];
    uint8_t palette[64];
    ink_photo_bmp_header_t header;
    uint8_t white_index = 0U;
    uint8_t light_index = 0U;
    uint8_t dark_index = 0U;
    uint8_t black_index = 0U;
    uint8_t *row = NULL;
    size_t row_stride = 0U;
    bool ok = false;

    if (path == NULL
        || lsb_plane == NULL
        || msb_plane == NULL
        || lsb_size < EPD_GDEY0426T82_GRAY_PLANE_SIZE
        || msb_size < EPD_GDEY0426T82_GRAY_PLANE_SIZE) {
        set_error(error_text, error_text_size, "plane invalid");
        return false;
    }

    memset(lsb_plane, 0x00, EPD_GDEY0426T82_GRAY_PLANE_SIZE);
    memset(msb_plane, 0x00, EPD_GDEY0426T82_GRAY_PLANE_SIZE);

    fp = fopen(path, "rb");
    if (fp == NULL) {
        set_error(error_text, error_text_size, "图片读取失败");
        return false;
    }
    if (fread(file_header, 1U, sizeof(file_header), fp) != sizeof(file_header)
        || fread(dib_header, 1U, sizeof(dib_header), fp) != sizeof(dib_header)) {
        set_error(error_text, error_text_size, "bmp header read fail");
        goto done;
    }
    if (!validate_headers(file_header, dib_header, &header, error_text, error_text_size)) {
        goto done;
    }
    if (fread(palette, 1U, sizeof(palette), fp) < 16U) {
        set_error(error_text, error_text_size, "bmp palette read fail");
        goto done;
    }
    if (!classify_palette(
            palette,
            header.colors_used,
            &white_index,
            &light_index,
            &dark_index,
            &black_index)) {
        set_error(error_text, error_text_size, "bmp palette invalid");
        goto done;
    }

    row_stride = ((header.width + 1U) / 2U + 3U) & ~3U;
    row = (uint8_t *)malloc(row_stride);
    if (row == NULL) {
        set_error(error_text, error_text_size, "bmp no mem");
        goto done;
    }
    if (fseek(fp, (long)header.image_offset, SEEK_SET) != 0) {
        set_error(error_text, error_text_size, "bmp seek fail");
        goto done;
    }

    for (uint32_t src_row = 0U; src_row < (uint32_t)header.height; ++src_row) {
        const uint16_t dst_y = (uint16_t)(header.height - 1 - (int32_t)src_row);
        if (fread(row, 1U, row_stride, fp) != row_stride) {
            set_error(error_text, error_text_size, "bmp row read fail");
            goto done;
        }
        for (uint16_t x = 0U; x < EPD_GDEY0426T82_WIDTH; x += 2U) {
            const uint8_t packed = row[x / 2U];
            const uint8_t indices[2] = {
                (uint8_t)((packed >> 4) & 0x0FU),
                (uint8_t)(packed & 0x0FU),
            };
            for (uint16_t pixel = 0U; pixel < 2U; ++pixel) {
                const uint8_t gray = gray_code_from_palette_index(
                    indices[pixel],
                    white_index,
                    light_index,
                    dark_index,
                    black_index);
                write_gray_pixel(lsb_plane, msb_plane, (uint16_t)(x + pixel), dst_y, gray);
            }
        }
    }

    ok = true;
done:
    if (row != NULL) {
        free(row);
    }
    if (fp != NULL) {
        fclose(fp);
    }
    return ok;
}

bool ink_photo_bmp_file_looks_supported(const char *path)
{
    FILE *fp = NULL;
    uint8_t file_header[14];
    uint8_t dib_header[40];
    uint8_t palette[64];
    ink_photo_bmp_header_t header;
    uint8_t white_index = 0U;
    uint8_t light_index = 0U;
    uint8_t dark_index = 0U;
    uint8_t black_index = 0U;
    bool ok = false;

    if (path == NULL || path[0] == '\0') {
        return false;
    }

    fp = fopen(path, "rb");
    if (fp == NULL) {
        return false;
    }
    if (fread(file_header, 1U, sizeof(file_header), fp) != sizeof(file_header)
        || fread(dib_header, 1U, sizeof(dib_header), fp) != sizeof(dib_header)) {
        goto done;
    }
    if (!validate_headers(file_header, dib_header, &header, NULL, 0U)) {
        goto done;
    }
    if (fread(palette, 1U, sizeof(palette), fp) < 16U) {
        goto done;
    }
    ok = classify_palette(
        palette,
        header.colors_used,
        &white_index,
        &light_index,
        &dark_index,
        &black_index);

done:
    fclose(fp);
    return ok;
}

static bool bmp_header_validation_self_test(void)
{
    uint8_t file_header[14] = {0};
    uint8_t dib_header[40] = {0};
    ink_photo_bmp_header_t header;
    char error_text[32];

    file_header[0] = 'B';
    file_header[1] = 'M';
    file_header[10] = 54U;
    dib_header[0] = 40U;
    dib_header[4] = (uint8_t)(EPD_GDEY0426T82_WIDTH & 0xFFU);
    dib_header[5] = (uint8_t)(EPD_GDEY0426T82_WIDTH >> 8);
    dib_header[8] = (uint8_t)(EPD_GDEY0426T82_HEIGHT & 0xFFU);
    dib_header[9] = (uint8_t)(EPD_GDEY0426T82_HEIGHT >> 8);
    dib_header[12] = 1U;
    dib_header[14] = 4U;
    dib_header[32] = 4U;

    if (!validate_headers(file_header, dib_header, &header, error_text, sizeof(error_text))) {
        return false;
    }

    dib_header[14] = 8U;
    return !validate_headers(file_header, dib_header, &header, error_text, sizeof(error_text));
}

static bool bmp_gray_write_self_test(void)
{
    uint8_t lsb[EPD_GDEY0426T82_WIDTH / 8U];
    uint8_t msb[EPD_GDEY0426T82_WIDTH / 8U];

    memset(lsb, 0x00, sizeof(lsb));
    memset(msb, 0x00, sizeof(msb));

    write_gray_pixel(lsb, msb, 0U, 0U, 0U);
    write_gray_pixel(lsb, msb, 1U, 0U, 1U);
    write_gray_pixel(lsb, msb, 2U, 0U, 2U);
    write_gray_pixel(lsb, msb, 3U, 0U, 3U);

    return (lsb[0] & 0x80U) == 0U
        && (msb[0] & 0x80U) == 0U
        && (lsb[0] & 0x40U) != 0U
        && (msb[0] & 0x40U) == 0U
        && (lsb[0] & 0x20U) == 0U
        && (msb[0] & 0x20U) != 0U
        && (lsb[0] & 0x10U) != 0U
        && (msb[0] & 0x10U) != 0U;
}

static bool bmp_palette_classify_self_test(void)
{
    uint8_t palette[16] = {
        0x00, 0x00, 0x00, 0x00,
        0x55, 0x55, 0x55, 0x00,
        0xAA, 0xAA, 0xAA, 0x00,
        0xFF, 0xFF, 0xFF, 0x00,
    };
    uint8_t white_index = 0U;
    uint8_t light_index = 0U;
    uint8_t dark_index = 0U;
    uint8_t black_index = 0U;

    if (!classify_palette(
            palette,
            4U,
            &white_index,
            &light_index,
            &dark_index,
            &black_index)) {
        return false;
    }

    return white_index == 3U
        && light_index == 2U
        && dark_index == 1U
        && black_index == 0U;
}

static bool bmp_export_contract_gray_index_self_test(void)
{
    uint8_t lsb[EPD_GDEY0426T82_WIDTH / 8U];
    uint8_t msb[EPD_GDEY0426T82_WIDTH / 8U];

    memset(lsb, 0x00, sizeof(lsb));
    memset(msb, 0x00, sizeof(msb));

    /* QuickShakePic palette contract:
     * 0 = white, 1 = light, 2 = dark, 3 = black
     *
     * Photo album preview should align with the project's existing
     * Verified panel semantics:
     * white -> 00
     * light -> 01
     * dark  -> 10
     * black -> 11
     */
    write_gray_pixel(lsb, msb, 0U, 0U, 0U); /* white plane code */
    write_gray_pixel(lsb, msb, 1U, 0U, 1U); /* light plane code */
    write_gray_pixel(lsb, msb, 2U, 0U, 2U); /* dark plane code */
    write_gray_pixel(lsb, msb, 3U, 0U, 3U); /* black plane code */

    return (lsb[0] & 0x80U) == 0U
        && (msb[0] & 0x80U) == 0U
        && (lsb[0] & 0x40U) != 0U
        && (msb[0] & 0x40U) == 0U
        && (lsb[0] & 0x20U) == 0U
        && (msb[0] & 0x20U) != 0U
        && (lsb[0] & 0x10U) != 0U
        && (msb[0] & 0x10U) != 0U;
}

static bool bmp_aligns_with_xth_gray_semantics_self_test(void)
{
    uint8_t lsb[EPD_GDEY0426T82_WIDTH / 8U];
    uint8_t msb[EPD_GDEY0426T82_WIDTH / 8U];

    memset(lsb, 0x00, sizeof(lsb));
    memset(msb, 0x00, sizeof(msb));

    write_gray_pixel(lsb, msb, 0U, 0U, 0U);
    write_gray_pixel(lsb, msb, 1U, 0U, 1U);
    write_gray_pixel(lsb, msb, 2U, 0U, 2U);
    write_gray_pixel(lsb, msb, 3U, 0U, 3U);

    return (lsb[0] & 0x80U) == 0U
        && (msb[0] & 0x80U) == 0U
        && (lsb[0] & 0x40U) != 0U
        && (msb[0] & 0x40U) == 0U
        && (lsb[0] & 0x20U) == 0U
        && (msb[0] & 0x20U) != 0U
        && (lsb[0] & 0x10U) != 0U
        && (msb[0] & 0x10U) != 0U;
}

bool ink_photo_bmp_parser_self_test(void)
{
    return bmp_header_validation_self_test()
        && bmp_gray_write_self_test()
        && bmp_palette_classify_self_test()
        && bmp_export_contract_gray_index_self_test()
        && bmp_aligns_with_xth_gray_semantics_self_test();
}
