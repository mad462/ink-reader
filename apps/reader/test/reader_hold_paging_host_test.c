#include <stdio.h>

#include "reader_hold_paging.h"

static reader_hold_sample_t sample(reader_hold_direction_t direction,
                                   bool pressed, bool released, bool down,
                                   uint32_t held_ms, uint32_t now_ms) {
  return (reader_hold_sample_t){.direction = direction,
                                .pressed = pressed,
                                .released = released,
                                .down = down,
                                .held_ms = held_ms,
                                .now_ms = now_ms};
}

static int expect_short_press_turns_one_page_on_release(void) {
  reader_hold_paging_t state;
  reader_hold_paging_init(&state);
  reader_hold_sample_t event =
      sample(READER_HOLD_FORWARD, true, false, true, 0U, 100U);
  reader_hold_action_t action = reader_hold_paging_update(&state, &event);
  if (action.kind != READER_HOLD_ACTION_NONE || !state.pending) return 0;

  event = sample(READER_HOLD_FORWARD, false, false, true, 700U, 800U);
  action = reader_hold_paging_update(&state, &event);
  if (action.kind != READER_HOLD_ACTION_NONE) return 0;

  event = sample(READER_HOLD_FORWARD, false, true, false, 0U, 820U);
  action = reader_hold_paging_update(&state, &event);
  return action.kind == READER_HOLD_ACTION_TURN && action.page_step == 1U &&
         action.direction == READER_HOLD_FORWARD && !state.pending &&
         !state.active;
}

static int expect_hold_repeats_five_page_turns(void) {
  reader_hold_paging_t state;
  reader_hold_paging_init(&state);
  reader_hold_sample_t event =
      sample(READER_HOLD_BACKWARD, true, false, true, 0U, 1000U);
  (void)reader_hold_paging_update(&state, &event);

  event = sample(READER_HOLD_BACKWARD, false, false, true, 999U, 1999U);
  if (reader_hold_paging_update(&state, &event).kind !=
      READER_HOLD_ACTION_NONE)
    return 0;

  event = sample(READER_HOLD_BACKWARD, false, false, true, 1000U, 2000U);
  reader_hold_action_t action = reader_hold_paging_update(&state, &event);
  if (action.kind != READER_HOLD_ACTION_TURN || action.page_step != 5U ||
      action.direction != READER_HOLD_BACKWARD || !state.active)
    return 0;

  event = sample(READER_HOLD_BACKWARD, false, false, true, 1359U, 2359U);
  if (reader_hold_paging_update(&state, &event).kind !=
      READER_HOLD_ACTION_NONE)
    return 0;
  event = sample(READER_HOLD_BACKWARD, false, false, true, 1360U, 2360U);
  action = reader_hold_paging_update(&state, &event);
  if (action.kind != READER_HOLD_ACTION_TURN || action.page_step != 5U)
    return 0;

  event = sample(READER_HOLD_BACKWARD, false, true, false, 0U, 2400U);
  action = reader_hold_paging_update(&state, &event);
  return action.kind == READER_HOLD_ACTION_NONE && !state.pending &&
         !state.active;
}

static int expect_old_firmware_timing_and_page_clamping(void) {
  return reader_hold_step_interval_ms(4999U) == 360U &&
         reader_hold_step_interval_ms(5000U) == 300U &&
         reader_hold_step_interval_ms(10000U) == 240U &&
         reader_hold_step_interval_ms(20000U) == 200U &&
         reader_hold_target_page(10U, 100U, READER_HOLD_FORWARD, 5U) == 15U &&
         reader_hold_target_page(97U, 100U, READER_HOLD_FORWARD, 5U) == 99U &&
         reader_hold_target_page(3U, 100U, READER_HOLD_BACKWARD, 5U) == 0U &&
         reader_hold_target_page(0U, 0U, READER_HOLD_FORWARD, 5U) == 0U;
}

int main(void) {
  if (!expect_short_press_turns_one_page_on_release()) {
    fputs("short press behavior failed\n", stderr);
    return 1;
  }
  if (!expect_hold_repeats_five_page_turns()) {
    fputs("hold repeat behavior failed\n", stderr);
    return 1;
  }
  if (!expect_old_firmware_timing_and_page_clamping()) {
    fputs("hold timing or page clamping failed\n", stderr);
    return 1;
  }
  puts("PASS: reader hold paging host tests");
  return 0;
}
