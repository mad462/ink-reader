#include <direct.h>
#include <io.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ink_reader_core.h"

#ifndef INK_READER_TEST_ROOT
#define INK_READER_TEST_ROOT "/sdcard"
#endif

#define TEST_ROOT INK_READER_TEST_ROOT
#define TEST_BOOKS TEST_ROOT "/books"
#define BAD_BOOK TEST_BOOKS "/a_bad.xtc"
#define GOOD_BOOK TEST_BOOKS "/b_good.xtc"
#define PAGE_FAIL_BOOK TEST_BOOKS "/page_fail.bin"
#define XTH_GOOD_BOOK TEST_BOOKS "/xth_good.bin"
#define XTH_FAIL_BOOK TEST_BOOKS "/xth_fail.bin"

static void put16(uint8_t *p, uint16_t value) {
  p[0] = (uint8_t)value;
  p[1] = (uint8_t)(value >> 8);
}

static void put32(uint8_t *p, uint32_t value) {
  put16(p, (uint16_t)value);
  put16(p + 2, (uint16_t)(value >> 16));
}

static void put64(uint8_t *p, uint64_t value) {
  put32(p, (uint32_t)value);
  put32(p + 4, (uint32_t)(value >> 32));
}

static void make_page_header(uint8_t *header, const char *magic,
                             uint32_t data_size) {
  memset(header, 0, 22);
  memcpy(header, magic, 4);
  put16(header + 4, 480);
  put16(header + 6, 800);
  put32(header + 10, data_size);
}

static int write_bytes(FILE *file, size_t count, uint8_t value) {
  uint8_t bytes[256];
  memset(bytes, value, sizeof(bytes));
  while (count > 0) {
    const size_t chunk = count < sizeof(bytes) ? count : sizeof(bytes);
    if (fwrite(bytes, 1, chunk, file) != chunk) return 0;
    count -= chunk;
  }
  return 1;
}

static int write_single_page_book(const char *path, size_t payload_bytes) {
  uint8_t container[72] = {0};
  uint8_t page_header[22];
  FILE *file = fopen(path, "wb");
  if (!file) return 0;

  memcpy(container, "XTC\0", 4);
  put16(container + 4, 1);
  put16(container + 6, 1);
  put64(container + 24, 56);
  put64(container + 32, 72);
  put64(container + 56, 72);
  put32(container + 64, 22 + INK_READER_PAGE_SIZE);
  put16(container + 68, 480);
  put16(container + 70, 800);
  make_page_header(page_header, "XTG\0", INK_READER_PAGE_SIZE);

  const int ok =
      fwrite(container, 1, sizeof(container), file) == sizeof(container) &&
      fwrite(page_header, 1, sizeof(page_header), file) ==
          sizeof(page_header) &&
      write_bytes(file, payload_bytes, 0x00);
  return fclose(file) == 0 && ok;
}

static int write_page_failure_book(void) {
  uint8_t container[88] = {0};
  uint8_t page_header[22];
  const uint64_t first_offset = sizeof(container);
  const uint64_t second_offset = first_offset + 22 + INK_READER_PAGE_SIZE;
  FILE *file = fopen(PAGE_FAIL_BOOK, "wb");
  if (!file) return 0;

  memcpy(container, "XTC\0", 4);
  put16(container + 4, 1);
  put16(container + 6, 2);
  put64(container + 24, 56);
  put64(container + 32, 88);
  put64(container + 56, first_offset);
  put32(container + 64, 22 + INK_READER_PAGE_SIZE);
  put16(container + 68, 480);
  put16(container + 70, 800);
  put64(container + 72, second_offset);
  put32(container + 80, 22 + INK_READER_PAGE_SIZE);
  put16(container + 84, 480);
  put16(container + 86, 800);
  make_page_header(page_header, "XTG\0", INK_READER_PAGE_SIZE);

  const int ok =
      fwrite(container, 1, sizeof(container), file) == sizeof(container) &&
      fwrite(page_header, 1, sizeof(page_header), file) ==
          sizeof(page_header) &&
      write_bytes(file, INK_READER_PAGE_SIZE, 0x00) &&
      fwrite(page_header, 1, sizeof(page_header), file) ==
          sizeof(page_header) &&
      write_bytes(file, 127, 0x3c);
  return fclose(file) == 0 && ok;
}

static int write_single_page_xth(const char *path) {
  uint8_t container[72] = {0};
  uint8_t page_header[22];
  FILE *file = fopen(path, "wb");
  if (!file) return 0;

  memcpy(container, "XTC\0", 4);
  put16(container + 4, 1);
  put16(container + 6, 1);
  put64(container + 24, 56);
  put64(container + 32, 72);
  put64(container + 56, 72);
  put32(container + 64, 22 + INK_READER_PAGE_SIZE * 2u);
  put16(container + 68, 480);
  put16(container + 70, 800);
  make_page_header(page_header, "XTH\0", INK_READER_PAGE_SIZE * 2u);

  const int ok =
      fwrite(container, 1, sizeof(container), file) == sizeof(container) &&
      fwrite(page_header, 1, sizeof(page_header), file) ==
          sizeof(page_header) &&
      write_bytes(file, INK_READER_PAGE_SIZE * 2u, 0x00);
  return fclose(file) == 0 && ok;
}

