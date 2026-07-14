#pragma once

#include <stdbool.h>

typedef enum {
  READER_REFRESH_PARTIAL = 0,
  READER_REFRESH_FULL,
} reader_refresh_mode_t;

typedef struct {
  bool screen_ready;
} reader_refresh_state_t;

void reader_refresh_state_init(reader_refresh_state_t *state);
reader_refresh_mode_t reader_refresh_state_choose(
    const reader_refresh_state_t *state, reader_refresh_mode_t preferred);
void reader_refresh_state_record(reader_refresh_state_t *state, bool success);
