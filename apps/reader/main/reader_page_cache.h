#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define READER_PAGE_CACHE_SLOT_COUNT 3U

typedef bool (*reader_page_cache_loader_fn)(size_t page_index,
                                            uint8_t *buffer, size_t length,
                                            void *context);

typedef struct {
  bool valid;
  size_t page_index;
  uint64_t last_used;
} reader_page_cache_slot_t;

typedef struct {
  uint8_t *storage;
  size_t page_size;
  uint64_t clock;
  reader_page_cache_slot_t slots[READER_PAGE_CACHE_SLOT_COUNT];
} reader_page_cache_t;

bool reader_page_cache_init(reader_page_cache_t *cache, uint8_t *storage,
                            size_t storage_size, size_t page_size);
void reader_page_cache_reset(reader_page_cache_t *cache);
const uint8_t *reader_page_cache_lookup(reader_page_cache_t *cache,
                                        size_t page_index);
bool reader_page_cache_insert(reader_page_cache_t *cache, size_t page_index,
                              const uint8_t *page, size_t length);
const uint8_t *reader_page_cache_get_or_load(
    reader_page_cache_t *cache, size_t page_index,
    reader_page_cache_loader_fn loader, void *context, uint8_t *scratch,
    size_t scratch_length);
