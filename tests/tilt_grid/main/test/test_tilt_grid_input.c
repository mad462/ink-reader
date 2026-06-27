#include "tilt_grid_input.h"

#include "unity.h"

void test_deadzone_keeps_center_cell(void)
{
    tilt_grid_input_t state;
    tilt_grid_input_init(&state);

    TEST_ASSERT_EQUAL(TILT_GRID_DIRECTION_NONE, tilt_grid_input_update(&state, 4000, 4000));
    TEST_ASSERT_EQUAL(5, state.column);
    TEST_ASSERT_EQUAL(2, state.row);
}

void test_x_axis_is_inverted_so_negative_x_moves_right(void)
{
    tilt_grid_input_t state;
    tilt_grid_input_init(&state);

    TEST_ASSERT_EQUAL(TILT_GRID_DIRECTION_RIGHT, tilt_grid_input_update(&state, -18000, 0));
    TEST_ASSERT_EQUAL(6, state.column);
}

void test_positive_x_moves_left(void)
{
    tilt_grid_input_t state;
    tilt_grid_input_init(&state);

    TEST_ASSERT_EQUAL(TILT_GRID_DIRECTION_LEFT, tilt_grid_input_update(&state, 18000, 0));
    TEST_ASSERT_EQUAL(4, state.column);
}

void test_sustained_tilt_repeats_after_cooldown_without_return_to_neutral(void)
{
    tilt_grid_input_t state;
    tilt_grid_input_init(&state);

    TEST_ASSERT_EQUAL(TILT_GRID_DIRECTION_RIGHT, tilt_grid_input_update(&state, -18000, 0));
    TEST_ASSERT_EQUAL(6, state.column);

    TEST_ASSERT_EQUAL(TILT_GRID_DIRECTION_RIGHT, tilt_grid_input_update(&state, -18000, 0));
    TEST_ASSERT_EQUAL(7, state.column);
}

void test_about_15_degree_tilt_crosses_threshold(void)
{
    tilt_grid_input_t state;
    tilt_grid_input_init(&state);

    TEST_ASSERT_EQUAL(TILT_GRID_DIRECTION_RIGHT, tilt_grid_input_update(&state, -17000, 0));
    TEST_ASSERT_EQUAL(6, state.column);
}

void test_slight_tilt_below_15_degree_deadzone_does_not_move(void)
{
    tilt_grid_input_t state;
    tilt_grid_input_init(&state);

    for (int i = 0; i < 4; ++i) {
        TEST_ASSERT_EQUAL(TILT_GRID_DIRECTION_NONE, tilt_grid_input_update(&state, -4000, 0));
    }
    TEST_ASSERT_EQUAL(5, state.column);
}

void test_returning_level_after_strong_tilt_does_not_continue_previous_direction(void)
{
    tilt_grid_input_t state;
    tilt_grid_input_init(&state);

    TEST_ASSERT_EQUAL(TILT_GRID_DIRECTION_RIGHT, tilt_grid_input_update(&state, -18000, 0));
    const int moved_column = state.column;

    TEST_ASSERT_EQUAL(TILT_GRID_DIRECTION_NONE, tilt_grid_input_update(&state, 0, 0));
    TEST_ASSERT_EQUAL(moved_column, state.column);
}

void test_y_axis_moves_rows(void)
{
    tilt_grid_input_t state;
    tilt_grid_input_init(&state);

    TEST_ASSERT_EQUAL(TILT_GRID_DIRECTION_UP, tilt_grid_input_update(&state, 0, 18000));
    TEST_ASSERT_EQUAL(1, state.row);

    tilt_grid_input_init(&state);
    TEST_ASSERT_EQUAL(TILT_GRID_DIRECTION_DOWN, tilt_grid_input_update(&state, 0, -18000));
    TEST_ASSERT_EQUAL(3, state.row);
}

void test_diagonal_tilt_moves_column_and_row_together(void)
{
    tilt_grid_input_t state;
    tilt_grid_input_init(&state);

    TEST_ASSERT_EQUAL(TILT_GRID_DIRECTION_UP_RIGHT, tilt_grid_input_update(&state, -18000, 18000));
    TEST_ASSERT_EQUAL(6, state.column);
    TEST_ASSERT_EQUAL(1, state.row);
}

void test_wraps_horizontally_at_grid_edges(void)
{
    tilt_grid_input_t state;
    tilt_grid_input_init(&state);

    for (int i = 0; i < 7; ++i) {
        (void)tilt_grid_input_update(&state, -18000, 0);
    }
    TEST_ASSERT_EQUAL(0, state.column);

    (void)tilt_grid_input_update(&state, 18000, 0);
    TEST_ASSERT_EQUAL(11, state.column);
}

void test_wraps_vertically_at_grid_edges(void)
{
    tilt_grid_input_t state;
    tilt_grid_input_init(&state);

    for (int i = 0; i < 3; ++i) {
        (void)tilt_grid_input_update(&state, 0, 18000);
    }
    TEST_ASSERT_EQUAL(4, state.row);

    (void)tilt_grid_input_update(&state, 0, -18000);
    TEST_ASSERT_EQUAL(0, state.row);
}

void test_diagonal_wraps_both_axes(void)
{
    tilt_grid_input_t state;
    tilt_grid_input_init(&state);

    for (int i = 0; i < 7; ++i) {
        (void)tilt_grid_input_update(&state, -18000, 18000);
    }
    TEST_ASSERT_EQUAL(0, state.column);
    TEST_ASSERT_EQUAL(0, state.row);
}
