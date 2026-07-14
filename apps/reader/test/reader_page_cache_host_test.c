#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "reader_page_cache.h"

enum { TEST_PAGE_SIZE = 4 };

typedef struct {
  unsigned calls;
  size_t failed_page;
} loader_context_t;

static bool load_page(size_t page_index, uint8_t *buffer, size_t length,
                      void *context) {
  loader_context_t *loader = context;
  ++loader->calls;
  if (page_index == loader->failed_page) {
    memset(buffer, 0xee, length);
    return false;
  }
  if (length != TEST_PAGE_SIZE)
    return false;
  memset(buffer, (int)page_index, length);
  return true;
}

static bool page_is(const uint8_t *page, uint8_t value) {
  if (!page) return false;
  for (size_t i = 0; i < TEST_PAGE_SIZE; ++i)
    if (page[i] != value) return false;
  return true;
}

int main(void) {
  uint8_t storage[READER_PAGE_CACHE_SLOT_COUNT * TEST_PAGE_SIZE];
  uint8_t scratch[TEST_PAGE_SIZE];
  reader_page_cache_t cache;
  loader_context_t loader = {.failed_page = SIZE_MAX};

  if (!reader_page_cache_init(&cache, storage, sizeof(storage),
                              TEST_PAGE_SIZE)) {
    fprintf(stderr, "cache init failed\n");
    return 1;
  }

  const uint8_t *page = reader_page_cache_get_or_load(
      &cache, 1U, load_page, &loader, scratch, sizeof(scratch));
  if (!page_is(page, 1U) || loader.calls != 1U) {
    fprintf(stderr, "cache miss did not load page\n");
    return 1;
  }
  page = reader_page_cache_get_or_load(&cache, 1U, load_page, &loader,
                                       scratch, sizeof(scratch));
  if (!page_is(page, 1U) || loader.calls != 1U) {
    fprintf(stderr, "cache hit repeated loader\n");
    return 1;
  }

  if (!reader_page_cache_get_or_load(&cache, 2U, load_page, &loader, scratch,
                                     sizeof(scratch)) ||
      !reader_page_cache_get_or_load(&cache, 3U, load_page, &loader, scratch,
                                     sizeof(scratch)) ||
      !reader_page_cache_lookup(&cache, 1U) ||
      !reader_page_cache_get_or_load(&cache, 4U, load_page, &loader, scratch,
                                     sizeof(scratch)) ||
      !reader_page_cache_lookup(&cache, 1U) ||
      reader_page_cache_lookup(&cache, 2U) != NULL ||
      !reader_page_cache_lookup(&cache, 3U) ||
      !reader_page_cache_lookup(&cache, 4U)) {
    fprintf(stderr, "cache did not evict least recently used page\n");
    return 1;
  }

  loader.failed_page = 9U;
  const unsigned calls_before_failure = loader.calls;
  if (reader_page_cache_get_or_load(&cache, 9U, load_page, &loader, scratch,
                                    sizeof(scratch)) != NULL ||
      loader.calls != calls_before_failure + 1U ||
      reader_page_cache_lookup(&cache, 9U) != NULL ||
      !page_is(reader_page_cache_lookup(&cache, 1U), 1U) ||
      !page_is(reader_page_cache_lookup(&cache, 3U), 3U) ||
      !page_is(reader_page_cache_lookup(&cache, 4U), 4U)) {
    fprintf(stderr, "failed load polluted cache slots\n");
    return 1;
  }

  uint8_t page_seven[TEST_PAGE_SIZE] = {7U, 7U, 7U, 7U};
  if (!reader_page_cache_insert(&cache, 7U, page_seven, sizeof(page_seven))) {
    fprintf(stderr, "cache insert failed\n");
    return 1;
  }
  page_seven[0] = 0U;
  if (!page_is(reader_page_cache_lookup(&cache, 7U), 7U)) {
    fprintf(stderr, "cache insert did not copy page\n");
    return 1;
  }

  reader_page_cache_reset(&cache);
  if (reader_page_cache_lookup(&cache, 1U) ||
      reader_page_cache_lookup(&cache, 3U) ||
      reader_page_cache_lookup(&cache, 4U) ||
      reader_page_cache_lookup(&cache, 7U)) {
    fprintf(stderr, "cache reset retained pages\n");
    return 1;
  }

  puts("PASS: reader page cache host tests");
  return 0;
}
