#include "ink_reader_core.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "esp_heap_caps.h"

#define XTC_HEADER_SIZE 56u
#define XTC_LEGACY_HEADER_SIZE 48u
#define XTC_METADATA_SIZE 256u
#define XTC_CHAPTER_ENTRY_SIZE 96u
#define XTC_INDEX_ENTRY_SIZE 16u
#define XTG_HEADER_SIZE 22u
#define XTC_MAGIC 0x00435458u
#define XTCH_MAGIC 0x48435458u
#define XTG_MAGIC 0x00475458u
#define XTH_MAGIC 0x00485458u
#define INK_READER_CATALOG_COOKIE 0x494e4b43u

#ifndef INK_READER_SCAN_ROOT
#define INK_READER_SCAN_ROOT "/sdcard"
#endif
#define INK_READER_BOOKS_DIR INK_READER_SCAN_ROOT "/books"

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

typedef struct {
  uint16_t version;
  uint16_t page_count;
  bool has_metadata;
  bool has_chapters;
  uint64_t metadata_offset;
  uint64_t chapter_offset;
  uint64_t index_offset;
  uint64_t data_offset;
} xtc_header_t;

static bool range_end(uint64_t offset, uint64_t length, uint64_t *end) {
  if (UINT64_MAX - offset < length) return false;
  if (end) *end = offset + length;
  return true;
}

static void copy_text(char *dst, size_t dst_size, const uint8_t *src,
                      size_t src_size) {
  size_t length = 0;
  if (!dst || dst_size == 0U) return;
  while (length < src_size && src[length] != 0U) ++length;
  if (length >= dst_size) length = dst_size - 1U;
  if (length > 0U) memcpy(dst, src, length);
  dst[length] = '\0';
}

static bool parse_header(const uint8_t *raw, size_t length,
                         xtc_header_t *header) {
  if (!raw || length < XTC_HEADER_SIZE || !header) return false;
  memset(header, 0, sizeof(*header));
  const uint32_t magic = le32(raw);
  header->version = le16(raw + 4);
  header->page_count = le16(raw + 6);
  header->has_metadata = raw[9] != 0U;
  header->has_chapters = raw[11] != 0U;
  header->metadata_offset = le64(raw + 16);
  header->index_offset = le64(raw + 24);
  header->data_offset = le64(raw + 32);
  const uint64_t header_size =
      header->version == 256 ? XTC_LEGACY_HEADER_SIZE : XTC_HEADER_SIZE;
  if ((magic != XTC_MAGIC && magic != XTCH_MAGIC) ||
      (header->version != 1 && header->version != 256) ||
      header->page_count == 0U || header->index_offset < header_size ||
      header->data_offset <= header->index_offset)
    return false;

  uint64_t metadata_end = 0;
  uint64_t index_end = 0;
  if (!range_end(header->index_offset,
                 (uint64_t)header->page_count * XTC_INDEX_ENTRY_SIZE,
                 &index_end) ||
      index_end > header->data_offset)
    return false;
  if (header->has_metadata) {
    if (header->metadata_offset < header_size ||
        !range_end(header->metadata_offset, XTC_METADATA_SIZE,
                   &metadata_end) ||
        metadata_end > header->index_offset)
      return false;
  } else if (header->metadata_offset != 0U) {
    return false;
  }

  if (header->version == 256) {
    if (header->has_chapters)
      header->chapter_offset =
          header->has_metadata ? metadata_end : XTC_LEGACY_HEADER_SIZE;
  } else {
    header->chapter_offset = le64(raw + 48);
  }
  if (header->has_chapters) {
    if (header->chapter_offset < header_size ||
        header->chapter_offset >= header->index_offset ||
        (header->has_metadata && header->chapter_offset < metadata_end) ||
        (header->index_offset - header->chapter_offset) %
                XTC_CHAPTER_ENTRY_SIZE !=
            0U)
      return false;
  } else if (header->chapter_offset != 0U) {
    return false;
  }
  return true;
}

static void parse_metadata(const uint8_t *raw, ink_reader_metadata_t *out) {
  memset(out, 0, sizeof(*out));
  copy_text(out->title, sizeof(out->title), raw, 128U);
  copy_text(out->author, sizeof(out->author), raw + 128, 64U);
  copy_text(out->publisher, sizeof(out->publisher), raw + 200, 32U);
  copy_text(out->language, sizeof(out->language), raw + 224, 16U);
  out->create_time = le32(raw + 192);
  out->chapter_count = le16(raw + 196);
  out->cover_page = le16(raw + 244);
  if (out->chapter_count == 0U) out->chapter_count = le16(raw + 246);
}

