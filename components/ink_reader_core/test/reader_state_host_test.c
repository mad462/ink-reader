#include <direct.h>
#include <errno.h>
#include <io.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ink_reader_core.h"
#include "ink_reader_state.h"

#ifdef rename
#undef rename
#endif
int rename(const char *old_path, const char *new_path);

#ifndef INK_READER_TEST_ROOT
#define INK_READER_TEST_ROOT "/sdcard"
#endif

#define TEST_ROOT INK_READER_TEST_ROOT
#define TEST_BOOKS TEST_ROOT "/books"
#define TEST_STATE_DIR TEST_ROOT "/.ink-reader"
#define TEST_STATE_PATH TEST_STATE_DIR "/state.bin"
#define LEGACY_V4_PATH TEST_ROOT "/legacy-v4.bin"
#define LEGACY_OLD_PATH TEST_ROOT "/legacy-old.bin"
#define TRUNCATED_PATH TEST_ROOT "/truncated.bin"
#define UNKNOWN_PATH TEST_ROOT "/unknown.bin"
#define BAD_MAGIC_PATH TEST_ROOT "/bad-magic.bin"
#define DIRECTORY_TARGET TEST_ROOT "/directory-target"

static int rename_failure_enabled;
static const char *rename_failure_target;

int ink_reader_test_rename(const char *old_path, const char *new_path) {
  const size_t length = old_path ? strlen(old_path) : 0U;
  if (rename_failure_enabled && rename_failure_target && new_path &&
      strcmp(new_path, rename_failure_target) == 0 && length >= 4U &&
      strcmp(old_path + length - 4U, ".tmp") == 0) {
    errno = EACCES;
    return -1;
  }
  return rename(old_path, new_path);
}

#define LEGACY_V4_FILE_SIZE 23616u
#define LEGACY_STATE_OFFSET 8u
#define LEGACY_BOOKSHELF_OFFSET 9400u
#define LEGACY_BOOKSHELF_ENTRY_SIZE 444u

static void put16(uint8_t *p, uint16_t value) {
  p[0] = (uint8_t)value;
  p[1] = (uint8_t)(value >> 8);
}

static void put32(uint8_t *p, uint32_t value) {
  put16(p, (uint16_t)value);
  put16(p + 2, (uint16_t)(value >> 16));
}

static int write_file(const char *path, const void *data, size_t size) {
  FILE *file = fopen(path, "wb");
  if (!file) return 0;
  const int ok = fwrite(data, 1, size, file) == size;
  return fclose(file) == 0 && ok;
}

static int touch_file(const char *path) {
  static const uint8_t byte = 0;
  return write_file(path, &byte, sizeof(byte));
}

static void remove_catalog_files(void) {
  char path[INK_READER_PATH_MAX];
  for (int i = 0; i < 35; ++i) {
    snprintf(path, sizeof(path), TEST_BOOKS "/Book%02d.%s", i,
             (i & 1) ? "xtch" : "XTC");
    remove(path);
  }
  remove(TEST_BOOKS "/ignore.txt");
  remove(TEST_BOOKS "/\xe4\xb8\xad\xe6\x96\x87.xtc");
  remove(TEST_BOOKS "/zeta.xtc");
  remove(TEST_BOOKS "/Alpha.xtch");
  remove(TEST_BOOKS "/beta.XTC");
}

static void cleanup_fixture(void) {
  remove_catalog_files();
  remove(TEST_STATE_PATH ".tmp");
  remove(TEST_STATE_PATH ".bak");
  remove(TEST_STATE_PATH);
  remove(LEGACY_V4_PATH);
  remove(LEGACY_OLD_PATH);
  remove(TRUNCATED_PATH);
  remove(UNKNOWN_PATH);
  remove(BAD_MAGIC_PATH);
  remove(DIRECTORY_TARGET ".tmp");
  remove(DIRECTORY_TARGET ".bak");
  _rmdir(DIRECTORY_TARGET);
  _rmdir(TEST_STATE_DIR);
  _rmdir(TEST_BOOKS);
  _rmdir(TEST_ROOT);
}

