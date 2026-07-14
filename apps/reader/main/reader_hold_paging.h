#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
  READER_HOLD_ENTER_MS = 1000U,
  READER_HOLD_PAGE_STEP = 5U,
};

typedef enum {
  READER_HOLD_NONE = 0,
  READER_HOLD_BACKWARD = -1,
  READER_HOLD_FORWARD = 1,
} reader_hold_direction_t;

typedef enum {
  READER_HOLD_ACTION_NONE = 0,
  READER_HOLD_ACTION_TURN,
} reader_hold_action_kind_t;

typedef struct {
  reader_hold_direction_t direction;
  bool pressed;
  bool released;
  bool down;
  uint32_t held_ms;
  uint32_t now_ms;
} reader_hold_sample_t;

typedef struct {
  reader_hold_action_kind_t kind;
  reader_hold_direction_t direction;
  size_t page_step;
} reader_hold_action_t;

typedef struct {
  bool pending;
  bool active;
  reader_hold_direction_t direction;
  uint32_t last_step_ms;
} reader_hold_paging_t;

void reader_hold_paging_init(reader_hold_paging_t *state);
uint32_t reader_hold_step_interval_ms(uint32_t held_ms);
reader_hold_action_t reader_hold_paging_update(
    reader_hold_paging_t *state, const reader_hold_sample_t *sample);
size_t reader_hold_target_page(size_t current_page, size_t total_pages,
                               reader_hold_direction_t direction,
                               size_t page_step);
