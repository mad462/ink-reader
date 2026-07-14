#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define INK_READER_PATH_MAX 320
#define INK_READER_PAGE_SIZE (480 * 800 / 8)
#define INK_READER_CATALOG_CAPACITY 32
#define INK_READER_CATALOG_NAME_MAX 256

typedef struct {
  char path[INK_READER_PATH_MAX];
  char name[INK_READER_CATALOG_NAME_MAX];
} ink_reader_catalog_item_t;

typedef struct {
  ink_reader_catalog_item_t *items;
  size_t count;
} ink_reader_catalog_t;

typedef struct {
  uint64_t offset;
  uint32_t encoded_size;
  uint16_t width;
  uint16_t height;
} ink_reader_page_t;

typedef struct {
  char title[129];
  char author[65];
  char publisher[33];
  char language[17];
  uint32_t create_time;
  uint16_t cover_page;
  uint16_t chapter_count;
} ink_reader_metadata_t;

typedef struct {
  char title[81];
  uint16_t start_page;
  uint16_t end_page;
} ink_reader_chapter_t;

typedef struct {
  FILE *file;
  char path[INK_READER_PATH_MAX];
  ink_reader_metadata_t metadata;
  bool has_metadata;
  ink_reader_chapter_t *chapters;
  size_t chapter_count;
  ink_reader_page_t *pages;
  size_t page_count;
  size_t current_page;
} ink_reader_book_t;

typedef enum {
  INK_READER_SCAN_OK,
  INK_READER_SCAN_DIR_MISSING,
  INK_READER_SCAN_EMPTY,
  INK_READER_SCAN_FORMAT_ERROR,
  INK_READER_SCAN_IO_ERROR,
} ink_reader_scan_result_t;

void ink_reader_book_init(ink_reader_book_t *book);
void ink_reader_book_close(ink_reader_book_t *book);
bool ink_reader_catalog_load(ink_reader_catalog_t *catalog);
void ink_reader_catalog_free(ink_reader_catalog_t *catalog);
size_t ink_reader_catalog_count(const ink_reader_catalog_t *catalog);
const ink_reader_catalog_item_t *ink_reader_catalog_at(
    const ink_reader_catalog_t *catalog, size_t index);
bool ink_reader_find_first_book(char *path, size_t path_size);
ink_reader_scan_result_t ink_reader_open_first_book(
    ink_reader_book_t *book, char *candidate_path, size_t path_size);
bool ink_reader_book_open(ink_reader_book_t *book, const char *path);
bool ink_reader_book_load_current(const ink_reader_book_t *book,
                                  uint8_t *buffer, size_t length);
bool ink_reader_book_load_page(ink_reader_book_t *book, size_t page_index,
                               uint8_t *buffer, size_t length);
bool ink_reader_book_next(ink_reader_book_t *book);
bool ink_reader_book_previous(ink_reader_book_t *book);
size_t ink_reader_book_chapter_count(const ink_reader_book_t *book);
const ink_reader_chapter_t *ink_reader_book_chapter_at(
    const ink_reader_book_t *book, size_t chapter_index);
const ink_reader_chapter_t *ink_reader_book_chapter_for_page(
    const ink_reader_book_t *book, size_t page_index);
bool ink_reader_book_jump_to_chapter(ink_reader_book_t *book,
                                     size_t chapter_index);
bool ink_reader_core_self_test(void);
