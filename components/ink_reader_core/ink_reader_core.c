#include "ink_reader_core.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "esp_heap_caps.h"

#define XTC_HEADER_SIZE 56u
#define XTC_INDEX_ENTRY_SIZE 16u
#define XTG_HEADER_SIZE 22u
#define XTC_MAGIC 0x00435458u
#define XTCH_MAGIC 0x48435458u
#define XTG_MAGIC 0x00475458u
#define XTH_MAGIC 0x00485458u

static uint16_t le16(const uint8_t *p) {
  return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}
static uint32_t le32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}
static uint64_t le64(const uint8_t *p) {
  return (uint64_t)le32(p) | ((uint64_t)le32(p + 4) << 32);
}

static bool parse_header(const uint8_t *raw, size_t length, uint16_t *count,
                         uint64_t *index_offset, uint64_t *data_offset) {
  if (!raw || length < XTC_HEADER_SIZE || !count || !index_offset ||
      !data_offset)
    return false;
  const uint32_t magic = le32(raw);
  const uint16_t version = le16(raw + 4);
  *count = le16(raw + 6);
  *index_offset = le64(raw + 24);
  *data_offset = le64(raw + 32);
  if ((magic != XTC_MAGIC && magic != XTCH_MAGIC) ||
      (version != 1 && version != 256) || *count == 0)
    return false;
  const uint64_t header_size = version == 256 ? 48u : 56u;
  return *index_offset >= header_size && *data_offset > *index_offset &&
         *index_offset + (uint64_t)*count * XTC_INDEX_ENTRY_SIZE <=
             *data_offset;
}

static bool extension_ok(const char *name) {
  const char *dot = name ? strrchr(name, '.') : NULL;
  return dot && (!strcasecmp(dot, ".xtc") || !strcasecmp(dot, ".xtch"));
}

typedef struct {
  char (*paths)[INK_READER_PATH_MAX];
  size_t count;
  size_t capacity;
} reader_candidate_list_t;

typedef enum {
  READER_DIR_OK,
  READER_DIR_MISSING,
  READER_DIR_IO_ERROR,
} reader_dir_result_t;

static bool candidate_add(reader_candidate_list_t *list, const char *path) {
  if (list->count == list->capacity) {
    const size_t capacity = list->capacity == 0U ? 8U : list->capacity * 2U;
    if (capacity < list->capacity ||
        capacity > SIZE_MAX / sizeof(*list->paths))
      return false;
    void *resized = heap_caps_realloc_prefer(
        list->paths, capacity * sizeof(*list->paths), 2,
        MALLOC_CAP_DEFAULT | MALLOC_CAP_SPIRAM,
        MALLOC_CAP_DEFAULT | MALLOC_CAP_INTERNAL);
    if (!resized) return false;
    list->paths = resized;
    list->capacity = capacity;
  }
  if (snprintf(list->paths[list->count], INK_READER_PATH_MAX, "%s", path) >=
      INK_READER_PATH_MAX)
    return false;
  ++list->count;
  return true;
}

static int compare_candidates(const void *left, const void *right) {
  return strcasecmp((const char *)left, (const char *)right);
}

static reader_dir_result_t collect_candidates(
    const char *dir_path, reader_candidate_list_t *list) {
  errno = 0;
  DIR *dir = opendir(dir_path);
  if (!dir)
    return errno == ENOENT ? READER_DIR_MISSING : READER_DIR_IO_ERROR;
  reader_dir_result_t result = READER_DIR_OK;
  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    if (entry->d_name[0] == '.' || !extension_ok(entry->d_name)) continue;
    char path[INK_READER_PATH_MAX];
    if (snprintf(path, sizeof(path), "%s/%s", dir_path, entry->d_name) >=
            (int)sizeof(path) ||
        !candidate_add(list, path)) {
      result = READER_DIR_IO_ERROR;
      break;
    }
  }
  closedir(dir);
  return result;
}

static ink_reader_scan_result_t classify_scan(
    size_t candidate_count, reader_dir_result_t books_result,
    reader_dir_result_t root_result) {
  if (books_result == READER_DIR_IO_ERROR ||
      root_result == READER_DIR_IO_ERROR)
    return INK_READER_SCAN_IO_ERROR;
  if (candidate_count > 0U) return INK_READER_SCAN_FORMAT_ERROR;
  if (books_result == READER_DIR_MISSING)
    return INK_READER_SCAN_DIR_MISSING;
  return INK_READER_SCAN_EMPTY;
}

static bool seek_page_payload(FILE *file, const ink_reader_page_t *page,
                              uint32_t *data_size) {
  if (!file || !page || !data_size || page->offset > LONG_MAX ||
      fseek(file, (long)page->offset, SEEK_SET) != 0)
    return false;
  uint8_t header[XTG_HEADER_SIZE];
  if (fread(header, 1, sizeof(header), file) != sizeof(header)) return false;
  const uint32_t magic = le32(header);
  *data_size = le32(header + 10);
  const uint32_t expected_size =
      magic == XTG_MAGIC   ? INK_READER_PAGE_SIZE
      : magic == XTH_MAGIC ? INK_READER_PAGE_SIZE * 2u
                           : 0u;
  return le16(header + 4) == 480 && le16(header + 6) == 800 &&
         expected_size != 0U && *data_size == expected_size &&
         (uint64_t)XTG_HEADER_SIZE + *data_size <= page->encoded_size;
}

