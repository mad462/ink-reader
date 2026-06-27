#include "tilt_grid_input.h"

#include <stddef.h>

enum {
    TILT_GRID_COLS = 12,
    TILT_GRID_ROWS = 5,
    TILT_GRID_CENTER_COL = 5,
    TILT_GRID_CENTER_ROW = 2,
    TILT_GRID_ENTER_THRESHOLD = 4200,
    TILT_GRID_EXIT_THRESHOLD = 2200,
    TILT_GRID_REPEAT_SAMPLES = 0,
};

static int abs_int(int value)
{
    return value < 0 ? -value : value;
}

void tilt_grid_input_init(tilt_grid_input_t *state)
{
    if (state == NULL) {
        return;
    }
    state->column = TILT_GRID_CENTER_COL;
    state->row = TILT_GRID_CENTER_ROW;
    state->filtered_x = 0;
    state->filtered_y = 0;
    state->repeat_countdown = 0;
    state->active_direction = TILT_GRID_DIRECTION_NONE;
}

static int wrap_grid_coord(int value, int size)
{
    if (value < 0) {
        return size - 1;
    }
    if (value >= size) {
        return 0;
    }
    return value;
}

static bool tilt_grid_direction_delta(tilt_grid_direction_t direction, int *dx, int *dy)
{
    if (dx == NULL || dy == NULL) {
        return false;
    }
    *dx = 0;
    *dy = 0;

    switch (direction) {
        case TILT_GRID_DIRECTION_LEFT:
            *dx = -1;
            return true;
        case TILT_GRID_DIRECTION_RIGHT:
            *dx = 1;
            return true;
        case TILT_GRID_DIRECTION_UP:
            *dy = -1;
            return true;
        case TILT_GRID_DIRECTION_DOWN:
            *dy = 1;
            return true;
        case TILT_GRID_DIRECTION_UP_LEFT:
            *dx = -1;
            *dy = -1;
            return true;
        case TILT_GRID_DIRECTION_UP_RIGHT:
            *dx = 1;
            *dy = -1;
            return true;
        case TILT_GRID_DIRECTION_DOWN_LEFT:
            *dx = -1;
            *dy = 1;
            return true;
        case TILT_GRID_DIRECTION_DOWN_RIGHT:
            *dx = 1;
            *dy = 1;
            return true;
        case TILT_GRID_DIRECTION_NONE:
        default:
            return false;
    }
}

static bool tilt_grid_move(tilt_grid_input_t *state, tilt_grid_direction_t direction)
{
    int dx = 0;
    int dy = 0;

    if (state == NULL || !tilt_grid_direction_delta(direction, &dx, &dy)) {
        return false;
    }

    state->column = wrap_grid_coord(state->column + dx, TILT_GRID_COLS);
    state->row = wrap_grid_coord(state->row + dy, TILT_GRID_ROWS);
    return true;
}

tilt_grid_direction_t tilt_grid_input_update(tilt_grid_input_t *state, int accel_x, int accel_y)
{
    if (state == NULL) {
        return TILT_GRID_DIRECTION_NONE;
    }

    state->filtered_x = ((state->filtered_x * 3) + accel_x) / 4;
    state->filtered_y = ((state->filtered_y * 3) + accel_y) / 4;

    const int abs_x = abs_int(accel_x);
    const int abs_y = abs_int(accel_y);
    tilt_grid_direction_t direction = TILT_GRID_DIRECTION_NONE;

    if (abs_x < TILT_GRID_EXIT_THRESHOLD && abs_y < TILT_GRID_EXIT_THRESHOLD) {
        state->active_direction = TILT_GRID_DIRECTION_NONE;
        state->repeat_countdown = 0;
        return TILT_GRID_DIRECTION_NONE;
    }

    if (abs_x >= TILT_GRID_ENTER_THRESHOLD || abs_y >= TILT_GRID_ENTER_THRESHOLD) {
        const bool move_x = abs_x >= TILT_GRID_ENTER_THRESHOLD;
        const bool move_y = abs_y >= TILT_GRID_ENTER_THRESHOLD;
        const bool right = accel_x < 0;
        const bool down = accel_y < 0;

        if (move_x && move_y) {
            if (right && down) {
                direction = TILT_GRID_DIRECTION_DOWN_RIGHT;
            } else if (right) {
                direction = TILT_GRID_DIRECTION_UP_RIGHT;
            } else if (down) {
                direction = TILT_GRID_DIRECTION_DOWN_LEFT;
            } else {
                direction = TILT_GRID_DIRECTION_UP_LEFT;
            }
        } else if (move_x) {
            direction = right ? TILT_GRID_DIRECTION_RIGHT : TILT_GRID_DIRECTION_LEFT;
        } else {
            direction = down ? TILT_GRID_DIRECTION_DOWN : TILT_GRID_DIRECTION_UP;
        }
    }

    if (direction == TILT_GRID_DIRECTION_NONE) {
        return TILT_GRID_DIRECTION_NONE;
    }

    if (direction != state->active_direction) {
        state->active_direction = direction;
        state->repeat_countdown = 0;
    } else if (state->repeat_countdown > 0) {
        --state->repeat_countdown;
        return TILT_GRID_DIRECTION_NONE;
    }

    state->repeat_countdown = TILT_GRID_REPEAT_SAMPLES;
    return tilt_grid_move(state, direction) ? direction : TILT_GRID_DIRECTION_NONE;
}

const char *tilt_grid_direction_name(tilt_grid_direction_t direction)
{
    switch (direction) {
        case TILT_GRID_DIRECTION_LEFT:
            return "left";
        case TILT_GRID_DIRECTION_RIGHT:
            return "right";
        case TILT_GRID_DIRECTION_UP:
            return "up";
        case TILT_GRID_DIRECTION_DOWN:
            return "down";
        case TILT_GRID_DIRECTION_UP_LEFT:
            return "up_left";
        case TILT_GRID_DIRECTION_UP_RIGHT:
            return "up_right";
        case TILT_GRID_DIRECTION_DOWN_LEFT:
            return "down_left";
        case TILT_GRID_DIRECTION_DOWN_RIGHT:
            return "down_right";
        case TILT_GRID_DIRECTION_NONE:
        default:
            return "none";
    }
}
