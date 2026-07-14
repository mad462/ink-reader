#include "reader_hold_paging.h"

#include <string.h>

static reader_hold_action_t no_action(void) {
  return (reader_hold_action_t){0};
}

static reader_hold_action_t turn_action(reader_hold_direction_t direction,
                                        size_t page_step) {
  return (reader_hold_action_t){.kind = READER_HOLD_ACTION_TURN,
                                .direction = direction,
                                .page_step = page_step};
}

void reader_hold_paging_init(reader_hold_paging_t *state) {
  if (state) memset(state, 0, sizeof(*state));
}

uint32_t reader_hold_step_interval_ms(uint32_t held_ms) {
  if (held_ms < 5000U) return 360U;
  if (held_ms < 10000U) return 300U;
  if (held_ms < 20000U) return 240U;
  return 200U;
}

reader_hold_action_t reader_hold_paging_update(
    reader_hold_paging_t *state, const reader_hold_sample_t *sample) {
  if (!state || !sample) return no_action();

  if (sample->pressed && !state->pending && !state->active &&
      sample->direction != READER_HOLD_NONE) {
    state->pending = true;
    state->direction = sample->direction;
    return no_action();
  }

  if (sample->direction != state->direction) return no_action();
  if (sample->released || !sample->down) {
    const bool short_press = state->pending && !state->active;
    const reader_hold_direction_t direction = state->direction;
    reader_hold_paging_init(state);
    return short_press ? turn_action(direction, 1U) : no_action();
  }

  if (state->pending && sample->held_ms >= READER_HOLD_ENTER_MS) {
    state->pending = false;
    state->active = true;
    state->last_step_ms = sample->now_ms;
    return turn_action(state->direction, READER_HOLD_PAGE_STEP);
  }
  if (!state->active) return no_action();

  const uint32_t interval = reader_hold_step_interval_ms(sample->held_ms);
  if ((uint32_t)(sample->now_ms - state->last_step_ms) < interval)
    return no_action();
  state->last_step_ms = sample->now_ms;
  return turn_action(state->direction, READER_HOLD_PAGE_STEP);
}

size_t reader_hold_target_page(size_t current_page, size_t total_pages,
                               reader_hold_direction_t direction,
                               size_t page_step) {
  if (total_pages == 0U || page_step == 0U) return current_page;
  const size_t last_page = total_pages - 1U;
  if (current_page > last_page) current_page = last_page;
  if (direction == READER_HOLD_BACKWARD)
    return page_step > current_page ? 0U : current_page - page_step;
  if (direction == READER_HOLD_FORWARD)
    return page_step > last_page - current_page ? last_page
                                                : current_page + page_step;
  return current_page;
}
