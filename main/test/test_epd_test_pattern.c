#include "unity.h"

#include <stdint.h>
#include <string.h>

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

void test_epd_ui_list_geometry_has_dense_rows(void)
{
    epd_test_pattern_list_layout_t layout = epd_test_pattern_crosspoint_list_layout();

    TEST_ASSERT_GREATER_THAN_INT(6, layout.visible_rows);
    TEST_ASSERT_LESS_THAN_INT(64, layout.row_h);
}