static bool page_payload_readable(FILE *file, const ink_reader_page_t *page) {
  uint32_t data_size = 0;
  if (!seek_page_payload(file, page, &data_size)) return false;
  const uint64_t last_byte =
      page->offset + XTG_HEADER_SIZE + (uint64_t)data_size - 1U;
  return last_byte <= LONG_MAX && fseek(file, (long)last_byte, SEEK_SET) == 0 &&
         fgetc(file) != EOF;
}

static bool find_in(const char *dir_path, char *path, size_t path_size) {
  DIR *dir = opendir(dir_path);
  if (!dir) return false;
  char best[INK_READER_PATH_MAX] = {0};
  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    if (entry->d_name[0] == '.' || !extension_ok(entry->d_name)) continue;
    if (best[0] == 0 || strcasecmp(entry->d_name, best) < 0)
      snprintf(best, sizeof(best), "%s", entry->d_name);
  }
  closedir(dir);
  if (best[0] == 0) return false;
  return snprintf(path, path_size, "%s/%s", dir_path, best) < (int)path_size;
}

void ink_reader_book_init(ink_reader_book_t *book) {
  if (book) memset(book, 0, sizeof(*book));
}
void ink_reader_book_close(ink_reader_book_t *book) {
  if (!book) return;
  if (book->file) fclose(book->file);
  free(book->pages);
  memset(book, 0, sizeof(*book));
}
bool ink_reader_find_first_book(char *path, size_t size) {
  if (!path || !size) return false;
  path[0] = 0;
  return find_in("/sdcard/books", path, size) || find_in("/sdcard", path, size);
}

ink_reader_scan_result_t ink_reader_open_first_book(
    ink_reader_book_t *book, char *candidate_path, size_t path_size) {
  if (!book || !candidate_path || path_size == 0U)
    return INK_READER_SCAN_IO_ERROR;
  candidate_path[0] = 0;
  reader_candidate_list_t candidates = {0};
  const reader_dir_result_t books_result =
      collect_candidates("/sdcard/books", &candidates);
  const size_t books_count = candidates.count;
  const reader_dir_result_t root_result =
      collect_candidates("/sdcard", &candidates);
  if (books_count > 1U)
    qsort(candidates.paths, books_count, sizeof(*candidates.paths),
          compare_candidates);
  const size_t root_count = candidates.count - books_count;
  if (root_count > 1U)
    qsort(candidates.paths + books_count, root_count,
          sizeof(*candidates.paths), compare_candidates);

  for (size_t i = 0; i < candidates.count; ++i) {
    if (!ink_reader_book_open(book, candidates.paths[i])) continue;
    if (!page_payload_readable(book->file, &book->pages[0])) {
      ink_reader_book_close(book);
      continue;
    }
    snprintf(candidate_path, path_size, "%s", candidates.paths[i]);
    free(candidates.paths);
    return INK_READER_SCAN_OK;
  }

  if (candidates.count > 0U)
    snprintf(candidate_path, path_size, "%s", candidates.paths[0]);
  const ink_reader_scan_result_t result =
      classify_scan(candidates.count, books_result, root_result);
  free(candidates.paths);
  return result;
}

bool ink_reader_book_open(ink_reader_book_t *book, const char *path) {
  if (!book || !path) return false;
  ink_reader_book_close(book);
  FILE *file = fopen(path, "rb");
  if (!file) return false;
  uint8_t header[XTC_HEADER_SIZE];
  uint16_t count;
  uint64_t index_offset, data_offset;
  if (fread(header, 1, sizeof(header), file) != sizeof(header) ||
      !parse_header(header, sizeof(header), &count, &index_offset,
                    &data_offset) ||
      index_offset > LONG_MAX)
    goto fail;
  ink_reader_page_t *pages = heap_caps_calloc(
      count, sizeof(*pages), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!pages) pages = calloc(count, sizeof(*pages));
  if (!pages) goto fail;
  if (fseek(file, (long)index_offset, SEEK_SET) != 0) {
    free(pages);
    goto fail;
  }
  for (uint16_t i = 0; i < count; ++i) {
    uint8_t raw[XTC_INDEX_ENTRY_SIZE];
    if (fread(raw, 1, sizeof(raw), file) != sizeof(raw)) {
      free(pages);
      goto fail;
    }
    pages[i].offset = le64(raw);
    pages[i].encoded_size = le32(raw + 8);
    pages[i].width = le16(raw + 12);
    pages[i].height = le16(raw + 14);
    if (pages[i].offset < data_offset ||
        pages[i].encoded_size < XTG_HEADER_SIZE || pages[i].width != 480 ||
        pages[i].height != 800) {
      free(pages);
      goto fail;
    }
  }
  book->file = file;
  book->pages = pages;
  book->page_count = count;
  snprintf(book->path, sizeof(book->path), "%s", path);
  return true;
fail:
  fclose(file);
  return false;
}

