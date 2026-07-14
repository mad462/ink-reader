#include "ink_reader_state.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

#define STATE_MAGIC 0x49534150U
#define STATE_VERSION 4U
#define DISK_HEADER_SIZE 8U
#define DISK_V1_SIZE 276U
#define DISK_V2_SIZE 284U
#define DISK_V3_SIZE 9404U
#define DISK_V4_SIZE 23616U

#define V1_OPEN_PATH_OFFSET 12U
#define COMMON_OPEN_PATH_OFFSET 20U
#define COMMON_STATE_SIZE 276U
#define PROGRESS_OFFSET 276U
#define PROGRESS_DISK_SIZE 276U
#define BOOKMARK_OFFSET 4692U
#define BOOKMARK_DISK_SIZE 392U
#define RECENT_COUNTER_OFFSET 9396U
#define BOOKSHELF_OFFSET 9400U
#define BOOKSHELF_DISK_SIZE 444U
#define TEMP_PATH_CAPACITY 384U

#if SIZE_MAX == UINT32_MAX
_Static_assert(sizeof(ink_reader_progress_entry_t) == PROGRESS_DISK_SIZE,
               "legacy progress ABI changed");
_Static_assert(offsetof(ink_reader_progress_entry_t, book_path) == 20U,
               "legacy progress offsets changed");
_Static_assert(sizeof(ink_reader_bookmark_t) == BOOKMARK_DISK_SIZE,
               "legacy bookmark ABI changed");
_Static_assert(offsetof(ink_reader_bookmark_t, chapter_title) == 276U &&
                   offsetof(ink_reader_bookmark_t, timestamp_text) == 357U,
               "legacy bookmark offsets changed");
_Static_assert(sizeof(ink_reader_bookshelf_entry_t) == BOOKSHELF_DISK_SIZE,
               "legacy bookshelf ABI changed");
_Static_assert(offsetof(ink_reader_bookshelf_entry_t, book_path) == 24U &&
                   offsetof(ink_reader_bookshelf_entry_t, title) == 280U &&
                   offsetof(ink_reader_bookshelf_entry_t, chapter_title) ==
                       361U,
               "legacy bookshelf offsets changed");
_Static_assert(sizeof(ink_reader_state_t) == DISK_V4_SIZE - DISK_HEADER_SIZE,
               "legacy state ABI changed");
_Static_assert(offsetof(ink_reader_state_t, progress) == PROGRESS_OFFSET &&
                   offsetof(ink_reader_state_t, bookmarks) ==
                       BOOKMARK_OFFSET &&
                   offsetof(ink_reader_state_t, recent_order_counter) ==
                       RECENT_COUNTER_OFFSET &&
                   offsetof(ink_reader_state_t, bookshelf) == BOOKSHELF_OFFSET,
               "legacy state offsets changed");
#endif