static int test_catalog(void) {
  ink_reader_catalog_t catalog = {0};
  char path[INK_READER_PATH_MAX];

  for (int i = 34; i >= 0; --i) {
    snprintf(path, sizeof(path), TEST_BOOKS "/Book%02d.%s", i,
             (i & 1) ? "xtch" : "XTC");
    if (!touch_file(path)) return 0;
  }
  if (!touch_file(TEST_BOOKS "/ignore.txt") ||
      !ink_reader_catalog_load(&catalog))
    return 0;
  if (ink_reader_catalog_count(&catalog) != INK_READER_CATALOG_CAPACITY)
    return 0;
  for (size_t i = 0; i < INK_READER_CATALOG_CAPACITY; ++i) {
    const ink_reader_catalog_item_t *item = ink_reader_catalog_at(&catalog, i);
    char expected[32];
    snprintf(expected, sizeof(expected), "Book%02u.%s", (unsigned)i,
             (i & 1) ? "xtch" : "XTC");
    if (!item || strcmp(item->name, expected) != 0 ||
        strstr(item->path, expected) == NULL)
      return 0;
  }
  if (ink_reader_catalog_at(&catalog, INK_READER_CATALOG_CAPACITY) != NULL)
    return 0;
  ink_reader_catalog_free(&catalog);

  remove_catalog_files();
  if (!touch_file(TEST_BOOKS "/zeta.xtc") ||
      !touch_file(TEST_BOOKS "/Alpha.xtch") ||
      !touch_file(TEST_BOOKS "/beta.XTC") ||
      !touch_file(TEST_BOOKS "/\xe4\xb8\xad\xe6\x96\x87.xtc") ||
      !ink_reader_catalog_load(&catalog) ||
      ink_reader_catalog_count(&catalog) != 4U)
    return 0;
  if (strcmp(ink_reader_catalog_at(&catalog, 0)->name, "Alpha.xtch") != 0 ||
      strcmp(ink_reader_catalog_at(&catalog, 1)->name, "beta.XTC") != 0 ||
      strcmp(ink_reader_catalog_at(&catalog, 2)->name, "zeta.xtc") != 0)
    return 0;
  const ink_reader_catalog_item_t *utf8 = ink_reader_catalog_at(&catalog, 3);
  if (!utf8 || strcmp(utf8->name, "\xe4\xb8\xad\xe6\x96\x87.xtc") != 0 ||
      strstr(utf8->path, "\xe4\xb8\xad\xe6\x96\x87.xtc") == NULL)
    return 0;
  ink_reader_catalog_free(&catalog);
  remove_catalog_files();
  return 1;
}

static int test_recent_favorite_and_progress(void) {
  ink_reader_state_t state;
  size_t index = 0;
  size_t page = 0;
  size_t chapter = 0;
  size_t total = 0;

  ink_reader_state_default(&state);
  if (!ink_reader_state_note_open(&state, "/sdcard/books/a.xtc", "A", 3, 1,
                                  40, "One") ||
      !ink_reader_state_note_open(&state, "/sdcard/books/b.xtc", "B", 8, 2,
                                  50, "Two") ||
      !ink_reader_state_find_bookshelf(&state, "/sdcard/books/a.xtc", &index))
    return 0;
  const ink_reader_bookshelf_entry_t *a =
      ink_reader_state_bookshelf_at(&state, index);
  if (!a || !a->has_opened || a->recent_order != 1U) return 0;
  if (!ink_reader_state_find_bookshelf(&state, "/sdcard/books/b.xtc", &index))
    return 0;
  const ink_reader_bookshelf_entry_t *b =
      ink_reader_state_bookshelf_at(&state, index);
  if (!b || !b->has_opened || b->recent_order != 2U) return 0;

  if (!ink_reader_state_set_favorite(&state, "/sdcard/books/fav.xtc", "Fav",
                                     true) ||
      !ink_reader_state_find_bookshelf(&state, "/sdcard/books/fav.xtc",
                                       &index))
    return 0;
  const ink_reader_bookshelf_entry_t *favorite =
      ink_reader_state_bookshelf_at(&state, index);
  if (!favorite || !favorite->is_favorite || favorite->has_opened ||
      favorite->recent_order != 0U)
    return 0;
  if (!ink_reader_state_set_favorite(&state, "/sdcard/books/fav.xtc", "Fav",
                                     false) ||
      ink_reader_state_bookshelf_at(&state, index)->is_favorite)
    return 0;

  if (!ink_reader_state_set_progress(&state, "/sdcard/books/a.xtc", 19, 4,
                                     120) ||
      !ink_reader_state_save(TEST_STATE_PATH, &state))
    return 0;
  ink_reader_state_default(&state);
  if (ink_reader_state_load(TEST_STATE_PATH, &state) != INK_READER_STATE_OK ||
      !ink_reader_state_find_progress(&state, "/sdcard/books/a.xtc", &page,
                                      &chapter, &total) ||
      page != 19U || chapter != 4U || total != 120U)
    return 0;
  return 1;
}

