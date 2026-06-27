#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    TILT_GRID_DIRECTION_NONE = 0,
    TILT_GRID_DIRECTION_LEFT,
    TILT_GRID_DIRECTION_RIGHT,
    TILT_GRID_DIRECTION_UP,
    TILT_GRID_DIRECTION_DOWN,
    TILT_GRID_DIRECTION_UP_LEFT,
    TILT_GRID_DIRECTION_UP_RIGHT,
    TILT_GRID_DIRECTION_DOWN_LEFT,
    TILT_GRID_DIRECTION_DOWN_RIGHT,
} tilt_grid_direction_t;

typedef struct {
    int column;
    int row;
    int filtered_x;
    int filtered_y;
    int repeat_countdown;
    tilt_grid_direction_t active_direction;
} tilt_grid_input_t;

void tilt_grid_input_init(tilt_grid_input_t *state);
tilt_grid_direction_t tilt_grid_input_update(tilt_grid_input_t *state, int accel_x, int accel_y);
const char *tilt_grid_direction_name(tilt_grid_direction_t direction);