static int write_xth_page_failure_book(void) {
  uint8_t container[88] = {0};
  uint8_t xtg_header[22];
  uint8_t xth_header[22];
  const uint64_t first_offset = sizeof(container);
  const uint64_t second_offset = first_offset + 22 + INK_READER_PAGE_SIZE;
  FILE *file = fopen(XTH_FAIL_BOOK, "wb");
  if (!file) return 0;

  memcpy(container, "XTC\0", 4);
  put16(container + 4, 1);
  put16(container + 6, 2);
  put64(container + 24, 56);
  put64(container + 32, 88);
  put64(container + 56, first_offset);
  put32(container + 64, 22 + INK_READER_PAGE_SIZE);
  put16(container + 68, 480);
  put16(container + 70, 800);
  put64(container + 72, second_offset);
  put32(container + 80, 22 + INK_READER_PAGE_SIZE * 2u);
  put16(container + 84, 480);
  put16(container + 86, 800);
  make_page_header(xtg_header, "XTG\0", INK_READER_PAGE_SIZE);
  make_page_header(xth_header, "XTH\0", INK_READER_PAGE_SIZE * 2u);

  const int ok =
      fwrite(container, 1, sizeof(container), file) == sizeof(container) &&
      fwrite(xtg_header, 1, sizeof(xtg_header), file) == sizeof(xtg_header) &&
      write_bytes(file, INK_READER_PAGE_SIZE, 0x00) &&
      fwrite(xth_header, 1, sizeof(xth_header), file) == sizeof(xth_header) &&
      write_bytes(file, INK_READER_PAGE_SIZE, 0x00) &&
      write_bytes(file, 127, 0x3c);
  return fclose(file) == 0 && ok;
}

static void cleanup_fixture(void) {
  remove(BAD_BOOK);
  remove(GOOD_BOOK);
  remove(PAGE_FAIL_BOOK);
  remove(XTH_GOOD_BOOK);
  remove(XTH_FAIL_BOOK);
  _rmdir(TEST_BOOKS);
  _rmdir(TEST_ROOT);
}

static int framebuffer_is(uint8_t *buffer, uint8_t value) {
  for (size_t i = 0; i < INK_READER_PAGE_SIZE; ++i)
    if (buffer[i] != value) return 0;
  return 1;
}

int main(void) {
  ink_reader_book_t book;
  char path[INK_READER_PATH_MAX];
  uint8_t *framebuffer = NULL;
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
  if (!write_single_page_book(BAD_BOOK, 127) ||
      !write_single_page_book(GOOD_BOOK, INK_READER_PAGE_SIZE) ||
      !write_page_failure_book() || !write_single_page_xth(XTH_GOOD_BOOK) ||
      !write_xth_page_failure_book()) {
    fprintf(stderr, "fixture file setup failed\n");
    goto cleanup;
  }
  if (!ink_reader_core_self_test()) {
    fprintf(stderr, "reader core self test failed\n");
    goto cleanup;
  }

  framebuffer = (uint8_t *)malloc(INK_READER_PAGE_SIZE);
  ink_reader_book_init(&book);
  if (!framebuffer || !ink_reader_book_open(&book, XTH_GOOD_BOOK) ||
      !ink_reader_book_load_current(&book, framebuffer,
                                    INK_READER_PAGE_SIZE) ||
      !framebuffer_is(framebuffer, 0xff)) {
    fprintf(stderr, "valid XTH page did not load\n");
    ink_reader_book_close(&book);
    goto cleanup;
  }
  ink_reader_book_close(&book);

  if (!ink_reader_book_open(&book, XTH_FAIL_BOOK)) {
    fprintf(stderr, "XTH transaction fixture did not open\n");
    goto cleanup;
  }
  memset(framebuffer, 0xa5, INK_READER_PAGE_SIZE);
  if (ink_reader_book_load_page(&book, 1, framebuffer,
                                INK_READER_PAGE_SIZE) ||
      book.current_page != 0 || !framebuffer_is(framebuffer, 0xa5)) {
    fprintf(stderr, "truncated XTH second plane changed output state\n");
    ink_reader_book_close(&book);
    goto cleanup;
  }
  ink_reader_book_close(&book);

  ink_reader_book_init(&book);
  if (ink_reader_open_first_book(&book, path, sizeof(path)) !=
          INK_READER_SCAN_OK ||
      strcmp(path, GOOD_BOOK) != 0 || strcmp(book.path, GOOD_BOOK) != 0) {
    fprintf(stderr, "scanner did not skip truncated first page: path=%s\n",
            path);
    ink_reader_book_close(&book);
    goto cleanup;
  }
  if (!ink_reader_book_load_current(&book, framebuffer, INK_READER_PAGE_SIZE)) {
    fprintf(stderr, "valid fallback book did not load\n");
    ink_reader_book_close(&book);
    goto cleanup;
  }
  ink_reader_book_close(&book);

  if (!ink_reader_book_open(&book, PAGE_FAIL_BOOK)) {
    fprintf(stderr, "page transaction fixture did not open\n");
    goto cleanup;
  }
  memset(framebuffer, 0xa5, INK_READER_PAGE_SIZE);
  if (ink_reader_book_load_page(&book, 1, framebuffer,
                                INK_READER_PAGE_SIZE)) {
    fprintf(stderr, "truncated target page unexpectedly loaded\n");
    ink_reader_book_close(&book);
    goto cleanup;
  }
  if (book.current_page != 0) {
    fprintf(stderr, "failed page load committed index=%u\n",
            (unsigned)book.current_page);
    ink_reader_book_close(&book);
    goto cleanup;
  }
  if (!framebuffer_is(framebuffer, 0xa5)) {
    fprintf(stderr, "failed page load modified framebuffer\n");
    ink_reader_book_close(&book);
    goto cleanup;
  }
  ink_reader_book_close(&book);
  result = 0;
  puts("PASS: reader scan and page transaction host tests");

cleanup:
  free(framebuffer);
  cleanup_fixture();
  return result;
}
