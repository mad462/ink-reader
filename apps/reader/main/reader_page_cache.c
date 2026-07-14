#include "reader_page_cache.h"

#include <string.h>

static uint8_t *slot_data(reader_page_cache_t *cache, size_t slot_index) {
  return cache->storage + slot_index * cache->page_size;
}

static size_t find_slot(const reader_page_cache_t *cache, size_t page_index) {
  for (size_t i = 0; i < READER_PAGE_CACHE_SLOT_COUNT; ++i)
    if (cache->slots[i].valid && cache->slots[i].page_index == page_index)
      return i;
  return READER_PAGE_CACHE_SLOT_COUNT;
}

static size_t choose_victim(const reader_page_cache_t *cache) {
  size_t victim = 0U;
  for (size_t i = 0; i < READER_PAGE_CACHE_SLOT_COUNT; ++i) {
    if (!cache->slots[i].valid) return i;
    if (cache->slots[i].last_used < cache->slots[victim].last_used) victim = i;
  }
  return victim;
}

bool reader_page_cache_init(reader_page_cache_t *cache, uint8_t *storage,
                            size_t storage_size, size_t page_size) {
  if (!cache || !storage || page_size == 0U ||
      page_size > SIZE_MAX / READER_PAGE_CACHE_SLOT_COUNT ||
      storage_size < page_size * READER_PAGE_CACHE_SLOT_COUNT)
    return false;
  cache->storage = storage;
  cache->page_size = page_size;
  reader_page_cache_reset(cache);
  return true;
}

void reader_page_cache_reset(reader_page_cache_t *cache) {
  if (!cache) return;
  cache->clock = 0U;
  memset(cache->slots, 0, sizeof(cache->slots));
}

const uint8_t *reader_page_cache_lookup(reader_page_cache_t *cache,
                                        size_t page_index) {
  if (!cache || !cache->storage || cache->page_size == 0U) return NULL;
  const size_t slot_index = find_slot(cache, page_index);
  if (slot_index == READER_PAGE_CACHE_SLOT_COUNT) return NULL;
  cache->slots[slot_index].last_used = ++cache->clock;
  return slot_data(cache, slot_index);
}

bool reader_page_cache_insert(reader_page_cache_t *cache, size_t page_index,
                              const uint8_t *page, size_t length) {
  if (!cache || !cache->storage || !page || length < cache->page_size)
    return false;
  size_t slot_index = find_slot(cache, page_index);
  if (slot_index == READER_PAGE_CACHE_SLOT_COUNT)
    slot_index = choose_victim(cache);
  memcpy(slot_data(cache, slot_index), page, cache->page_size);
  cache->slots[slot_index] = (reader_page_cache_slot_t){
      .valid = true, .page_index = page_index, .last_used = ++cache->clock};
  return true;
}

const uint8_t *reader_page_cache_get_or_load(
    reader_page_cache_t *cache, size_t page_index,
    reader_page_cache_loader_fn loader, void *context, uint8_t *scratch,
    size_t scratch_length) {
  const uint8_t *page = reader_page_cache_lookup(cache, page_index);
  if (page) return page;
  if (!cache || !loader || !scratch || scratch_length < cache->page_size ||
      !loader(page_index, scratch, cache->page_size, context) ||
      !reader_page_cache_insert(cache, page_index, scratch, cache->page_size))
    return NULL;
  return reader_page_cache_lookup(cache, page_index);
}
