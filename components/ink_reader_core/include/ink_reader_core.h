#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define INK_READER_PATH_MAX 320
#define INK_READER_PAGE_SIZE (480 * 800 / 8)

typedef struct {
  uint64_t offset;
  uint32_t encoded_size;
  uint16_t width;
  uint16_t height;
} ink_reader_page_t;
typedef struct {
  FILE *file;
  char path[INK_READER_PATH_MAX];
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
bool ink_reader_find_first_book(char *path, size_t path_size);
ink_reader_scan_result_t ink_reader_open_first_book(
    ink_reader_book_t *book, char *candidate_path, size_t path_size);
bool ink_reader_book_open(ink_reader_book_t *book, const char *path);
bool ink_reader_book_load_current(const ink_reader_book_t *book,
                                  uint8_t *buffer, size_t length);
bool ink_reader_book_next(ink_reader_book_t *book);
bool ink_reader_book_previous(ink_reader_book_t *book);
bool ink_reader_core_self_test(void);