static int test_bookmarks(void) {
  ink_reader_state_t state;
  char timestamp[INK_READER_BOOKMARK_TIME_LENGTH + 1];
  size_t index = 0;

  ink_reader_state_default(&state);
  for (size_t i = 0; i < INK_READER_BOOKMARK_CAPACITY; ++i) {
    snprintf(timestamp, sizeof(timestamp), "2026-07-13 10:%02u",
             (unsigned)i);
    if (!ink_reader_state_bookmark_add_or_replace(
            &state, "/sdcard/books/a.xtc", i, i / 2, 100, "Chapter",
            timestamp))
      return 0;
  }
  if (ink_reader_state_bookmark_count(&state, "/sdcard/books/a.xtc") !=
      INK_READER_BOOKMARK_CAPACITY)
    return 0;

  if (!ink_reader_state_bookmark_add_or_replace(
          &state, "/sdcard/books/a.xtc", 5, 9, 101, "Replacement",
          "2026-07-13 11:00") ||
      !ink_reader_state_bookmark_find(&state, "/sdcard/books/a.xtc", 5,
                                      &index))
    return 0;
  const ink_reader_bookmark_t *replacement =
      ink_reader_state_bookmark_at(&state, index);
  if (!replacement || replacement->chapter_index != 9U ||
      strcmp(replacement->chapter_title, "Replacement") != 0)
    return 0;

  if (!ink_reader_state_bookmark_add_or_replace(
          &state, "/sdcard/books/a.xtc", 99, 10, 101, "Newest",
          "2026-07-13 12:00") ||
      ink_reader_state_bookmark_find(&state, "/sdcard/books/a.xtc", 0, NULL) ||
      ink_reader_state_bookmark_count(&state, "/sdcard/books/a.xtc") !=
          INK_READER_BOOKMARK_CAPACITY)
    return 0;
  if (!ink_reader_state_bookmark_find(&state, "/sdcard/books/a.xtc", 1,
                                      &index) ||
      !ink_reader_state_bookmark_overwrite(
          &state, index, "/sdcard/books/b.xtc", 7, 3, 80, "Moved",
          "2026-07-13 13:00") ||
      ink_reader_state_bookmark_find(&state, "/sdcard/books/a.xtc", 1, NULL) ||
      !ink_reader_state_bookmark_find(&state, "/sdcard/books/b.xtc", 7,
                                      NULL) ||
      !ink_reader_state_bookmark_remove(&state, "/sdcard/books/b.xtc", 7) ||
      ink_reader_state_bookmark_find(&state, "/sdcard/books/b.xtc", 7, NULL))
    return 0;
  return 1;
}

