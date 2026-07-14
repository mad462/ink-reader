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
#define XTH_SCAN_BAD_BOOK TEST_BOOKS "/a_xth_bad.xtch"
#define XTH_SCAN_GOOD_BOOK TEST_BOOKS "/b_xth_good.xtch"
#define CHAPTER_V1_BOOK TEST_BOOKS "/chapter_v1.xtc"
#define CHAPTER_V256_BOOK TEST_BOOKS "/chapter_v256.xtc"
#define BAD_METADATA_OFFSET_BOOK TEST_BOOKS "/bad_metadata_offset.xtc"
#define BAD_CHAPTER_RANGE_BOOK TEST_BOOKS "/bad_chapter_range.xtc"
#define BAD_CHAPTER_REVERSED_BOOK TEST_BOOKS "/bad_chapter_reversed.xtc"
#define BAD_CHAPTER_END_BOOK TEST_BOOKS "/bad_chapter_end.xtc"
#define BAD_CHAPTER_COUNT_BOOK TEST_BOOKS "/bad_chapter_count.xtc"

#define XTC_METADATA_SIZE 256u
#define XTC_CHAPTER_ENTRY_SIZE 96u
#define XTC_PAGE_INDEX_ENTRY_SIZE 16u

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

static int write_single_page_xth(const char *path, size_t payload_bytes) {
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
      write_bytes(file, payload_bytes, 0x00);
  return fclose(file) == 0 && ok;
}

static int write_chapter_book(const char *path, uint16_t version,
                              const char *title, int bad_metadata_offset,
                              int bad_chapter_range,
                              uint16_t metadata_chapter_count) {
  uint8_t header[56] = {0};
  uint8_t metadata[XTC_METADATA_SIZE] = {0};
  uint8_t chapters[2 * XTC_CHAPTER_ENTRY_SIZE] = {0};
  uint8_t index[3 * XTC_PAGE_INDEX_ENTRY_SIZE] = {0};
  uint8_t page_header[22];
  const uint64_t header_size = version == 256 ? 48u : 56u;
  const uint64_t metadata_offset = header_size;
  const uint64_t chapter_offset = metadata_offset + sizeof(metadata);
  const uint64_t index_offset = chapter_offset + sizeof(chapters);
  const uint64_t data_offset = index_offset + sizeof(index);
  FILE *file = fopen(path, "wb");
  if (!file) return 0;

  memcpy(header, "XTC\0", 4);
  put16(header + 4, version);
  put16(header + 6, 3);
  header[9] = 1;
  header[11] = 1;
  put64(header + 16,
        bad_metadata_offset ? header_size - 1u : metadata_offset);
  put64(header + 24, index_offset);
  put64(header + 32, data_offset);
  if (version == 1) put64(header + 48, chapter_offset);

  snprintf((char *)metadata, 128, "%s", title);
  put16(metadata + 196, metadata_chapter_count);
  memcpy(chapters, "Opening", 7);
  put16(chapters + 80, bad_chapter_range == 1 ? 3
                        : bad_chapter_range == 2 ? 2
                                                 : 0);
  put16(chapters + 82, bad_chapter_range == 2   ? 0
                        : bad_chapter_range == 3 ? 3
                                                 : 1);
  memcpy(chapters + XTC_CHAPTER_ENTRY_SIZE, "Finale", 6);
  put16(chapters + XTC_CHAPTER_ENTRY_SIZE + 80, 2);
  put16(chapters + XTC_CHAPTER_ENTRY_SIZE + 82, 2);

  for (size_t i = 0; i < 3; ++i) {
    const uint64_t page_offset = data_offset + i * sizeof(page_header);
    put64(index + i * XTC_PAGE_INDEX_ENTRY_SIZE, page_offset);
    put32(index + i * XTC_PAGE_INDEX_ENTRY_SIZE + 8, sizeof(page_header));
    put16(index + i * XTC_PAGE_INDEX_ENTRY_SIZE + 12, 480);
    put16(index + i * XTC_PAGE_INDEX_ENTRY_SIZE + 14, 800);
  }
  make_page_header(page_header, "XTG\0", INK_READER_PAGE_SIZE);

  int ok = fwrite(header, 1, (size_t)header_size, file) == header_size &&
           fwrite(metadata, 1, sizeof(metadata), file) == sizeof(metadata) &&
           fwrite(chapters, 1, sizeof(chapters), file) == sizeof(chapters) &&
           fwrite(index, 1, sizeof(index), file) == sizeof(index);
  for (size_t i = 0; ok && i < 3; ++i)
    ok = fwrite(page_header, 1, sizeof(page_header), file) ==
         sizeof(page_header);
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
  remove(XTH_SCAN_BAD_BOOK);
  remove(XTH_SCAN_GOOD_BOOK);
  remove(CHAPTER_V1_BOOK);
  remove(CHAPTER_V256_BOOK);
  remove(BAD_METADATA_OFFSET_BOOK);
  remove(BAD_CHAPTER_RANGE_BOOK);
  remove(BAD_CHAPTER_REVERSED_BOOK);
  remove(BAD_CHAPTER_END_BOOK);
  remove(BAD_CHAPTER_COUNT_BOOK);
  _rmdir(TEST_BOOKS);
  _rmdir(TEST_ROOT);
}