static void xth_to_bw(const uint8_t *p0, const uint8_t *p1, uint8_t *out) {
  memset(out, 0xff, INK_READER_PAGE_SIZE);
  const uint32_t col_bytes = 100, row_bytes = 60;
  for (uint16_t x = 0; x < 480; ++x)
    for (uint16_t y = 0; y < 800; ++y) {
      uint32_t src = (uint32_t)(479 - x) * col_bytes + y / 8;
      uint8_t mask = (uint8_t)(0x80u >> (y & 7));
      uint8_t level =
          (uint8_t)(((p1[src] & mask ? 1 : 0) << 1) | (p0[src] & mask ? 1 : 0));
      if (level >= 2)
        out[(uint32_t)y * row_bytes + x / 8] &= (uint8_t)~(0x80u >> (x & 7));
    }
}

static bool load_page(FILE *file, const ink_reader_page_t *page,
                      uint8_t *buffer, size_t length) {
  if (!file || !page || !buffer || length < INK_READER_PAGE_SIZE)
    return false;
  uint32_t data_size = 0;
  if (!seek_page_payload(file, page, &data_size)) return false;
  if (data_size == INK_READER_PAGE_SIZE)
    return fread(buffer, 1, data_size, file) == data_size;
  if (data_size == INK_READER_PAGE_SIZE * 2u) {
    uint8_t *p0 = heap_caps_malloc(INK_READER_PAGE_SIZE,
                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    uint8_t *p1 = heap_caps_malloc(INK_READER_PAGE_SIZE,
                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p0 || !p1) {
      free(p0);
      free(p1);
      return false;
    }
    bool ok =
        fread(p0, 1, INK_READER_PAGE_SIZE, file) == INK_READER_PAGE_SIZE &&
        fread(p1, 1, INK_READER_PAGE_SIZE, file) == INK_READER_PAGE_SIZE;
    if (ok) xth_to_bw(p0, p1, buffer);
    free(p0);
    free(p1);
    return ok;
  }
  return false;
}

bool ink_reader_book_load_current(const ink_reader_book_t *book,
                                  uint8_t *buffer, size_t length) {
  if (!book || !book->pages || book->current_page >= book->page_count)
    return false;
  return load_page(book->file, &book->pages[book->current_page], buffer,
                   length);
}

bool ink_reader_book_load_page(ink_reader_book_t *book, size_t page_index,
                               uint8_t *buffer, size_t length) {
  if (!book || !book->pages || page_index >= book->page_count ||
      !load_page(book->file, &book->pages[page_index], buffer, length))
    return false;
  book->current_page = page_index;
  return true;
}

bool ink_reader_book_next(ink_reader_book_t *book) {
  if (!book || book->current_page + 1 >= book->page_count) return false;
  ++book->current_page;
  return true;
}
bool ink_reader_book_previous(ink_reader_book_t *book) {
  if (!book || book->current_page == 0) return false;
  --book->current_page;
  return true;
}

bool ink_reader_core_self_test(void) {
  uint8_t h[XTC_HEADER_SIZE] = {'X', 'T', 'C', 0, 1, 0, 2, 0};
  h[24] = 56;
  h[32] = 88;
  uint16_t count = 0;
  uint64_t index = 0, data = 0;
  if (!parse_header(h, sizeof(h), &count, &index, &data) || count != 2 ||
      index != 56 || data != 88)
    return false;
  h[0] = 'B';
  if (parse_header(h, sizeof(h), &count, &index, &data)) return false;
  if (!extension_ok("BOOK.XTC") || !extension_ok("book.xtch") ||
      extension_ok("book.txt") ||
      classify_scan(0U, READER_DIR_MISSING, READER_DIR_OK) !=
          INK_READER_SCAN_DIR_MISSING ||
      classify_scan(0U, READER_DIR_OK, READER_DIR_OK) !=
          INK_READER_SCAN_EMPTY ||
      classify_scan(1U, READER_DIR_OK, READER_DIR_OK) !=
          INK_READER_SCAN_FORMAT_ERROR ||
      classify_scan(1U, READER_DIR_OK, READER_DIR_IO_ERROR) !=
          INK_READER_SCAN_IO_ERROR ||
      classify_scan(0U, READER_DIR_OK, READER_DIR_IO_ERROR) !=
          INK_READER_SCAN_IO_ERROR)
    return false;
  ink_reader_book_t book = {.page_count = 2, .current_page = 0};
  uint8_t unused = 0;
  if (ink_reader_book_load_page(&book, 1, &unused, INK_READER_PAGE_SIZE) ||
      book.current_page != 0)
    return false;
  return !ink_reader_book_previous(&book) && ink_reader_book_next(&book) &&
         book.current_page == 1 && !ink_reader_book_next(&book) &&
         ink_reader_book_previous(&book);
}