static bool read_metadata(FILE *file, const xtc_header_t *header,
                          ink_reader_metadata_t *metadata) {
  uint8_t raw[XTC_METADATA_SIZE];
  if (!header->has_metadata) return true;
  if (header->metadata_offset > LONG_MAX ||
      fseek(file, (long)header->metadata_offset, SEEK_SET) != 0 ||
      fread(raw, 1, sizeof(raw), file) != sizeof(raw))
    return false;
  parse_metadata(raw, metadata);
  return true;
}

static bool read_chapters(FILE *file, const xtc_header_t *header,
                          ink_reader_chapter_t **chapters_out,
                          size_t *count_out) {
  *chapters_out = NULL;
  *count_out = 0U;
  if (!header->has_chapters) return true;
  const uint64_t span = header->index_offset - header->chapter_offset;
  const uint64_t count64 = span / XTC_CHAPTER_ENTRY_SIZE;
  if (count64 == 0U || count64 > SIZE_MAX / sizeof(ink_reader_chapter_t) ||
      header->chapter_offset > LONG_MAX)
    return false;
  const size_t count = (size_t)count64;
  ink_reader_chapter_t *chapters = heap_caps_calloc(
      count, sizeof(*chapters), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!chapters) chapters = calloc(count, sizeof(*chapters));
  if (!chapters || fseek(file, (long)header->chapter_offset, SEEK_SET) != 0) {
    free(chapters);
    return false;
  }
  for (size_t i = 0; i < count; ++i) {
    uint8_t raw[XTC_CHAPTER_ENTRY_SIZE];
    if (fread(raw, 1, sizeof(raw), file) != sizeof(raw)) {
      free(chapters);
      return false;
    }
    copy_text(chapters[i].title, sizeof(chapters[i].title), raw, 80U);
    chapters[i].start_page = le16(raw + 80);
    chapters[i].end_page = le16(raw + 82);
    if (chapters[i].start_page >= header->page_count ||
        chapters[i].end_page < chapters[i].start_page ||
        chapters[i].end_page >= header->page_count ||
        (i > 0U &&
         chapters[i].start_page < chapters[i - 1U].start_page)) {
      free(chapters);
      return false;
    }
  }
  *chapters_out = chapters;
  *count_out = count;
  return true;
}

static bool extension_ok(const char *name) {
  const char *dot = name ? strrchr(name, '.') : NULL;
  return dot && (!strcasecmp(dot, ".xtc") || !strcasecmp(dot, ".xtch"));
}

static unsigned char ascii_lower(unsigned char value) {
  return value >= 'A' && value <= 'Z' ? (unsigned char)(value + ('a' - 'A'))
                                      : value;
}

static int ascii_case_compare(const char *left, const char *right) {
  while (*left && *right) {
    const unsigned char l = ascii_lower((unsigned char)*left);
    const unsigned char r = ascii_lower((unsigned char)*right);
    if (l != r) return l < r ? -1 : 1;
    ++left;
    ++right;
  }
  if (*left || *right) return *left ? 1 : -1;
  return strcmp(left, right);
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
  const char *left_path = (const char *)left;
  const char *right_path = (const char *)right;
  const char *left_name = strrchr(left_path, '/');
  const char *right_name = strrchr(right_path, '/');
  left_name = left_name ? left_name + 1 : left_path;
  right_name = right_name ? right_name + 1 : right_path;
  const int folded = ascii_case_compare(left_name, right_name);
  return folded != 0 ? folded : strcmp(left_name, right_name);
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
  free(book->chapters);
  free(book->pages);
  memset(book, 0, sizeof(*book));
}

void ink_reader_catalog_init(ink_reader_catalog_t *catalog) {
  if (!catalog) return;
  catalog->items = NULL;
  catalog->count = 0U;
  catalog->lifecycle_cookie = INK_READER_CATALOG_COOKIE;
}

bool ink_reader_catalog_load(ink_reader_catalog_t *catalog) {
  if (!catalog ||
      catalog->lifecycle_cookie != INK_READER_CATALOG_COOKIE)
    return false;
  reader_candidate_list_t candidates = {0};
  if (collect_candidates(INK_READER_BOOKS_DIR, &candidates) != READER_DIR_OK) {
    free(candidates.paths);
    return false;
  }
  if (candidates.count > 1U)
    qsort(candidates.paths, candidates.count, sizeof(*candidates.paths),
          compare_candidates);

  const size_t count = candidates.count < INK_READER_CATALOG_CAPACITY
                           ? candidates.count
                           : INK_READER_CATALOG_CAPACITY;
  ink_reader_catalog_item_t *items = NULL;
  if (count > 0U) {
    items = heap_caps_calloc(count, sizeof(*items),
                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!items) items = calloc(count, sizeof(*items));
    if (!items) {
      free(candidates.paths);
      return false;
    }
  }
  for (size_t i = 0; i < count; ++i) {
    const char *name = strrchr(candidates.paths[i], '/');
    name = name ? name + 1 : candidates.paths[i];
    if (snprintf(items[i].path, sizeof(items[i].path), "%s",
                 candidates.paths[i]) >= (int)sizeof(items[i].path) ||
        snprintf(items[i].name, sizeof(items[i].name), "%s", name) >=
            (int)sizeof(items[i].name)) {
      free(items);
      free(candidates.paths);
      return false;
    }
  }
  free(candidates.paths);
  ink_reader_catalog_free(catalog);
  catalog->items = items;
  catalog->count = count;
  return true;
}