static int framebuffer_is(uint8_t *buffer, uint8_t value) {
  for (size_t i = 0; i < INK_READER_PAGE_SIZE; ++i)
    if (buffer[i] != value) return 0;
  return 1;
}

static int display_chapter_filter_is_compatible(void) {
  ink_reader_chapter_t chapters[] = {
      {.title = "译序", .start_page = 0, .end_page = 0},
      {.title = "前言：关于本书", .start_page = 1, .end_page = 1},
      {.title = "第一章", .start_page = 2, .end_page = 4},
      {.title = "附录 A", .start_page = 5, .end_page = 5},
      {.title = "第二章", .start_page = 6, .end_page = 8},
  };
  ink_reader_book_t book = {
      .chapters = chapters,
      .chapter_count = sizeof(chapters) / sizeof(chapters[0]),
      .page_count = 9,
  };
  size_t display_index = 99U;
  size_t display_total = 99U;
  const ink_reader_chapter_t *chapter = NULL;

  if (ink_reader_chapter_title_is_displayable(NULL) ||
      ink_reader_chapter_title_is_displayable("") ||
      ink_reader_chapter_title_is_displayable("序") ||
      ink_reader_chapter_title_is_displayable("序章") ||
      ink_reader_chapter_title_is_displayable("上篇") ||
      ink_reader_chapter_title_is_displayable("下篇") ||
      ink_reader_chapter_title_is_displayable("楔子之一") ||
      ink_reader_chapter_title_is_displayable("后记补遗") ||
      !ink_reader_chapter_title_is_displayable("第一章"))
    return 0;

  if (ink_reader_book_resolve_display_chapter(
          &book, 1U, &display_index, &display_total, &chapter) ||
      display_index != 0U || display_total != 2U || chapter != NULL)
    return 0;
  if (!ink_reader_book_resolve_display_chapter(
          &book, 4U, &display_index, &display_total, &chapter) ||
      display_index != 0U || display_total != 2U || chapter != &chapters[2])
    return 0;
  return ink_reader_book_resolve_display_chapter(
             &book, 7U, &display_index, &display_total, &chapter) &&
         display_index == 1U && display_total == 2U &&
         chapter == &chapters[4] &&
         !ink_reader_book_resolve_display_chapter(
             &book, 9U, &display_index, &display_total, &chapter);
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
      !write_page_failure_book() ||
      !write_single_page_xth(XTH_GOOD_BOOK, INK_READER_PAGE_SIZE * 2u) ||
      !write_xth_page_failure_book() ||
      !write_chapter_book(CHAPTER_V1_BOOK, 1, "Version One", 0, 0, 2) ||
      !write_chapter_book(CHAPTER_V256_BOOK, 256, "Legacy 256", 0, 0, 2) ||
      !write_chapter_book(BAD_METADATA_OFFSET_BOOK, 1, "Bad Offset", 1, 0,
                          2) ||
      !write_chapter_book(BAD_CHAPTER_RANGE_BOOK, 1, "Bad Range", 0, 1, 2) ||
      !write_chapter_book(BAD_CHAPTER_REVERSED_BOOK, 1, "Bad Reverse", 0,
                          2, 2) ||
      !write_chapter_book(BAD_CHAPTER_END_BOOK, 1, "Bad End", 0, 3, 2) ||
      !write_chapter_book(BAD_CHAPTER_COUNT_BOOK, 1, "Bad Count", 0, 0, 1)) {
    fprintf(stderr, "fixture file setup failed\n");
    goto cleanup;
  }
  if (!ink_reader_core_self_test()) {
    fprintf(stderr, "reader core self test failed\n");
    goto cleanup;
  }
  if (!display_chapter_filter_is_compatible()) {
    fprintf(stderr, "display chapter filtering is incompatible\n");
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

  if (!ink_reader_book_open(&book, CHAPTER_V1_BOOK) || !book.has_metadata ||
      strcmp(book.metadata.title, "Version One") != 0 ||
      ink_reader_book_chapter_count(&book) != 2) {
    fprintf(stderr, "v1 metadata or chapters did not parse\n");
    ink_reader_book_close(&book);
    goto cleanup;
  }
  const ink_reader_chapter_t *chapter = ink_reader_book_chapter_at(&book, 0);
  if (!chapter || strcmp(chapter->title, "Opening") != 0 ||
      chapter->start_page != 0 || chapter->end_page != 1 ||
      ink_reader_book_chapter_at(&book, 2) != NULL ||
      ink_reader_book_chapter_for_page(&book, 0) != chapter ||
      ink_reader_book_chapter_for_page(&book, 1) != chapter) {
    fprintf(stderr, "v1 chapter entries or first boundary are wrong\n");
    ink_reader_book_close(&book);
    goto cleanup;
  }
  chapter = ink_reader_book_chapter_for_page(&book, 2);
  if (!chapter || strcmp(chapter->title, "Finale") != 0 ||
      ink_reader_book_chapter_for_page(&book, 3) != NULL ||
      !ink_reader_book_jump_to_chapter(&book, 1) || book.current_page != 2 ||
      ink_reader_book_jump_to_chapter(&book, 2) || book.current_page != 2) {
    fprintf(stderr, "page-to-chapter boundary or jump is wrong\n");
    ink_reader_book_close(&book);
    goto cleanup;
  }
  ink_reader_book_close(&book);

  if (!ink_reader_book_open(&book, CHAPTER_V256_BOOK) ||
      strcmp(book.metadata.title, "Legacy 256") != 0 ||
      ink_reader_book_chapter_count(&book) != 2 ||
      !ink_reader_book_jump_to_chapter(&book, 1) || book.current_page != 2) {
    fprintf(stderr, "v256 metadata, chapters, or jump did not parse\n");
    ink_reader_book_close(&book);
    goto cleanup;
  }
  ink_reader_book_close(&book);

  if (ink_reader_book_open(&book, BAD_METADATA_OFFSET_BOOK)) {
    fprintf(stderr, "invalid metadata offset was accepted\n");
    ink_reader_book_close(&book);
    goto cleanup;
  }
  if (ink_reader_book_open(&book, BAD_CHAPTER_RANGE_BOOK)) {
    fprintf(stderr, "out-of-range chapter was accepted\n");
    ink_reader_book_close(&book);
    goto cleanup;
  }
  if (ink_reader_book_open(&book, BAD_CHAPTER_REVERSED_BOOK)) {
    fprintf(stderr, "reversed chapter range was accepted\n");
    ink_reader_book_close(&book);
    goto cleanup;
  }
  if (ink_reader_book_open(&book, BAD_CHAPTER_END_BOOK)) {
    fprintf(stderr, "out-of-range chapter end was accepted\n");
    ink_reader_book_close(&book);
    goto cleanup;
  }
  if (ink_reader_book_open(&book, BAD_CHAPTER_COUNT_BOOK)) {
    fprintf(stderr, "metadata chapter count mismatch was accepted\n");
    ink_reader_book_close(&book);
    goto cleanup;
  }

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

  remove(BAD_BOOK);
  remove(GOOD_BOOK);
  if (!write_single_page_xth(XTH_SCAN_BAD_BOOK, INK_READER_PAGE_SIZE) ||
      !write_single_page_xth(XTH_SCAN_GOOD_BOOK,
                             INK_READER_PAGE_SIZE * 2u)) {
    fprintf(stderr, "XTH scanner fixture setup failed\n");
    goto cleanup;
  }
  ink_reader_book_init(&book);
  if (ink_reader_open_first_book(&book, path, sizeof(path)) !=
          INK_READER_SCAN_OK ||
      strcmp(path, XTH_SCAN_GOOD_BOOK) != 0 ||
      strcmp(book.path, XTH_SCAN_GOOD_BOOK) != 0) {
    fprintf(stderr, "scanner did not skip truncated XTH first page: path=%s\n",
            path);
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
