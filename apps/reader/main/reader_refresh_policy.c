#include "reader_refresh_policy.h"

void reader_refresh_state_init(reader_refresh_state_t *state) {
  if (state) state->screen_ready = false;
}

reader_refresh_mode_t reader_refresh_state_choose(
    const reader_refresh_state_t *state, reader_refresh_mode_t preferred) {
  return !state || !state->screen_ready ? READER_REFRESH_FULL : preferred;
}

void reader_refresh_state_record(reader_refresh_state_t *state, bool success) {
  if (state) state->screen_ready = success;
}