static int test_replacement_failure_and_backup_recovery(void) {
  ink_reader_state_t state;
  ink_reader_state_t restored;

  remove(TEST_STATE_PATH ".tmp");
  remove(TEST_STATE_PATH ".bak");
  ink_reader_state_default(&state);
  state.recent_order_counter = 10U;
  if (!ink_reader_state_save(TEST_STATE_PATH, &state)) return 0;

  state.recent_order_counter = 20U;
  rename_failure_target = TEST_STATE_PATH;
  rename_failure_enabled = 1;
  const bool save_result = ink_reader_state_save(TEST_STATE_PATH, &state);
  rename_failure_enabled = 0;
  rename_failure_target = NULL;
  if (save_result) return 0;

  ink_reader_state_default(&restored);
  if (ink_reader_state_load(TEST_STATE_PATH, &restored) !=
          INK_READER_STATE_OK ||
      restored.recent_order_counter != 10U)
    return 0;

  remove(TEST_STATE_PATH ".bak");
  if (rename(TEST_STATE_PATH, TEST_STATE_PATH ".bak") != 0 ||
      _access(TEST_STATE_PATH, 0) == 0)
    return 0;
  ink_reader_state_default(&restored);
  if (ink_reader_state_load(TEST_STATE_PATH, &restored) !=
          INK_READER_STATE_OK ||
      restored.recent_order_counter != 10U)
    return 0;

  if (_mkdir(DIRECTORY_TARGET) != 0 ||
      ink_reader_state_save(DIRECTORY_TARGET, &state) ||
      _access(DIRECTORY_TARGET, 0) != 0 ||
      _access(DIRECTORY_TARGET ".bak", 0) == 0)
    return 0;
  return 1;
}

static int write_legacy_v4_fixture(void) {
  uint8_t *bytes = (uint8_t *)calloc(1, LEGACY_V4_FILE_SIZE);
  if (!bytes) return 0;
  put32(bytes, 0x49534150U);
  put16(bytes + 4, 4U);

  uint8_t *state = bytes + LEGACY_STATE_OFFSET;
  state[0] = 1;
  put32(state + 4, 3U);
  put32(state + 8, 14U);
  put32(state + 12, 2U);
  put32(state + 16, 200U);
  memcpy(state + 20, "/sdcard/books/open.xtc",
         sizeof("/sdcard/books/open.xtc"));

  uint8_t *progress = state + 276U;
  progress[0] = 1;
  put32(progress + 4, 3U);
  put32(progress + 8, 23U);
  put32(progress + 12, 4U);
  put32(progress + 16, 300U);
  memcpy(progress + 20, "/sdcard/books/progress.xtc",
         sizeof("/sdcard/books/progress.xtc"));

  const size_t shelf = LEGACY_STATE_OFFSET + LEGACY_BOOKSHELF_OFFSET +
                       3U * LEGACY_BOOKSHELF_ENTRY_SIZE;
  bytes[shelf] = 1;
  bytes[shelf + 1] = 1;
  bytes[shelf + 2] = 1;
  put32(bytes + shelf + 4, 3U);
  put32(bytes + shelf + 8, 77U);
  put32(bytes + shelf + 12, 22U);
  put32(bytes + shelf + 16, 4U);
  put32(bytes + shelf + 20, 300U);
  memcpy(bytes + shelf + 24, "/sdcard/books/legacy.xtc", 25);
  memcpy(bytes + shelf + 280, "Legacy Title", 12);
  memcpy(bytes + shelf + 361, "Legacy Chapter", 14);
  put32(bytes + LEGACY_STATE_OFFSET + 9396U, 77U);

  const int ok = write_file(LEGACY_V4_PATH, bytes, LEGACY_V4_FILE_SIZE);
  free(bytes);
  return ok;
}

