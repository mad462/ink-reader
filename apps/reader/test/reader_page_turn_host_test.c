#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "reader_page_turn.h"

enum { PAGE_SIZE = 4 };

typedef struct {
  size_t failed_page;
  unsigned load_calls;
} loader_context_t;

typedef struct {
  bool result;
  unsigned refresh_calls;
  unsigned work_calls;
  uint8_t expected_page;
  bool candidate_restored;
} refresh_context_t;

static bool load_page(size_t page_index, uint8_t *buffer, size_t length,
                      void *context) {
  loader_context_t *loader = context;
  ++loader->load_calls;
  memset(buffer, (int)page_index, length);
  return page_index != loader->failed_page;
}

static void decorate_page(size_t page_index, uint8_t *buffer, size_t length,
                          void *context) {
  (void)page_index;
  (void)buffer;
  (void)length;
  (void)context;
}

static bool buffer_is(const uint8_t *buffer, uint8_t value) {
  for (size_t i = 0; i < PAGE_SIZE; ++i)
    if (buffer[i] != value) return false;
  return true;
}

static bool fake_refresh(const uint8_t *candidate, size_t length,
                         reader_page_turn_work_fn work, void *work_context,
                         void *context) {
  refresh_context_t *refresh = context;
  ++refresh->refresh_calls;
  if (length != PAGE_SIZE ||
      !buffer_is(candidate, refresh->expected_page))
    return false;
  if (work) {
    ++refresh->work_calls;
    (void)work(work_context);
  }
  refresh->candidate_restored =
      buffer_is(candidate, refresh->expected_page);
  return refresh->result;
}

static void fill_page(uint8_t *page, uint8_t value) {
  memset(page, value, PAGE_SIZE);
}

static bool seed_full_cache(reader_page_cache_t *cache) {
  uint8_t page[PAGE_SIZE];
  for (size_t index = 1U; index <= 3U; ++index) {
    fill_page(page, (uint8_t)index);
    if (!reader_page_cache_insert(cache, index, page, sizeof(page)))
      return false;
  }
  return true;
}

static reader_page_turn_options_t make_options(
    reader_page_cache_t *cache, size_t *current_page, size_t target_page,
    size_t prefetch_page, uint8_t *framebuffer, uint8_t *candidate,
    loader_context_t *loader, refresh_context_t *refresh) {
  return (reader_page_turn_options_t){
      .cache = cache,
      .current_page = current_page,
      .target_page = target_page,
      .prefetch_page = prefetch_page,
      .framebuffer = framebuffer,
      .candidate = candidate,
      .page_size = PAGE_SIZE,
      .load = load_page,
      .load_context = loader,
      .decorate = decorate_page,
      .refresh = fake_refresh,
      .refresh_context = refresh,
  };
}

int main(void) {
  uint8_t storage[READER_PAGE_CACHE_SLOT_COUNT * PAGE_SIZE];
  uint8_t framebuffer[PAGE_SIZE];
  uint8_t candidate[PAGE_SIZE];
  reader_page_cache_t cache;
  loader_context_t loader = {.failed_page = SIZE_MAX};
  refresh_context_t refresh = {
      .result = true,
      .expected_page = 2U,
  };
  size_t current_page = 1U;

  if (!reader_page_cache_init(&cache, storage, sizeof(storage), PAGE_SIZE) ||
      !seed_full_cache(&cache)) {
    fprintf(stderr, "full cache setup failed\n");
    return 1;
  }
  fill_page(framebuffer, 1U);
  reader_page_turn_options_t options =
      make_options(&cache, &current_page, 2U, 4U, framebuffer, candidate,
                   &loader, &refresh);
  if (reader_page_turn_execute(&options) != READER_PAGE_TURN_COMMITTED ||
      current_page != 2U || !buffer_is(framebuffer, 2U) ||
      !refresh.candidate_restored || refresh.refresh_calls != 1U ||
      refresh.work_calls != 1U ||
      !buffer_is(reader_page_cache_lookup(&cache, 4U), 4U)) {
    fprintf(stderr, "full-cache prefetch did not restore target candidate\n");
    return 1;
  }

  reader_page_cache_reset(&cache);
  if (!seed_full_cache(&cache)) return 1;
  loader.failed_page = 4U;
  refresh = (refresh_context_t){
      .result = true,
      .expected_page = 2U,
  };
  current_page = 1U;
  fill_page(framebuffer, 1U);
  options = make_options(&cache, &current_page, 2U, 4U, framebuffer,
                         candidate, &loader, &refresh);
  if (reader_page_turn_execute(&options) != READER_PAGE_TURN_COMMITTED ||
      current_page != 2U || !buffer_is(framebuffer, 2U) ||
      !refresh.candidate_restored || refresh.work_calls != 1U ||
      reader_page_cache_lookup(&cache, 4U) != NULL) {
    fprintf(stderr, "failed prefetch did not restore target candidate\n");
    return 1;
  }

  loader.failed_page = SIZE_MAX;
  refresh = (refresh_context_t){
      .result = false,
      .expected_page = 2U,
  };
  current_page = 1U;
  fill_page(framebuffer, 1U);
  options = make_options(&cache, &current_page, 2U, 3U, framebuffer,
                         candidate, &loader, &refresh);
  if (reader_page_turn_execute(&options) != READER_PAGE_TURN_REFRESH_FAILED ||
      current_page != 1U || !buffer_is(framebuffer, 1U) ||
      refresh.refresh_calls != 1U) {
    fprintf(stderr, "refresh failure did not roll back transaction\n");
    return 1;
  }

  refresh = (refresh_context_t){
      .result = true,
      .expected_page = 2U,
  };
  current_page = 1U;
  fill_page(framebuffer, 1U);
  options = make_options(&cache, &current_page, 2U, 3U, framebuffer,
                         candidate, &loader, &refresh);
  if (reader_page_turn_execute(&options) != READER_PAGE_TURN_COMMITTED ||
      refresh.refresh_calls != 1U || current_page != 2U ||
      !buffer_is(framebuffer, 2U)) {
    fprintf(stderr, "successful transaction did not refresh exactly once\n");
    return 1;
  }

  puts("PASS: reader page turn transaction host tests");
  return 0;
}