void ink_reader_catalog_free(ink_reader_catalog_t *catalog) {
  if (!catalog ||
      catalog->lifecycle_cookie != INK_READER_CATALOG_COOKIE)
    return;
  free(catalog->items);
  catalog->items = NULL;
  catalog->count = 0U;
}

size_t ink_reader_catalog_count(const ink_reader_catalog_t *catalog) {
  return catalog &&
                 catalog->lifecycle_cookie == INK_READER_CATALOG_COOKIE
             ? catalog->count
             : 0U;
}

const ink_reader_catalog_item_t *ink_reader_catalog_at(
    const ink_reader_catalog_t *catalog, size_t index) {
  return catalog &&
                 catalog->lifecycle_cookie == INK_READER_CATALOG_COOKIE &&
                 index < catalog->count
             ? &catalog->items[index]
             : NULL;
}

bool ink_reader_find_first_book(char *path, size_t size) {
  if (!path || !size) return false;
  path[0] = 0;
  return find_in(INK_READER_BOOKS_DIR, path, size) ||
         find_in(INK_READER_SCAN_ROOT, path, size);
}

ink_reader_scan_result_t ink_reader_open_first_book(
    ink_reader_book_t *book, char *candidate_path, size_t path_size) {
  if (!book || !candidate_path || path_size == 0U)
    return INK_READER_SCAN_IO_ERROR;
  candidate_path[0] = 0;
  reader_candidate_list_t candidates = {0};
  const reader_dir_result_t books_result =
      collect_candidates(INK_READER_BOOKS_DIR, &candidates);
  const size_t books_count = candidates.count;
  const reader_dir_result_t root_result =
      collect_candidates(INK_READER_SCAN_ROOT, &candidates);
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
  xtc_header_t parsed;
  if (fread(header, 1, sizeof(header), file) != sizeof(header) ||
      !parse_header(header, sizeof(header), &parsed) ||
      parsed.index_offset > LONG_MAX)
    goto fail;
  ink_reader_page_t *pages = heap_caps_calloc(
      parsed.page_count, sizeof(*pages), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!pages) pages = calloc(parsed.page_count, sizeof(*pages));
  if (!pages) goto fail;
  if (fseek(file, (long)parsed.index_offset, SEEK_SET) != 0) {
    free(pages);
    goto fail;
  }
  uint64_t previous_end = parsed.data_offset;
  for (uint16_t i = 0; i < parsed.page_count; ++i) {
    uint8_t raw[XTC_INDEX_ENTRY_SIZE];
    if (fread(raw, 1, sizeof(raw), file) != sizeof(raw)) {
      free(pages);
      goto fail;
    }
    pages[i].offset = le64(raw);
    pages[i].encoded_size = le32(raw + 8);
    pages[i].width = le16(raw + 12);
    pages[i].height = le16(raw + 14);
    uint64_t page_end = 0;
    if (pages[i].offset < parsed.data_offset ||
        pages[i].encoded_size < XTG_HEADER_SIZE || pages[i].width != 480 ||
        pages[i].height != 800 ||
        !range_end(pages[i].offset, pages[i].encoded_size, &page_end) ||
        pages[i].offset < previous_end) {
      free(pages);
      goto fail;
    }
    previous_end = page_end;
  }
  ink_reader_metadata_t metadata = {0};
  ink_reader_chapter_t *chapters = NULL;
  size_t chapter_count = 0U;
  if (!read_metadata(file, &parsed, &metadata) ||
      !read_chapters(file, &parsed, &chapters, &chapter_count)) {
    free(pages);
    goto fail;
  }
  if (parsed.has_metadata && parsed.has_chapters &&
      metadata.chapter_count != chapter_count) {
    free(chapters);
    free(pages);
    goto fail;
  }
  book->file = file;
  book->metadata = metadata;
  book->has_metadata = parsed.has_metadata;
  book->chapters = chapters;
  book->chapter_count = chapter_count;
  book->pages = pages;
  book->page_count = parsed.page_count;
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
  if (data_size == INK_READER_PAGE_SIZE) {
    uint8_t *page_data = heap_caps_malloc(
        INK_READER_PAGE_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!page_data) return false;
    const bool ok = fread(page_data, 1, data_size, file) == data_size;
    if (ok) memcpy(buffer, page_data, data_size);
    free(page_data);
    return ok;
  }
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

bool ink_reader_book_decode_page(const ink_reader_book_t *book,
                                 size_t page_index, uint8_t *buffer,
                                 size_t length) {
  if (!book || !book->pages || page_index >= book->page_count) return false;
  return load_page(book->file, &book->pages[page_index], buffer, length);
}

bool ink_reader_book_load_page(ink_reader_book_t *book, size_t page_index,
                               uint8_t *buffer, size_t length) {
  if (!ink_reader_book_decode_page(book, page_index, buffer, length))
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

size_t ink_reader_book_chapter_count(const ink_reader_book_t *book) {
  return book ? book->chapter_count : 0U;
}

const ink_reader_chapter_t *ink_reader_book_chapter_at(
    const ink_reader_book_t *book, size_t chapter_index) {
  if (!book || !book->chapters || chapter_index >= book->chapter_count)
    return NULL;
  return &book->chapters[chapter_index];
}

const ink_reader_chapter_t *ink_reader_book_chapter_for_page(
    const ink_reader_book_t *book, size_t page_index) {
  if (!book || !book->chapters || book->chapter_count == 0U ||
      page_index >= book->page_count)
    return NULL;
  const ink_reader_chapter_t *chapter = &book->chapters[0];
  for (size_t i = 1; i < book->chapter_count; ++i) {
    if (page_index < book->chapters[i].start_page) break;
    chapter = &book->chapters[i];
  }
  return chapter;
}

static bool chapter_title_has_prefix(const char *title, const char *prefix) {
  if (!title || !prefix) return false;
  const size_t prefix_length = strlen(prefix);
  return prefix_length > 0U && strncmp(title, prefix, prefix_length) == 0;
}

bool ink_reader_chapter_title_is_displayable(const char *title) {
  if (!title || title[0] == '\0') return false;
  if (strcmp(title, "译序") == 0 || strcmp(title, "序") == 0 ||
      strcmp(title, "序章") == 0 || strcmp(title, "上篇") == 0 ||
      strcmp(title, "下篇") == 0)
    return false;
  return !chapter_title_has_prefix(title, "楔子") &&
         !chapter_title_has_prefix(title, "前言") &&
         !chapter_title_has_prefix(title, "后记") &&
         !chapter_title_has_prefix(title, "附录");
}

bool ink_reader_book_resolve_display_chapter(
    const ink_reader_book_t *book, size_t page_index,
    size_t *display_chapter_index, size_t *display_chapter_total,
    const ink_reader_chapter_t **display_chapter) {
  if (display_chapter_index) *display_chapter_index = 0U;
  if (display_chapter_total) *display_chapter_total = 0U;
  if (display_chapter) *display_chapter = NULL;
  if (!book || !book->chapters || page_index >= book->page_count) return false;

  const ink_reader_chapter_t *resolved = NULL;
  size_t resolved_index = 0U;
  size_t total = 0U;
  for (size_t i = 0; i < book->chapter_count; ++i) {
    const ink_reader_chapter_t *chapter = &book->chapters[i];
    if (!ink_reader_chapter_title_is_displayable(chapter->title)) continue;
    if (chapter->start_page <= page_index) {
      resolved = chapter;
      resolved_index = total;
    }
    ++total;
  }
  if (display_chapter_total) *display_chapter_total = total;
  if (!resolved) return false;
  if (display_chapter_index) *display_chapter_index = resolved_index;
  if (display_chapter) *display_chapter = resolved;
  return true;
}

bool ink_reader_book_jump_to_chapter(ink_reader_book_t *book,
                                     size_t chapter_index) {
  const ink_reader_chapter_t *chapter =
      ink_reader_book_chapter_at(book, chapter_index);
  if (!chapter || chapter->start_page >= book->page_count) return false;
  book->current_page = chapter->start_page;
  return true;
}

bool ink_reader_core_self_test(void) {
  uint8_t h[XTC_HEADER_SIZE] = {'X', 'T', 'C', 0, 1, 0, 2, 0};
  h[24] = 56;
  h[32] = 88;
  xtc_header_t header;
  if (!parse_header(h, sizeof(h), &header) || header.page_count != 2 ||
      header.index_offset != 56 || header.data_offset != 88)
    return false;
  h[0] = 'B';
  if (parse_header(h, sizeof(h), &header)) return false;
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
