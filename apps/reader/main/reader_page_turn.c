#include "reader_page_turn.h"

#include <string.h>

typedef struct {
  const reader_page_turn_options_t *options;
} prefetch_context_t;

static int64_t now_us(const reader_page_turn_options_t *options) {
  return options->clock ? options->clock(options->clock_context) : 0;
}

static void observe(const reader_page_turn_options_t *options,
                    reader_page_turn_event_t event, size_t page_index,
                    int64_t started_us) {
  if (options->observe)
    options->observe(event, page_index, now_us(options) - started_us,
                     options->observer_context);
}

static bool prefetch_and_restore(void *context) {
  const reader_page_turn_options_t *options =
      ((prefetch_context_t *)context)->options;
  const int64_t started_us = now_us(options);
  const uint8_t *prefetched =
      reader_page_cache_lookup(options->cache, options->prefetch_page);
  if (prefetched) {
    observe(options, READER_PAGE_TURN_EVENT_PREFETCH_HIT,
            options->prefetch_page, started_us);
  } else {
    prefetched = reader_page_cache_get_or_load(
        options->cache, options->prefetch_page, options->load,
        options->load_context, options->candidate, options->page_size);
    observe(options,
            prefetched ? READER_PAGE_TURN_EVENT_PREFETCH_LOADED
                       : READER_PAGE_TURN_EVENT_PREFETCH_FAILED,
            options->prefetch_page, started_us);
  }

  const uint8_t *displayed =
      reader_page_cache_lookup(options->cache, options->target_page);
  if (!displayed) return false;
  memcpy(options->candidate, displayed, options->page_size);
  if (options->decorate)
    options->decorate(options->target_page, options->candidate,
                      options->page_size, options->decorate_context);
  return prefetched != NULL;
}

reader_page_turn_result_t reader_page_turn_execute(
    const reader_page_turn_options_t *options) {
  if (!options || !options->current_page || !options->framebuffer ||
      !options->candidate || options->page_size == 0U || !options->load ||
      !options->refresh)
    return READER_PAGE_TURN_PREPARE_FAILED;

  const size_t previous_page = *options->current_page;
  const int64_t started_us = now_us(options);
  memcpy(options->candidate, options->framebuffer, options->page_size);
  const uint8_t *cached = options->cache
                              ? reader_page_cache_lookup(options->cache,
                                                         options->target_page)
                              : NULL;
  bool target_cached = cached != NULL;
  if (cached) {
    memcpy(options->candidate, cached, options->page_size);
    observe(options, READER_PAGE_TURN_EVENT_TARGET_HIT, options->target_page,
            started_us);
  } else {
    const bool loaded = options->load(options->target_page, options->candidate,
                                      options->page_size,
                                      options->load_context);
    observe(options, READER_PAGE_TURN_EVENT_TARGET_MISS, options->target_page,
            started_us);
    if (!loaded) return READER_PAGE_TURN_PREPARE_FAILED;
    if (options->cache)
      target_cached = reader_page_cache_insert(
          options->cache, options->target_page, options->candidate,
          options->page_size);
  }

  *options->current_page = options->target_page;
  if (options->decorate)
    options->decorate(options->target_page, options->candidate,
                      options->page_size, options->decorate_context);

  prefetch_context_t prefetch_context = {.options = options};
  reader_page_turn_work_fn work = NULL;
  if (options->cache && target_cached &&
      options->prefetch_page != options->target_page)
    work = prefetch_and_restore;

  if (!options->refresh(options->candidate, options->page_size, work,
                        &prefetch_context, options->refresh_context)) {
    *options->current_page = previous_page;
    return READER_PAGE_TURN_REFRESH_FAILED;
  }
  memcpy(options->framebuffer, options->candidate, options->page_size);
  return READER_PAGE_TURN_COMMITTED;
}