static uint16_t read_u16(const uint8_t *p) {
  return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t read_u32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void write_u16(uint8_t *p, uint16_t value) {
  p[0] = (uint8_t)value;
  p[1] = (uint8_t)(value >> 8);
}

static void write_u32(uint8_t *p, uint32_t value) {
  write_u16(p, (uint16_t)value);
  write_u16(p + 2, (uint16_t)(value >> 16));
}

static bool valid_bool(uint8_t value) { return value <= 1U; }

static bool decode_text(char *dst, size_t dst_size, const uint8_t *src,
                        size_t src_size) {
  const uint8_t *end = (const uint8_t *)memchr(src, 0, src_size);
  if (!end || dst_size == 0U) return false;
  const size_t length = (size_t)(end - src);
  if (length >= dst_size) return false;
  if (length > 0U) memcpy(dst, src, length);
  dst[length] = '\0';
  return true;
}

static bool encode_text(uint8_t *dst, size_t dst_size, const char *src) {
  if (!src) return false;
  size_t length = 0U;
  while (length < dst_size && src[length] != '\0') ++length;
  if (length == dst_size) return false;
  if (length > 0U) memcpy(dst, src, length);
  return true;
}

static void copy_text(char *dst, size_t dst_size, const char *src) {
  if (!dst || dst_size == 0U) return;
  if (!src) {
    dst[0] = '\0';
    return;
  }
  snprintf(dst, dst_size, "%s", src);
}

static bool progress_matches(const ink_reader_progress_entry_t *entry,
                             const char *path) {
  return entry && entry->used && entry->book_kind == INK_READER_BOOK_KIND_XTC &&
         path && strcmp(entry->book_path, path) == 0;
}

static int find_progress_slot(const ink_reader_state_t *state,
                              const char *path) {
  if (!state || !path || path[0] == '\0') return -1;
  for (size_t i = 0; i < INK_READER_PROGRESS_CAPACITY; ++i)
    if (progress_matches(&state->progress[i], path)) return (int)i;
  return -1;
}

static int free_progress_slot(const ink_reader_state_t *state) {
  if (!state) return -1;
  for (size_t i = 0; i < INK_READER_PROGRESS_CAPACITY; ++i)
    if (!state->progress[i].used) return (int)i;
  return 0;
}

static bool bookshelf_matches(const ink_reader_bookshelf_entry_t *entry,
                              const char *path) {
  return entry && entry->used && entry->book_kind == INK_READER_BOOK_KIND_XTC &&
         path && strcmp(entry->book_path, path) == 0;
}

static int find_bookshelf_slot(const ink_reader_state_t *state,
                               const char *path) {
  if (!state || !path || path[0] == '\0') return -1;
  for (size_t i = 0; i < INK_READER_BOOKSHELF_CAPACITY; ++i)
    if (bookshelf_matches(&state->bookshelf[i], path)) return (int)i;
  return -1;
}

static int free_bookshelf_slot(const ink_reader_state_t *state) {
  if (!state) return -1;
  for (size_t i = 0; i < INK_READER_BOOKSHELF_CAPACITY; ++i)
    if (!state->bookshelf[i].used) return (int)i;
  return -1;
}

static int oldest_bookshelf_slot(const ink_reader_state_t *state) {
  if (!state) return -1;
  size_t best = 0U;
  for (size_t i = 1; i < INK_READER_BOOKSHELF_CAPACITY; ++i) {
    const bool candidate_opened = state->bookshelf[i].has_opened;
    const bool best_opened = state->bookshelf[best].has_opened;
    if ((!candidate_opened && best_opened) ||
        (candidate_opened == best_opened &&
         state->bookshelf[i].recent_order <
             state->bookshelf[best].recent_order))
      best = i;
  }
  return (int)best;
}

static ink_reader_bookshelf_entry_t *upsert_bookshelf(
    ink_reader_state_t *state, const char *path, const char *title) {
  if (!state || !path || path[0] == '\0') return NULL;
  int slot = find_bookshelf_slot(state, path);
  if (slot < 0) slot = free_bookshelf_slot(state);
  if (slot < 0) slot = oldest_bookshelf_slot(state);
  if (slot < 0) return NULL;
  ink_reader_bookshelf_entry_t *entry = &state->bookshelf[slot];
  if (!entry->used || strcmp(entry->book_path, path) != 0) {
    memset(entry, 0, sizeof(*entry));
    entry->used = true;
    entry->book_kind = INK_READER_BOOK_KIND_XTC;
    copy_text(entry->book_path, sizeof(entry->book_path), path);
  }
  if (title && title[0] != '\0')
    copy_text(entry->title, sizeof(entry->title), title);
  return entry;
}

static bool bookmark_matches(const ink_reader_bookmark_t *bookmark,
                             const char *path, size_t page_index) {
  return bookmark && bookmark->used &&
         bookmark->book_kind == INK_READER_BOOK_KIND_XTC &&
         bookmark->page_index == page_index && path &&
         strcmp(bookmark->book_path, path) == 0;
}

static int find_bookmark_slot(const ink_reader_state_t *state,
                              const char *path, size_t page_index) {
  if (!state || !path || path[0] == '\0') return -1;
  for (size_t i = 0; i < INK_READER_BOOKMARK_CAPACITY; ++i)
    if (bookmark_matches(&state->bookmarks[i], path, page_index)) return (int)i;
  return -1;
}

static int free_bookmark_slot(const ink_reader_state_t *state) {
  if (!state) return -1;
  for (size_t i = 0; i < INK_READER_BOOKMARK_CAPACITY; ++i)
    if (!state->bookmarks[i].used) return (int)i;
  return -1;
}

static int oldest_bookmark_slot(const ink_reader_state_t *state) {
  if (!state) return -1;
  size_t best = 0U;
  for (size_t i = 1; i < INK_READER_BOOKMARK_CAPACITY; ++i)
    if (strcmp(state->bookmarks[i].timestamp_text,
               state->bookmarks[best].timestamp_text) < 0)
      best = i;
  return (int)best;
}

void ink_reader_state_default(ink_reader_state_t *state) {
  if (state) memset(state, 0, sizeof(*state));
}

void ink_reader_state_clear_open(ink_reader_state_t *state) {
  if (!state) return;
  state->has_open_book = false;
  state->open_book_kind = INK_READER_BOOK_KIND_NONE;
  state->open_book_page = 0U;
  state->open_book_chapter = 0U;
  state->open_book_total_pages_snapshot = 0U;
  state->open_book_path[0] = '\0';
}

void ink_reader_state_remember_open(ink_reader_state_t *state,
                                    const char *path, size_t page_index,
                                    size_t chapter_index,
                                    size_t total_pages_snapshot) {
  if (!state) return;
  state->has_open_book = true;
  state->open_book_kind = INK_READER_BOOK_KIND_XTC;
  state->open_book_page = page_index;
  state->open_book_chapter = chapter_index;
  state->open_book_total_pages_snapshot = total_pages_snapshot;
  copy_text(state->open_book_path, sizeof(state->open_book_path), path);
  (void)ink_reader_state_set_progress(state, path, page_index, chapter_index,
                                      total_pages_snapshot);
}

bool ink_reader_state_set_progress(ink_reader_state_t *state,
                                   const char *path, size_t page_index,
                                   size_t chapter_index,
                                   size_t total_pages_snapshot) {
  if (!state || !path || path[0] == '\0') return false;
  int slot = find_progress_slot(state, path);
  if (slot < 0) slot = free_progress_slot(state);
  if (slot < 0) return false;
  ink_reader_progress_entry_t *entry = &state->progress[slot];
  memset(entry, 0, sizeof(*entry));
  entry->used = true;
  entry->book_kind = INK_READER_BOOK_KIND_XTC;
  entry->page_index = page_index;
  entry->chapter_index = chapter_index;
  entry->total_pages_snapshot = total_pages_snapshot;
  copy_text(entry->book_path, sizeof(entry->book_path), path);
  return true;
}

bool ink_reader_state_find_progress(const ink_reader_state_t *state,
                                    const char *path, size_t *page_index_out,
                                    size_t *chapter_index_out,
                                    size_t *total_pages_snapshot_out) {
  const int slot = find_progress_slot(state, path);
  if (slot < 0) return false;
  const ink_reader_progress_entry_t *entry = &state->progress[slot];
  if (page_index_out) *page_index_out = entry->page_index;
  if (chapter_index_out) *chapter_index_out = entry->chapter_index;
  if (total_pages_snapshot_out)
    *total_pages_snapshot_out = entry->total_pages_snapshot;
  return true;
}

bool ink_reader_state_note_open(ink_reader_state_t *state, const char *path,
                                const char *title, size_t page_index,
                                size_t chapter_index,
                                size_t total_pages_snapshot,
                                const char *chapter_title) {
  ink_reader_bookshelf_entry_t *entry = upsert_bookshelf(state, path, title);
  if (!entry) return false;
  ++state->recent_order_counter;
  if (state->recent_order_counter == 0U) state->recent_order_counter = 1U;
  entry->has_opened = true;
  entry->recent_order = state->recent_order_counter;
  entry->page_index = page_index;
  entry->chapter_index = chapter_index;
  entry->total_pages_snapshot = total_pages_snapshot;
  if (chapter_title && chapter_title[0] != '\0')
    copy_text(entry->chapter_title, sizeof(entry->chapter_title), chapter_title);
  return true;
}

bool ink_reader_state_set_favorite(ink_reader_state_t *state,
                                   const char *path, const char *title,
                                   bool is_favorite) {
  ink_reader_bookshelf_entry_t *entry = upsert_bookshelf(state, path, title);
  if (!entry) return false;
  entry->is_favorite = is_favorite;
  return true;
}

bool ink_reader_state_find_bookshelf(const ink_reader_state_t *state,
                                     const char *path,
                                     size_t *entry_index_out) {
  const int slot = find_bookshelf_slot(state, path);
  if (entry_index_out) *entry_index_out = slot >= 0 ? (size_t)slot : 0U;
  return slot >= 0;
}

const ink_reader_bookshelf_entry_t *ink_reader_state_bookshelf_at(
    const ink_reader_state_t *state, size_t entry_index) {
  if (!state || entry_index >= INK_READER_BOOKSHELF_CAPACITY) return NULL;
  return state->bookshelf[entry_index].used ? &state->bookshelf[entry_index]
                                            : NULL;
}

static bool write_bookmark(ink_reader_bookmark_t *bookmark, const char *path,
                           size_t page_index, size_t chapter_index,
                           size_t total_pages_snapshot,
                           const char *chapter_title,
                           const char *timestamp_text) {
  if (!bookmark || !path || path[0] == '\0' || !timestamp_text ||
      timestamp_text[0] == '\0')
    return false;
  memset(bookmark, 0, sizeof(*bookmark));
  bookmark->used = true;
  bookmark->book_kind = INK_READER_BOOK_KIND_XTC;
  bookmark->page_index = page_index;
  bookmark->chapter_index = chapter_index;
  bookmark->total_pages_snapshot = total_pages_snapshot;
  copy_text(bookmark->book_path, sizeof(bookmark->book_path), path);
  copy_text(bookmark->chapter_title, sizeof(bookmark->chapter_title),
            chapter_title);
  copy_text(bookmark->timestamp_text, sizeof(bookmark->timestamp_text),
            timestamp_text);
  return true;
}

bool ink_reader_state_bookmark_add_or_replace(
    ink_reader_state_t *state, const char *path, size_t page_index,
    size_t chapter_index, size_t total_pages_snapshot,
    const char *chapter_title, const char *timestamp_text) {
  if (!state) return false;
  int slot = find_bookmark_slot(state, path, page_index);
  if (slot < 0) slot = free_bookmark_slot(state);
  if (slot < 0) slot = oldest_bookmark_slot(state);
  return slot >= 0 &&
         write_bookmark(&state->bookmarks[slot], path, page_index,
                        chapter_index, total_pages_snapshot, chapter_title,
                        timestamp_text);
}

bool ink_reader_state_bookmark_overwrite(
    ink_reader_state_t *state, size_t bookmark_index, const char *path,
    size_t page_index, size_t chapter_index, size_t total_pages_snapshot,
    const char *chapter_title, const char *timestamp_text) {
  return state && bookmark_index < INK_READER_BOOKMARK_CAPACITY &&
         write_bookmark(&state->bookmarks[bookmark_index], path, page_index,
                        chapter_index, total_pages_snapshot, chapter_title,
                        timestamp_text);
}

bool ink_reader_state_bookmark_remove(ink_reader_state_t *state,
                                      const char *path, size_t page_index) {
  const int slot = find_bookmark_slot(state, path, page_index);
  if (slot < 0) return false;
  memset(&state->bookmarks[slot], 0, sizeof(state->bookmarks[slot]));
  return true;
}

bool ink_reader_state_bookmark_find(const ink_reader_state_t *state,
                                    const char *path, size_t page_index,
                                    size_t *bookmark_index_out) {
  const int slot = find_bookmark_slot(state, path, page_index);
  if (bookmark_index_out)
    *bookmark_index_out = slot >= 0 ? (size_t)slot : 0U;
  return slot >= 0;
}

size_t ink_reader_state_bookmark_count(const ink_reader_state_t *state,
                                       const char *path) {
  if (!state || !path || path[0] == '\0') return 0U;
  size_t count = 0U;
  for (size_t i = 0; i < INK_READER_BOOKMARK_CAPACITY; ++i)
    if (state->bookmarks[i].used &&
        strcmp(state->bookmarks[i].book_path, path) == 0)
      ++count;
  return count;
}

const ink_reader_bookmark_t *ink_reader_state_bookmark_at(
    const ink_reader_state_t *state, size_t bookmark_index) {
  if (!state || bookmark_index >= INK_READER_BOOKMARK_CAPACITY) return NULL;
  return state->bookmarks[bookmark_index].used
             ? &state->bookmarks[bookmark_index]
             : NULL;
}

static bool decode_common(const uint8_t *src, ink_reader_state_t *state) {
  if (!valid_bool(src[0])) return false;
  state->has_open_book = src[0] != 0U;
  state->open_book_kind = (ink_reader_book_kind_t)read_u32(src + 4);
  state->open_book_page = read_u32(src + 8);
  state->open_book_chapter = read_u32(src + 12);
  state->open_book_total_pages_snapshot = read_u32(src + 16);
  return decode_text(state->open_book_path, sizeof(state->open_book_path),
                     src + COMMON_OPEN_PATH_OFFSET,
                     INK_READER_STATE_PATH_LENGTH + 1U);
}

static bool decode_progress(const uint8_t *src, ink_reader_state_t *state) {
  for (size_t i = 0; i < INK_READER_PROGRESS_CAPACITY; ++i) {
    const uint8_t *wire = src + i * PROGRESS_DISK_SIZE;
    if (!valid_bool(wire[0])) return false;
    if (!wire[0]) continue;
    ink_reader_progress_entry_t *entry = &state->progress[i];
    if (read_u32(wire + 4) != INK_READER_BOOK_KIND_XTC) return false;
    entry->used = true;
    entry->book_kind = INK_READER_BOOK_KIND_XTC;
    entry->page_index = read_u32(wire + 8);
    entry->chapter_index = read_u32(wire + 12);
    entry->total_pages_snapshot = read_u32(wire + 16);
    if (!decode_text(entry->book_path, sizeof(entry->book_path), wire + 20,
                     INK_READER_STATE_PATH_LENGTH + 1U) ||
        entry->book_path[0] == '\0')
      return false;
  }
  return true;
}

static bool decode_bookmarks(const uint8_t *src, ink_reader_state_t *state) {
  for (size_t i = 0; i < INK_READER_BOOKMARK_CAPACITY; ++i) {
    const uint8_t *wire = src + i * BOOKMARK_DISK_SIZE;
    if (!valid_bool(wire[0])) return false;
    if (!wire[0]) continue;
    ink_reader_bookmark_t *entry = &state->bookmarks[i];
    if (read_u32(wire + 4) != INK_READER_BOOK_KIND_XTC) return false;
    entry->used = true;
    entry->book_kind = INK_READER_BOOK_KIND_XTC;
    entry->page_index = read_u32(wire + 8);
    entry->chapter_index = read_u32(wire + 12);
    entry->total_pages_snapshot = read_u32(wire + 16);
    if (!decode_text(entry->book_path, sizeof(entry->book_path), wire + 20,
                     INK_READER_STATE_PATH_LENGTH + 1U) ||
        !decode_text(entry->chapter_title, sizeof(entry->chapter_title),
                     wire + 276, INK_READER_BOOKMARK_TITLE_LENGTH + 1U) ||
        !decode_text(entry->timestamp_text, sizeof(entry->timestamp_text),
                     wire + 357, INK_READER_BOOKMARK_TIME_LENGTH + 1U) ||
        entry->book_path[0] == '\0' || entry->timestamp_text[0] == '\0')
      return false;
  }
  return true;
}

static bool decode_bookshelf(const uint8_t *src, ink_reader_state_t *state) {
  for (size_t i = 0; i < INK_READER_BOOKSHELF_CAPACITY; ++i) {
    const uint8_t *wire = src + i * BOOKSHELF_DISK_SIZE;
    if (!valid_bool(wire[0]) || !valid_bool(wire[1]) ||
        !valid_bool(wire[2]))
      return false;
    if (!wire[0]) continue;
    ink_reader_bookshelf_entry_t *entry = &state->bookshelf[i];
    if (read_u32(wire + 4) != INK_READER_BOOK_KIND_XTC) return false;
    entry->used = true;
    entry->has_opened = wire[1] != 0U;
    entry->is_favorite = wire[2] != 0U;
    entry->book_kind = INK_READER_BOOK_KIND_XTC;
    entry->recent_order = read_u32(wire + 8);
    entry->page_index = read_u32(wire + 12);
    entry->chapter_index = read_u32(wire + 16);
    entry->total_pages_snapshot = read_u32(wire + 20);
    if (!decode_text(entry->book_path, sizeof(entry->book_path), wire + 24,
                     INK_READER_STATE_PATH_LENGTH + 1U) ||
        !decode_text(entry->title, sizeof(entry->title), wire + 280,
                     INK_READER_BOOK_TITLE_LENGTH + 1U) ||
        !decode_text(entry->chapter_title, sizeof(entry->chapter_title),
                     wire + 361, INK_READER_BOOK_TITLE_LENGTH + 1U) ||
        entry->book_path[0] == '\0')
      return false;
  }
  return true;
}

static void migrate_progress(ink_reader_state_t *state) {
  for (size_t i = 0; i < INK_READER_PROGRESS_CAPACITY; ++i) {
    const ink_reader_progress_entry_t *entry = &state->progress[i];
    if (entry->used && entry->book_kind == INK_READER_BOOK_KIND_XTC &&
        entry->book_path[0] != '\0')
      (void)ink_reader_state_note_open(
          state, entry->book_path, NULL, entry->page_index,
          entry->chapter_index, entry->total_pages_snapshot, NULL);
  }
}

static void migrate_open(ink_reader_state_t *state) {
  if (state->has_open_book &&
      state->open_book_kind == INK_READER_BOOK_KIND_XTC &&
      state->open_book_path[0] != '\0')
    (void)ink_reader_state_note_open(
        state, state->open_book_path, NULL, state->open_book_page,
        state->open_book_chapter, state->open_book_total_pages_snapshot, NULL);
}

static bool decode_v1(const uint8_t *bytes, ink_reader_state_t *state) {
  const uint8_t *src = bytes + DISK_HEADER_SIZE;
  if (!valid_bool(src[0])) return false;
  state->has_open_book = src[0] != 0U;
  state->open_book_kind = (ink_reader_book_kind_t)read_u32(src + 4);
  state->open_book_page = read_u32(src + 8);
  if (!decode_text(state->open_book_path, sizeof(state->open_book_path),
                   src + V1_OPEN_PATH_OFFSET,
                   INK_READER_STATE_PATH_LENGTH + 1U))
    return false;
  if (state->open_book_kind != INK_READER_BOOK_KIND_XTC)
    ink_reader_state_clear_open(state);
  return !state->has_open_book || state->open_book_path[0] != '\0';
}

static bool decode_v2(const uint8_t *bytes, ink_reader_state_t *state) {
  const uint8_t *src = bytes + DISK_HEADER_SIZE;
  if (!decode_common(src, state)) return false;
  if (state->open_book_kind != INK_READER_BOOK_KIND_XTC) {
    ink_reader_state_clear_open(state);
  } else if (state->open_book_path[0] != '\0') {
    (void)ink_reader_state_set_progress(
        state, state->open_book_path, state->open_book_page,
        state->open_book_chapter, state->open_book_total_pages_snapshot);
  }
  if (state->has_open_book && state->open_book_path[0] == '\0') return false;
  migrate_open(state);
  return true;
}

static bool decode_v3(const uint8_t *bytes, ink_reader_state_t *state) {
  const uint8_t *src = bytes + DISK_HEADER_SIZE;
  if (!decode_common(src, state) ||
      !decode_progress(src + PROGRESS_OFFSET, state) ||
      !decode_bookmarks(src + BOOKMARK_OFFSET, state))
    return false;
  if (state->open_book_kind != INK_READER_BOOK_KIND_XTC)
    ink_reader_state_clear_open(state);
  if (state->has_open_book && state->open_book_path[0] == '\0') return false;
  migrate_progress(state);
  migrate_open(state);
  return true;
}

static bool decode_v4(const uint8_t *bytes, ink_reader_state_t *state) {
  const uint8_t *src = bytes + DISK_HEADER_SIZE;
  if (!decode_common(src, state) ||
      !decode_progress(src + PROGRESS_OFFSET, state) ||
      !decode_bookmarks(src + BOOKMARK_OFFSET, state) ||
      !decode_bookshelf(src + BOOKSHELF_OFFSET, state))
    return false;
  state->recent_order_counter = read_u32(src + RECENT_COUNTER_OFFSET);
  if (state->open_book_kind != INK_READER_BOOK_KIND_NONE &&
      state->open_book_kind != INK_READER_BOOK_KIND_XTC)
    ink_reader_state_clear_open(state);
  if (state->has_open_book &&
      state->open_book_kind == INK_READER_BOOK_KIND_XTC) {
    if (state->open_book_path[0] == '\0') return false;
    (void)ink_reader_state_set_progress(
        state, state->open_book_path, state->open_book_page,
        state->open_book_chapter, state->open_book_total_pages_snapshot);
  }
  migrate_progress(state);
  migrate_open(state);
  return true;
}

static bool encode_common(uint8_t *dst, const ink_reader_state_t *state) {
  if (state->open_book_page > UINT32_MAX ||
      state->open_book_chapter > UINT32_MAX ||
      state->open_book_total_pages_snapshot > UINT32_MAX)
    return false;
  dst[0] = state->has_open_book ? 1U : 0U;
  write_u32(dst + 4, (uint32_t)state->open_book_kind);
  write_u32(dst + 8, (uint32_t)state->open_book_page);
  write_u32(dst + 12, (uint32_t)state->open_book_chapter);
  write_u32(dst + 16, (uint32_t)state->open_book_total_pages_snapshot);
  return encode_text(dst + COMMON_OPEN_PATH_OFFSET,
                     INK_READER_STATE_PATH_LENGTH + 1U,
                     state->open_book_path);
}

static bool encode_progress(uint8_t *dst, const ink_reader_state_t *state) {
  for (size_t i = 0; i < INK_READER_PROGRESS_CAPACITY; ++i) {
    const ink_reader_progress_entry_t *entry = &state->progress[i];
    if (!entry->used) continue;
    if (entry->book_kind != INK_READER_BOOK_KIND_XTC ||
        entry->page_index > UINT32_MAX || entry->chapter_index > UINT32_MAX ||
        entry->total_pages_snapshot > UINT32_MAX)
      return false;
    uint8_t *wire = dst + i * PROGRESS_DISK_SIZE;
    wire[0] = 1U;
    write_u32(wire + 4, INK_READER_BOOK_KIND_XTC);
    write_u32(wire + 8, (uint32_t)entry->page_index);
    write_u32(wire + 12, (uint32_t)entry->chapter_index);
    write_u32(wire + 16, (uint32_t)entry->total_pages_snapshot);
    if (!encode_text(wire + 20, INK_READER_STATE_PATH_LENGTH + 1U,
                     entry->book_path))
      return false;
  }
  return true;
}

static bool encode_bookmarks(uint8_t *dst, const ink_reader_state_t *state) {
  for (size_t i = 0; i < INK_READER_BOOKMARK_CAPACITY; ++i) {
    const ink_reader_bookmark_t *entry = &state->bookmarks[i];
    if (!entry->used) continue;
    if (entry->book_kind != INK_READER_BOOK_KIND_XTC ||
        entry->page_index > UINT32_MAX || entry->chapter_index > UINT32_MAX ||
        entry->total_pages_snapshot > UINT32_MAX)
      return false;
    uint8_t *wire = dst + i * BOOKMARK_DISK_SIZE;
    wire[0] = 1U;
    write_u32(wire + 4, INK_READER_BOOK_KIND_XTC);
    write_u32(wire + 8, (uint32_t)entry->page_index);
    write_u32(wire + 12, (uint32_t)entry->chapter_index);
    write_u32(wire + 16, (uint32_t)entry->total_pages_snapshot);
    if (!encode_text(wire + 20, INK_READER_STATE_PATH_LENGTH + 1U,
                     entry->book_path) ||
        !encode_text(wire + 276, INK_READER_BOOKMARK_TITLE_LENGTH + 1U,
                     entry->chapter_title) ||
        !encode_text(wire + 357, INK_READER_BOOKMARK_TIME_LENGTH + 1U,
                     entry->timestamp_text))
      return false;
  }
  return true;
}

static bool encode_bookshelf(uint8_t *dst, const ink_reader_state_t *state) {
  for (size_t i = 0; i < INK_READER_BOOKSHELF_CAPACITY; ++i) {
    const ink_reader_bookshelf_entry_t *entry = &state->bookshelf[i];
    if (!entry->used) continue;
    if (entry->book_kind != INK_READER_BOOK_KIND_XTC ||
        entry->page_index > UINT32_MAX || entry->chapter_index > UINT32_MAX ||
        entry->total_pages_snapshot > UINT32_MAX)
      return false;
    uint8_t *wire = dst + i * BOOKSHELF_DISK_SIZE;
    wire[0] = 1U;
    wire[1] = entry->has_opened ? 1U : 0U;
    wire[2] = entry->is_favorite ? 1U : 0U;
    write_u32(wire + 4, INK_READER_BOOK_KIND_XTC);
    write_u32(wire + 8, entry->recent_order);
    write_u32(wire + 12, (uint32_t)entry->page_index);
    write_u32(wire + 16, (uint32_t)entry->chapter_index);
    write_u32(wire + 20, (uint32_t)entry->total_pages_snapshot);
    if (!encode_text(wire + 24, INK_READER_STATE_PATH_LENGTH + 1U,
                     entry->book_path) ||
        !encode_text(wire + 280, INK_READER_BOOK_TITLE_LENGTH + 1U,
                     entry->title) ||
        !encode_text(wire + 361, INK_READER_BOOK_TITLE_LENGTH + 1U,
                     entry->chapter_title))
      return false;
  }
  return true;
}

static bool encode_v4(uint8_t *bytes, const ink_reader_state_t *state) {
  write_u32(bytes, STATE_MAGIC);
  write_u16(bytes + 4, STATE_VERSION);
  uint8_t *dst = bytes + DISK_HEADER_SIZE;
  if (!encode_common(dst, state) ||
      !encode_progress(dst + PROGRESS_OFFSET, state) ||
      !encode_bookmarks(dst + BOOKMARK_OFFSET, state) ||
      !encode_bookshelf(dst + BOOKSHELF_OFFSET, state))
    return false;
  write_u32(dst + RECENT_COUNTER_OFFSET, state->recent_order_counter);
  return true;
}

ink_reader_state_result_t ink_reader_state_load(const char *path,
                                                ink_reader_state_t *state) {
  if (!path || path[0] == '\0' || !state)
    return INK_READER_STATE_INVALID_ARGUMENT;
  FILE *file = fopen(path, "rb");
  if (!file)
    return errno == ENOENT ? INK_READER_STATE_NOT_FOUND
                           : INK_READER_STATE_IO_ERROR;
  uint8_t header[DISK_HEADER_SIZE];
  if (fread(header, 1, sizeof(header), file) != sizeof(header)) {
    fclose(file);
    return INK_READER_STATE_INVALID_DATA;
  }
  if (read_u32(header) != STATE_MAGIC) {
    fclose(file);
    return INK_READER_STATE_INVALID_DATA;
  }
  const uint16_t version = read_u16(header + 4);
  const size_t expected_size =
      version == 1U   ? DISK_V1_SIZE
      : version == 2U ? DISK_V2_SIZE
      : version == 3U ? DISK_V3_SIZE
      : version == 4U ? DISK_V4_SIZE
                      : 0U;
  if (expected_size == 0U || fseek(file, 0, SEEK_END) != 0 ||
      ftell(file) != (long)expected_size || fseek(file, 0, SEEK_SET) != 0) {
    fclose(file);
    return INK_READER_STATE_INVALID_DATA;
  }
  uint8_t *bytes = (uint8_t *)malloc(expected_size);
  ink_reader_state_t *decoded =
      (ink_reader_state_t *)calloc(1, sizeof(*decoded));
  if (!bytes || !decoded) {
    free(bytes);
    free(decoded);
    fclose(file);
    return INK_READER_STATE_NO_MEMORY;
  }
  const bool read_ok = fread(bytes, 1, expected_size, file) == expected_size;
  const bool close_ok = fclose(file) == 0;
  bool decoded_ok = false;
  if (read_ok && close_ok) {
    decoded_ok = version == 1U   ? decode_v1(bytes, decoded)
                 : version == 2U ? decode_v2(bytes, decoded)
                 : version == 3U ? decode_v3(bytes, decoded)
                                  : decode_v4(bytes, decoded);
  }
  free(bytes);
  if (!decoded_ok) {
    free(decoded);
    return read_ok && close_ok ? INK_READER_STATE_INVALID_DATA
                               : INK_READER_STATE_IO_ERROR;
  }
  *state = *decoded;
  free(decoded);
  return INK_READER_STATE_OK;
}

static int make_directory(const char *path) {
#ifdef _WIN32
  return _mkdir(path);
#else
  return mkdir(path, 0777);
#endif
}

static bool ensure_parent_directory(const char *path) {
  char parent[TEMP_PATH_CAPACITY];
  if (snprintf(parent, sizeof(parent), "%s", path) >= (int)sizeof(parent))
    return false;
  char *last_slash = strrchr(parent, '/');
  char *last_backslash = strrchr(parent, '\\');
  if (!last_slash || (last_backslash && last_backslash > last_slash))
    last_slash = last_backslash;
  if (!last_slash) return true;
  *last_slash = '\0';
  for (char *cursor = parent + 1; *cursor; ++cursor) {
    if (*cursor != '/' && *cursor != '\\') continue;
    if (cursor == parent + 2 && parent[1] == ':') continue;
    const char separator = *cursor;
    *cursor = '\0';
    if (make_directory(parent) != 0 && errno != EEXIST) return false;
    *cursor = separator;
  }
  return make_directory(parent) == 0 || errno == EEXIST;
}

bool ink_reader_state_save(const char *path, const ink_reader_state_t *state) {
  if (!path || path[0] == '\0' || !state || !ensure_parent_directory(path))
    return false;
  char temporary[TEMP_PATH_CAPACITY];
  if (snprintf(temporary, sizeof(temporary), "%s.tmp", path) >=
      (int)sizeof(temporary))
    return false;
  uint8_t *bytes = (uint8_t *)calloc(1, DISK_V4_SIZE);
  if (!bytes) return false;
  if (!encode_v4(bytes, state)) {
    free(bytes);
    return false;
  }
  FILE *file = fopen(temporary, "wb");
  if (!file) {
    free(bytes);
    return false;
  }
  const bool written = fwrite(bytes, 1, DISK_V4_SIZE, file) == DISK_V4_SIZE;
  free(bytes);
  const bool closed = fclose(file) == 0;
  if (!written || !closed) {
    remove(temporary);
    return false;
  }
#ifdef _WIN32
  (void)remove(path);
#endif
  if (rename(temporary, path) != 0) {
    remove(temporary);
    return false;
  }
  return true;
}