static int write_legacy_old_fixture(uint16_t version) {
  const size_t size = version == 1U ? 276U : version == 2U ? 284U : 9404U;
  uint8_t *bytes = (uint8_t *)calloc(1, size);
  if (!bytes) return 0;
  put32(bytes, 0x49534150U);
  put16(bytes + 4, version);
  uint8_t *state = bytes + 8;

  if (version == 1U) {
    state[0] = 1;
    put32(state + 4, 3U);
    put32(state + 8, 7U);
    memcpy(state + 12, "/sdcard/books/v1.xtc", 21);
  } else if (version == 2U) {
    state[0] = 1;
    put32(state + 4, 3U);
    put32(state + 8, 8U);
    put32(state + 12, 2U);
    put32(state + 16, 80U);
    memcpy(state + 20, "/sdcard/books/v2.xtc", 21);
  } else {
    uint8_t *progress = state + 276;
    progress[0] = 1;
    put32(progress + 4, 3U);
    put32(progress + 8, 9U);
    put32(progress + 12, 3U);
    put32(progress + 16, 90U);
    memcpy(progress + 20, "/sdcard/books/v3.xtc", 21);

    uint8_t *bookmark = state + 4692;
    bookmark[0] = 1;
    put32(bookmark + 4, 3U);
    put32(bookmark + 8, 10U);
    put32(bookmark + 12, 4U);
    put32(bookmark + 16, 90U);
    memcpy(bookmark + 20, "/sdcard/books/v3.xtc", 21);
    memcpy(bookmark + 276, "V3 Chapter", 11);
    memcpy(bookmark + 357, "2026-07-13 09:00", 17);
  }

  const int ok = write_file(LEGACY_OLD_PATH, bytes, size);
  free(bytes);
  return ok;
}

static int test_legacy_v1_to_v3(void) {
  ink_reader_state_t state;
  size_t page = 0;
  size_t chapter = 0;
  size_t total = 0;

  if (!write_legacy_old_fixture(1U)) return 0;
  ink_reader_state_default(&state);
  if (ink_reader_state_load(LEGACY_OLD_PATH, &state) != INK_READER_STATE_OK ||
      !state.has_open_book || state.open_book_page != 7U ||
      strcmp(state.open_book_path, "/sdcard/books/v1.xtc") != 0)
    return 0;

  if (!write_legacy_old_fixture(2U)) return 0;
  ink_reader_state_default(&state);
  if (ink_reader_state_load(LEGACY_OLD_PATH, &state) != INK_READER_STATE_OK ||
      !ink_reader_state_find_progress(&state, "/sdcard/books/v2.xtc", &page,
                                      &chapter, &total) ||
      page != 8U || chapter != 2U || total != 80U ||
      !ink_reader_state_find_bookshelf(&state, "/sdcard/books/v2.xtc", NULL))
    return 0;

  if (!write_legacy_old_fixture(3U)) return 0;
  ink_reader_state_default(&state);
  if (ink_reader_state_load(LEGACY_OLD_PATH, &state) != INK_READER_STATE_OK ||
      !ink_reader_state_find_progress(&state, "/sdcard/books/v3.xtc", &page,
                                      &chapter, &total) ||
      page != 9U || chapter != 3U || total != 90U ||
      !ink_reader_state_find_bookshelf(&state, "/sdcard/books/v3.xtc", NULL) ||
      !ink_reader_state_bookmark_find(&state, "/sdcard/books/v3.xtc", 10,
                                      NULL))
    return 0;
  return 1;
}

