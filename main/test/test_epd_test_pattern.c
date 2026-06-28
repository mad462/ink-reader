#include "unity.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "epd_gdey0426t82.h"
#include "epd_test_pattern.h"

void test_epd_pattern_generates_non_uniform_buffer(void)
{
    uint8_t buffer[32];

    memset(buffer, 0x00, sizeof(buffer));
    epd_test_pattern_fill_stripes(buffer, sizeof(buffer));

    TEST_ASSERT_NOT_EQUAL_HEX8(0x00, buffer[0]);
    TEST_ASSERT_NOT_EQUAL_HEX8(buffer[0], buffer[1]);
}

void test_epd_ui_truncate_keeps_short_text(void)
{
    char out[32];

    epd_test_pattern_truncate_text_middle("Photos", out, sizeof(out), 12);

    TEST_ASSERT_EQUAL_STRING("Photos", out);
}

void test_epd_ui_truncate_adds_ellipsis_for_long_text(void)
{
    char out[24];

    epd_test_pattern_truncate_text_middle(
        "crop_480x800_Eink-4Gray_atkinson_serpentine_indexed4_1782481618665",
        out,
        sizeof(out),
        18);

    TEST_ASSERT_NOT_EQUAL(0, strstr(out, "...") != NULL);
}

void test_epd_ui_truncate_preserves_tail_text(void)
{
    char out[24];

    epd_test_pattern_truncate_text_middle(
        "crop_480x800_Eink-4Gray_atkinson_serpentine_indexed4_1782481618665",
        out,
        sizeof(out),
        18);

    TEST_ASSERT_NOT_NULL(strstr(out, "..."));
    TEST_ASSERT_EQUAL_CHAR('5', out[strlen(out) - 1]);
    TEST_ASSERT_NOT_EQUAL(0, strcmp(out, "crop_480x800_Ein...") != 0);
}

void test_epd_ui_truncate_utf8_keeps_codepoints_intact(void)
{
    char out[32];

    epd_test_pattern_truncate_text_middle(
        "图书馆书架上的相册封面预览页面",
        out,
        sizeof(out),
        8);

    TEST_ASSERT_NOT_NULL(strstr(out, "..."));
    TEST_ASSERT_EQUAL(0, memcmp(out, "\xE5\x9B\xBE", 3));
    TEST_ASSERT_EQUAL(0, strcmp(out + strlen(out) - 3, "\xE9\x9D\xA2"));
    TEST_ASSERT_NULL(strstr(out, "\x80..."));
}

void test_epd_ui_truncate_utf8_stays_valid_in_tight_buffer(void)
{
    char out[12];

    epd_test_pattern_truncate_text_middle(
        "图书馆书架上的相册封面预览页面",
        out,
        sizeof(out),
        8);

    TEST_ASSERT_NOT_NULL(strstr(out, "..."));
    TEST_ASSERT_EQUAL(0, memcmp(out, "\xE5\x9B\xBE", 3));
    TEST_ASSERT_NULL(strstr(out, "\xE5..."));
    TEST_ASSERT_NULL(strstr(out, "\xA6..."));
}

void test_epd_ui_truncate_returns_empty_string_for_one_byte_buffer(void)
{
    char out[1] = {'X'};

    epd_test_pattern_truncate_text_middle("图书馆书架上的相册封面预览页面", out, sizeof(out), 8);

    TEST_ASSERT_EQUAL_CHAR('\0', out[0]);
}

void test_epd_ui_truncate_tiny_ascii_buffer_stays_terminated(void)
{
    char out[3] = {'X', 'Y', 'Z'};

    epd_test_pattern_truncate_text_middle("Photos", out, sizeof(out), 2);

    TEST_ASSERT_EQUAL_CHAR('\0', out[2]);
}

void test_epd_ui_list_geometry_has_dense_rows(void)
{
    epd_test_pattern_list_layout_t layout = epd_test_pattern_crosspoint_list_layout();

    TEST_ASSERT_GREATER_THAN_INT(6, layout.visible_rows);
    TEST_ASSERT_LESS_THAN_INT(64, layout.row_h);
}

void test_epd_ui_list_row_helper_draws_into_scratch_buffer(void)
{
    size_t buffer_size = EPD_GDEY0426T82_BUFFER_SIZE;
    uint8_t *buffer = (uint8_t *)malloc(buffer_size);
    epd_test_pattern_list_row_t row = {
        .title = "Very long row title that should be clipped by the helper itself",
        .line1 = "Metadata line one that also needs clipping",
        .line2 = "Metadata line two that also needs clipping",
        .selected = true,
        .emphasized = true,
    };
    size_t black_pixels = 0U;

    TEST_ASSERT_NOT_NULL(buffer);
    memset(buffer, 0xFF, buffer_size);

    epd_test_pattern_draw_crosspoint_list_row(buffer, NULL, 0U, &row, NULL, NULL);

    for (size_t i = 0; i < buffer_size; ++i) {
        if (buffer[i] != 0xFFU) {
            ++black_pixels;
        }
    }

    TEST_ASSERT_GREATER_THAN_UINT(0U, (unsigned int)black_pixels);

    free(buffer);
}
