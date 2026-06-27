#include "tilt_display_refresh.h"

#include "unity.h"

void test_stale_partial_refresh_is_dropped_without_full_refresh(void)
{
    TEST_ASSERT_FALSE(tilt_display_partial_error_should_full_refresh(ESP_ERR_INVALID_STATE));
}

void test_driver_or_argument_partial_refresh_error_falls_back_to_full_refresh(void)
{
    TEST_ASSERT_TRUE(tilt_display_partial_error_should_full_refresh(ESP_ERR_INVALID_ARG));
    TEST_ASSERT_TRUE(tilt_display_partial_error_should_full_refresh(ESP_FAIL));
}

void test_successful_partial_refresh_does_not_full_refresh(void)
{
    TEST_ASSERT_FALSE(tilt_display_partial_error_should_full_refresh(ESP_OK));
}
