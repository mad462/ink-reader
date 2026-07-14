#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "reader_page_cache.h"

typedef bool (*reader_page_turn_work_fn)(void *context);
typedef void (*reader_page_turn_decorate_fn)(size_t page_index,
                                             uint8_t *buffer, size_t length,
                                             void *context);
typedef bool (*reader_page_turn_refresh_fn)(
    const uint8_t *candidate, size_t length, reader_page_turn_work_fn work,
    void *work_context, void *context);
typedef int64_t (*reader_page_turn_clock_fn)(void *context);

typedef enum {
  READER_PAGE_TURN_EVENT_TARGET_HIT,
  READER_PAGE_TURN_EVENT_TARGET_MISS,
  READER_PAGE_TURN_EVENT_PREFETCH_HIT,
  READER_PAGE_TURN_EVENT_PREFETCH_LOADED,
  READER_PAGE_TURN_EVENT_PREFETCH_FAILED,
} reader_page_turn_event_t;

typedef void (*reader_page_turn_observer_fn)(reader_page_turn_event_t event,
                                             size_t page_index,
                                             int64_t elapsed_us,
                                             void *context);

typedef struct {
  reader_page_cache_t *cache;
  size_t *current_page;
  size_t target_page;
  size_t prefetch_page;
  uint8_t *framebuffer;
  uint8_t *candidate;
  size_t page_size;
  reader_page_cache_loader_fn load;
  void *load_context;
  reader_page_turn_decorate_fn decorate;
  void *decorate_context;
  reader_page_turn_refresh_fn refresh;
  void *refresh_context;
  reader_page_turn_clock_fn clock;
  void *clock_context;
  reader_page_turn_observer_fn observe;
  void *observer_context;
} reader_page_turn_options_t;

typedef enum {
  READER_PAGE_TURN_PREPARE_FAILED,
  READER_PAGE_TURN_REFRESH_FAILED,
  READER_PAGE_TURN_COMMITTED,
} reader_page_turn_result_t;

reader_page_turn_result_t reader_page_turn_execute(
    const reader_page_turn_options_t *options);
