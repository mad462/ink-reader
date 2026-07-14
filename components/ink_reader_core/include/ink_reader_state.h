#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef INK_READER_SCAN_ROOT
#define INK_READER_SCAN_ROOT "/sdcard"
#endif

#define INK_READER_STATE_PATH INK_READER_SCAN_ROOT "/.ink-reader/state.bin"
#define INK_READER_STATE_PATH_LENGTH 255
#define INK_READER_BOOKMARK_CAPACITY 12
#define INK_READER_PROGRESS_CAPACITY 16
#define INK_READER_BOOKSHELF_CAPACITY 32
#define INK_READER_BOOKMARK_TITLE_LENGTH 80
#define INK_READER_BOOKMARK_TIME_LENGTH 31
#define INK_READER_BOOK_TITLE_LENGTH 80

typedef enum {
  INK_READER_BOOK_KIND_NONE = 0,
  INK_READER_BOOK_KIND_XTC = 3,
} ink_reader_book_kind_t;

typedef struct {
  bool used;
  ink_reader_book_kind_t book_kind;
  size_t page_index;
  size_t chapter_index;
  size_t total_pages_snapshot;
  char book_path[INK_READER_STATE_PATH_LENGTH + 1];
} ink_reader_progress_entry_t;

typedef struct {
  bool used;
  ink_reader_book_kind_t book_kind;
  size_t page_index;
  size_t chapter_index;
  size_t total_pages_snapshot;
  char book_path[INK_READER_STATE_PATH_LENGTH + 1];
  char chapter_title[INK_READER_BOOKMARK_TITLE_LENGTH + 1];
  char timestamp_text[INK_READER_BOOKMARK_TIME_LENGTH + 1];
} ink_reader_bookmark_t;

typedef struct {
  bool used;
  bool has_opened;
  bool is_favorite;
  ink_reader_book_kind_t book_kind;
  uint32_t recent_order;
  size_t page_index;
  size_t chapter_index;
  size_t total_pages_snapshot;
  char book_path[INK_READER_STATE_PATH_LENGTH + 1];
  char title[INK_READER_BOOK_TITLE_LENGTH + 1];
  char chapter_title[INK_READER_BOOK_TITLE_LENGTH + 1];
} ink_reader_bookshelf_entry_t;

typedef struct {
  bool has_open_book;
  ink_reader_book_kind_t open_book_kind;
  size_t open_book_page;
  size_t open_book_chapter;
  size_t open_book_total_pages_snapshot;
  char open_book_path[INK_READER_STATE_PATH_LENGTH + 1];
  ink_reader_progress_entry_t progress[INK_READER_PROGRESS_CAPACITY];
  ink_reader_bookmark_t bookmarks[INK_READER_BOOKMARK_CAPACITY];
  uint32_t recent_order_counter;
  ink_reader_bookshelf_entry_t bookshelf[INK_READER_BOOKSHELF_CAPACITY];
} ink_reader_state_t;

typedef enum {
  INK_READER_STATE_OK = 0,
  INK_READER_STATE_INVALID_ARGUMENT,
  INK_READER_STATE_NOT_FOUND,
  INK_READER_STATE_IO_ERROR,
  INK_READER_STATE_INVALID_DATA,
  INK_READER_STATE_NO_MEMORY,
} ink_reader_state_result_t;

void ink_reader_state_default(ink_reader_state_t *state);
void ink_reader_state_clear_open(ink_reader_state_t *state);
void ink_reader_state_remember_open(ink_reader_state_t *state,
                                    const char *path, size_t page_index,
                                    size_t chapter_index,
                                    size_t total_pages_snapshot);
bool ink_reader_state_set_progress(ink_reader_state_t *state,
                                   const char *path, size_t page_index,
                                   size_t chapter_index,
                                   size_t total_pages_snapshot);
bool ink_reader_state_find_progress(const ink_reader_state_t *state,
                                    const char *path, size_t *page_index_out,
                                    size_t *chapter_index_out,
                                    size_t *total_pages_snapshot_out);
bool ink_reader_state_note_open(ink_reader_state_t *state, const char *path,
                                const char *title, size_t page_index,
                                size_t chapter_index,
                                size_t total_pages_snapshot,
                                const char *chapter_title);
bool ink_reader_state_set_favorite(ink_reader_state_t *state,
                                   const char *path, const char *title,
                                   bool is_favorite);
bool ink_reader_state_find_bookshelf(const ink_reader_state_t *state,
                                     const char *path, size_t *entry_index_out);
const ink_reader_bookshelf_entry_t *ink_reader_state_bookshelf_at(
    const ink_reader_state_t *state, size_t entry_index);
bool ink_reader_state_bookmark_add_or_replace(
    ink_reader_state_t *state, const char *path, size_t page_index,
    size_t chapter_index, size_t total_pages_snapshot,
    const char *chapter_title, const char *timestamp_text);
bool ink_reader_state_bookmark_overwrite(
    ink_reader_state_t *state, size_t bookmark_index, const char *path,
    size_t page_index, size_t chapter_index, size_t total_pages_snapshot,
    const char *chapter_title, const char *timestamp_text);
bool ink_reader_state_bookmark_remove(ink_reader_state_t *state,
                                       const char *path, size_t page_index);
bool ink_reader_state_bookmark_remove_at(ink_reader_state_t *state,
                                         size_t bookmark_index);
bool ink_reader_state_bookmark_find(const ink_reader_state_t *state,
                                    const char *path, size_t page_index,
                                    size_t *bookmark_index_out);
size_t ink_reader_state_bookmark_count(const ink_reader_state_t *state,
                                       const char *path);
const ink_reader_bookmark_t *ink_reader_state_bookmark_at(
    const ink_reader_state_t *state, size_t bookmark_index);
ink_reader_state_result_t ink_reader_state_load(const char *path,
                                                ink_reader_state_t *state);
bool ink_reader_state_save(const char *path, const ink_reader_state_t *state);