static int test_legacy_and_transactional_load(void) {
  ink_reader_state_t state;
  ink_reader_state_t before;
  size_t index = 0;
  static const uint8_t truncated[7] = {0x50, 0x41, 0x53, 0x49, 4, 0, 0};
  uint8_t unknown[8] = {0};
  uint8_t bad_magic[8] = {0};

  if (!write_legacy_v4_fixture()) return 0;
  ink_reader_state_default(&state);
  if (ink_reader_state_load(LEGACY_V4_PATH, &state) != INK_READER_STATE_OK ||
      state.recent_order_counter != 77U ||
      !ink_reader_state_find_bookshelf(&state, "/sdcard/books/legacy.xtc",
                                       &index) ||
      ink_reader_state_find_bookshelf(&state, "/sdcard/books/open.xtc",
                                      NULL) ||
      ink_reader_state_find_bookshelf(&state, "/sdcard/books/progress.xtc",
                                      NULL))
    return 0;
  if (!state.has_open_book ||
      strcmp(state.open_book_path, "/sdcard/books/open.xtc") != 0 ||
      !state.progress[0].used || state.progress[0].page_index != 23U ||
      state.progress[0].chapter_index != 4U ||
      state.progress[0].total_pages_snapshot != 300U ||
      strcmp(state.progress[0].book_path, "/sdcard/books/progress.xtc") != 0 ||
      ink_reader_state_find_progress(&state, "/sdcard/books/open.xtc", NULL,
                                     NULL, NULL))
    return 0;
  for (size_t i = 1; i < INK_READER_PROGRESS_CAPACITY; ++i)
    if (state.progress[i].used) return 0;
  const ink_reader_bookshelf_entry_t *entry =
      ink_reader_state_bookshelf_at(&state, index);
  if (!entry || !entry->has_opened || !entry->is_favorite ||
      entry->recent_order != 77U || entry->page_index != 22U ||
      entry->chapter_index != 4U || entry->total_pages_snapshot != 300U ||
      strcmp(entry->title, "Legacy Title") != 0 ||
      strcmp(entry->chapter_title, "Legacy Chapter") != 0)
    return 0;

  if (!ink_reader_state_save(TEST_STATE_PATH, &state)) return 0;
  FILE *saved = fopen(TEST_STATE_PATH, "rb");
  if (!saved || fseek(saved, 0, SEEK_END) != 0 ||
      ftell(saved) != LEGACY_V4_FILE_SIZE || fclose(saved) != 0)
    return 0;
  ink_reader_state_default(&state);
  if (ink_reader_state_load(TEST_STATE_PATH, &state) != INK_READER_STATE_OK ||
      !ink_reader_state_find_bookshelf(&state, "/sdcard/books/legacy.xtc",
                                       NULL))
    return 0;

  state.recent_order_counter = 0x12345678U;
  before = state;
  if (!write_file(TRUNCATED_PATH, truncated, sizeof(truncated)) ||
      ink_reader_state_load(TRUNCATED_PATH, &state) == INK_READER_STATE_OK ||
      memcmp(&state, &before, sizeof(state)) != 0)
    return 0;
  put32(unknown, 0x49534150U);
  put16(unknown + 4, 99U);
  if (!write_file(UNKNOWN_PATH, unknown, sizeof(unknown)) ||
      ink_reader_state_load(UNKNOWN_PATH, &state) == INK_READER_STATE_OK ||
      memcmp(&state, &before, sizeof(state)) != 0)
    return 0;
  put32(bad_magic, 0xdeadbeefU);
  put16(bad_magic + 4, 4U);
  if (!write_file(BAD_MAGIC_PATH, bad_magic, sizeof(bad_magic)) ||
      ink_reader_state_load(BAD_MAGIC_PATH, &state) == INK_READER_STATE_OK ||
      memcmp(&state, &before, sizeof(state)) != 0)
    return 0;
  return 1;
}

int main(void) {
  int result = 1;
  if (_access(TEST_ROOT, 0) == 0) {
    fprintf(stderr, "%s already exists; refusing to modify it\n", TEST_ROOT);
    return 2;
  }
  if (_mkdir(TEST_ROOT) != 0 || _mkdir(TEST_BOOKS) != 0) {
    fprintf(stderr, "fixture directory setup failed\n");
    cleanup_fixture();
    return 2;
  }

  if (!test_catalog()) {
    fprintf(stderr, "catalog tests failed\n");
    goto cleanup;
  }
  if (!test_recent_favorite_and_progress()) {
    fprintf(stderr, "recent/favorite/progress tests failed\n");
    goto cleanup;
  }
  if (!test_bookmarks()) {
    fprintf(stderr, "bookmark tests failed\n");
    goto cleanup;
  }
  if (!test_replacement_failure_and_backup_recovery()) {
    fprintf(stderr, "replacement/backup recovery tests failed\n");
    goto cleanup;
  }
  if (!test_legacy_v1_to_v3()) {
    fprintf(stderr, "legacy v1-v3 tests failed\n");
    goto cleanup;
  }
  if (!test_legacy_and_transactional_load()) {
    fprintf(stderr, "legacy/transaction tests failed\n");
    goto cleanup;
  }

  result = 0;
  puts("PASS: reader catalog and state host tests");

cleanup:
  cleanup_fixture();
  return result;
}
