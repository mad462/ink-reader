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
