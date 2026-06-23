#include "ink_xtc_book.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"

#include "epd_gdey0426t82.h"

static const char *TAG = "ink_xtc_book";

enum {
    INK_XTG_HEADER_SIZE = 22,
    INK_XTG_MAGIC = 0x00475458UL,
    INK_XTH_MAGIC = 0x00485458UL,
    INK_XTC_METADATA_SIZE = 256,
    INK_XTC_CHAPTER_ENTRY_SIZE = 96,
};

static uint16_t read_le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0]
        | ((uint32_t)p[1] << 8)
        | ((uint32_t)p[2] << 16)
        | ((uint32_t)p[3] << 24);
}

static void xth_planes_to_bw_bitmap(
    const uint8_t *plane0,
    const uint8_t *plane1,
    uint16_t width,
    uint16_t height,
    uint8_t *bitmap_buffer,
    size_t bitmap_length)
{
    uint32_t col_bytes = 0;
    uint32_t row_bytes = 0;

    if (plane0 == NULL || plane1 == NULL || bitmap_buffer == NULL || bitmap_length < EPD_GDEY0426T82_BUFFER_SIZE) {
        return;
    }

    memset(bitmap_buffer, 0xFF, EPD_GDEY0426T82_BUFFER_SIZE);
    col_bytes = (uint32_t)((height + 7U) / 8U);
    row_bytes = (uint32_t)((width + 7U) / 8U);

    for (uint16_t x = 0; x < width; ++x) {
        uint32_t col_index = (uint32_t)(width - 1U - x);
        for (uint16_t y = 0; y < height; ++y) {
            uint32_t src_byte = col_index * col_bytes + (uint32_t)(y / 8U);
            uint8_t mask = (uint8_t)(0x80U >> (y & 7U));
            uint8_t bit0 = (plane0[src_byte] & mask) != 0U ? 1U : 0U;
            uint8_t bit1 = (plane1[src_byte] & mask) != 0U ? 1U : 0U;
            uint8_t level = (uint8_t)((bit1 << 1U) | bit0);

            if (level >= 2U) {
                uint32_t dst_byte = (uint32_t)y * row_bytes + (uint32_t)(x / 8U);
                bitmap_buffer[dst_byte] &= (uint8_t)~(0x80U >> (x & 7U));
            }
        }
    }
}

static void ink_xtc_book_release_owned_resources(
    ink_xtc_book_t *book,
    const ink_xtc_page_entry_t *preserved_page_entries)
{
    if (book == NULL) {
        return;
    }

    if (book->file != NULL) {
        fclose(book->file);
        book->file = NULL;
    }
    if (book->page_entries != NULL && book->page_entries != preserved_page_entries) {
        free(book->page_entries);
        book->page_entries = NULL;
    }
    if (book->chapter_entries != NULL) {
        free(book->chapter_entries);
        book->chapter_entries = NULL;
    }
    book->chapter_entry_count = 0U;
    memset(&book->metadata, 0, sizeof(book->metadata));
    book->has_metadata = false;
}

static bool load_page_bitmap_from_entry(
    const ink_xtc_book_t *book,
    const ink_xtc_page_entry_t *entry,
    uint8_t *bitmap_buffer,
    size_t bitmap_length)
{
    uint8_t xtg_header[INK_XTG_HEADER_SIZE];
    uint32_t xtg_magic = 0;
    uint16_t width = 0;
    uint16_t height = 0;
    uint32_t data_size = 0;

    if (book == NULL
        || entry == NULL
        || bitmap_buffer == NULL
        || bitmap_length < EPD_GDEY0426T82_BUFFER_SIZE
        || book->file == NULL) {
        return false;
    }

    if (entry->encoded_size < INK_XTG_HEADER_SIZE) {
        ESP_LOGW(TAG, "xtc page entry invalid: size=%u", (unsigned)entry->encoded_size);
        return false;
    }
    if (fseeko(book->file, (off_t)entry->data_offset, SEEK_SET) != 0) {
        ESP_LOGW(TAG, "xtc page seek failed: offset=%u errno=%d", (unsigned)entry->data_offset, errno);
        return false;
    }
    if (fread(xtg_header, 1, sizeof(xtg_header), book->file) != sizeof(xtg_header)) {
        ESP_LOGW(TAG, "xtc page header read failed: offset=%u", (unsigned)entry->data_offset);
        return false;
    }

    xtg_magic = read_le32(xtg_header + 0);
    width = read_le16(xtg_header + 4);
    height = read_le16(xtg_header + 6);
    data_size = read_le32(xtg_header + 10);

    if (width != EPD_GDEY0426T82_WIDTH || height != EPD_GDEY0426T82_HEIGHT) {
        ESP_LOGW(
            TAG,
            "xtc page size mismatch: offset=%u magic=0x%08x width=%u height=%u expected=%u x %u",
            (unsigned)entry->data_offset,
            (unsigned)xtg_magic,
            (unsigned)width,
            (unsigned)height,
            (unsigned)EPD_GDEY0426T82_WIDTH,
            (unsigned)EPD_GDEY0426T82_HEIGHT);
        return false;
    }

    if (xtg_magic == INK_XTG_MAGIC) {
        if (data_size != EPD_GDEY0426T82_BUFFER_SIZE
            || entry->encoded_size < INK_XTG_HEADER_SIZE + data_size) {
            ESP_LOGW(
                TAG,
                "xtg page header mismatch: offset=%u data=%u encoded=%u expected_data=%u",
                (unsigned)entry->data_offset,
                (unsigned)data_size,
                (unsigned)entry->encoded_size,
                (unsigned)EPD_GDEY0426T82_BUFFER_SIZE);
            return false;
        }
        if (fread(bitmap_buffer, 1, data_size, book->file) != data_size) {
            ESP_LOGW(TAG, "xtg page bitmap read failed: offset=%u data=%u", (unsigned)entry->data_offset, (unsigned)data_size);
            return false;
        }
        return true;
    }

    if (xtg_magic == INK_XTH_MAGIC) {
        size_t plane_size = ((size_t)width * (size_t)height + 7U) / 8U;
        uint8_t *plane0 = malloc(plane_size);
        uint8_t *plane1 = malloc(plane_size);
        bool ok = false;

        if (data_size != plane_size * 2U || entry->encoded_size < INK_XTG_HEADER_SIZE + data_size) {
            ESP_LOGW(
                TAG,
                "xth page header mismatch: offset=%u data=%u encoded=%u expected_data=%u",
                (unsigned)entry->data_offset,
                (unsigned)data_size,
                (unsigned)entry->encoded_size,
                (unsigned)(plane_size * 2U));
            free(plane0);
            free(plane1);
            return false;
        }
        if (plane0 == NULL || plane1 == NULL) {
            ESP_LOGW(TAG, "xth plane alloc failed: plane_size=%u", (unsigned)plane_size);
            free(plane0);
            free(plane1);
            return false;
        }
        if (fread(plane0, 1, plane_size, book->file) != plane_size
            || fread(plane1, 1, plane_size, book->file) != plane_size) {
            ESP_LOGW(TAG, "xth plane read failed: offset=%u plane_size=%u", (unsigned)entry->data_offset, (unsigned)plane_size);
            goto xth_cleanup;
        }

        xth_planes_to_bw_bitmap(plane0, plane1, width, height, bitmap_buffer, bitmap_length);
        ok = true;

xth_cleanup:
        free(plane0);
        free(plane1);
        return ok;
    }

    ESP_LOGW(
        TAG,
        "xtc page magic unsupported: offset=%u magic=0x%08x width=%u height=%u data=%u encoded=%u",
        (unsigned)entry->data_offset,
        (unsigned)xtg_magic,
        (unsigned)width,
        (unsigned)height,
        (unsigned)data_size,
        (unsigned)entry->encoded_size);
    return false;
}

static bool load_metadata_and_chapters(ink_xtc_book_t *book, FILE *file)
{
    uint8_t metadata_bytes[INK_XTC_METADATA_SIZE];
    uint8_t *chapter_bytes = NULL;
    ink_xtc_chapter_entry_t *chapter_entries = NULL;
    size_t chapter_count = 0U;
    size_t chapter_bytes_length = 0U;
    bool ok = false;

    if (book == NULL || file == NULL) {
        return false;
    }

    memset(&book->metadata, 0, sizeof(book->metadata));
    book->has_metadata = false;
    book->chapter_entry_count = 0U;
    if (book->chapter_entries != NULL) {
        free(book->chapter_entries);
        book->chapter_entries = NULL;
    }

    if (book->header.has_metadata) {
        if (fseeko(file, (off_t)book->header.metadata_offset, SEEK_SET) != 0) {
            ESP_LOGW(TAG, "xtc metadata seek failed: path=%s offset=%u errno=%d", book->path, (unsigned)book->header.metadata_offset, errno);
            return false;
        }
        if (fread(metadata_bytes, 1, sizeof(metadata_bytes), file) != sizeof(metadata_bytes)) {
            ESP_LOGW(TAG, "xtc metadata read failed: path=%s", book->path);
            return false;
        }
        if (!ink_xtc_parse_metadata_bytes(metadata_bytes, sizeof(metadata_bytes), &book->metadata)) {
            ESP_LOGW(TAG, "xtc metadata parse failed: path=%s", book->path);
            return false;
        }
        book->has_metadata = true;
    }

    if (book->header.has_chapters) {
        if (book->header.page_index_offset <= book->header.chapter_offset) {
            return false;
        }
        chapter_bytes_length = (size_t)(book->header.page_index_offset - book->header.chapter_offset);
        if ((chapter_bytes_length % INK_XTC_CHAPTER_ENTRY_SIZE) != 0U) {
            ESP_LOGW(TAG, "xtc chapter span invalid: path=%s span=%u", book->path, (unsigned)chapter_bytes_length);
            return false;
        }
        chapter_count = chapter_bytes_length / INK_XTC_CHAPTER_ENTRY_SIZE;
        if (book->has_metadata
            && book->metadata.chapter_count > 0U
            && book->metadata.chapter_count != chapter_count) {
            ESP_LOGW(
                TAG,
                "xtc chapter count mismatch: path=%s metadata=%u span=%u",
                book->path,
                (unsigned)book->metadata.chapter_count,
                (unsigned)chapter_count);
        }
        if (chapter_bytes_length > 0U) {
            chapter_bytes = malloc(chapter_bytes_length);
            chapter_entries = calloc(chapter_count, sizeof(*chapter_entries));
            if (chapter_bytes == NULL || chapter_entries == NULL) {
                ESP_LOGW(TAG, "xtc chapter alloc failed: path=%s count=%u", book->path, (unsigned)chapter_count);
                goto cleanup;
            }
            if (fseeko(file, (off_t)book->header.chapter_offset, SEEK_SET) != 0) {
                ESP_LOGW(TAG, "xtc chapter seek failed: path=%s offset=%u errno=%d", book->path, (unsigned)book->header.chapter_offset, errno);
                goto cleanup;
            }
            if (fread(chapter_bytes, 1, chapter_bytes_length, file) != chapter_bytes_length) {
                ESP_LOGW(TAG, "xtc chapter read failed: path=%s count=%u", book->path, (unsigned)chapter_count);
                goto cleanup;
            }
            if (!ink_xtc_parse_chapter_bytes(
                    chapter_bytes,
                    chapter_bytes_length,
                    (uint32_t)book->header.page_count,
                    chapter_entries,
                    chapter_count,
                    &book->chapter_entry_count)) {
                ESP_LOGW(TAG, "xtc chapter parse failed: path=%s count=%u", book->path, (unsigned)chapter_count);
                goto cleanup;
            }
            book->chapter_entries = chapter_entries;
            chapter_entries = NULL;
        }
    }

    ok = true;

cleanup:
    if (chapter_bytes != NULL) {
        free(chapter_bytes);
    }
    if (chapter_entries != NULL) {
        free(chapter_entries);
    }
    return ok;
}

void ink_xtc_book_prepare_default(ink_xtc_book_t *book)
{
    if (book == NULL) {
        return;
    }

    memset(book, 0, sizeof(*book));
}

void ink_xtc_book_close(ink_xtc_book_t *book)
{
    if (book == NULL) {
        return;
    }

    ink_xtc_book_release_owned_resources(book, NULL);
    ink_xtc_book_prepare_default(book);
}

bool ink_xtc_book_open(ink_xtc_book_t *book, const char *path)
{
    uint8_t header_bytes[56];
    ink_xtc_page_entry_t *entries = NULL;
    FILE *file = NULL;
    size_t entry_count = 0;
    uint64_t index_bytes = 0;
    uint8_t *index_buffer = NULL;
    bool ok = false;

    if (book == NULL || path == NULL || path[0] == '\0') {
        return false;
    }

    file = fopen(path, "rb");
    if (file == NULL) {
        ESP_LOGW(TAG, "xtc fopen failed: path=%s errno=%d", path, errno);
        return false;
    }
    if (fread(header_bytes, 1, sizeof(header_bytes), file) != sizeof(header_bytes)) {
        ESP_LOGW(TAG, "xtc header read failed: path=%s", path);
        goto cleanup;
    }
    if (!ink_xtc_parse_header_bytes(header_bytes, sizeof(header_bytes), &book->header)) {
        ESP_LOGW(
            TAG,
            "xtc header parse failed: path=%s magic=%02x%02x%02x%02x",
            path,
            (unsigned)header_bytes[0],
            (unsigned)header_bytes[1],
            (unsigned)header_bytes[2],
            (unsigned)header_bytes[3]);
        goto cleanup;
    }

    index_bytes = (uint64_t)book->header.page_count * 16U;
    if (index_bytes == 0U || index_bytes > SIZE_MAX) {
        goto cleanup;
    }
    index_buffer = malloc((size_t)index_bytes);
    entries = calloc(book->header.page_count, sizeof(*entries));
    if (index_buffer == NULL || entries == NULL) {
        ESP_LOGW(TAG, "xtc alloc failed: path=%s index_bytes=%u page_count=%u", path, (unsigned)index_bytes, (unsigned)book->header.page_count);
        goto cleanup;
    }

    if (fseeko(file, (off_t)book->header.page_index_offset, SEEK_SET) != 0) {
        ESP_LOGW(TAG, "xtc index seek failed: path=%s offset=%u errno=%d", path, (unsigned)book->header.page_index_offset, errno);
        goto cleanup;
    }
    if (fread(index_buffer, 1, (size_t)index_bytes, file) != (size_t)index_bytes) {
        ESP_LOGW(TAG, "xtc index read failed: path=%s offset=%u bytes=%u", path, (unsigned)book->header.page_index_offset, (unsigned)index_bytes);
        goto cleanup;
    }
    if (!ink_xtc_parse_page_index_bytes(
            index_buffer,
            (size_t)index_bytes,
            &book->header,
            entries,
            book->header.page_count,
            &entry_count)) {
        ESP_LOGW(TAG, "xtc index parse failed: path=%s page_count=%u", path, (unsigned)book->header.page_count);
        goto cleanup;
    }
    if (!ink_xtc_book_attach_page_entries(book, path, &book->header, entries, entry_count)) {
        ESP_LOGW(TAG, "xtc attach page entries failed: path=%s entry_count=%u", path, (unsigned)entry_count);
        goto cleanup;
    }
    entries = NULL;
    book->file = file;
    file = NULL;
    if (!load_metadata_and_chapters(book, book->file)) {
        goto cleanup;
    }
    ok = true;
    ESP_LOGI(
        TAG,
        "xtc open ok: path=%s pages=%u chapters=%u index_offset=%u data_offset=%u first_offset=%u first_size=%u hq=%u",
        path,
        (unsigned)book->page_entry_count,
        (unsigned)book->chapter_entry_count,
        (unsigned)book->header.page_index_offset,
        (unsigned)book->header.data_offset,
        (unsigned)book->page_entries[0].data_offset,
        (unsigned)book->page_entries[0].encoded_size,
        book->header.is_hq_container ? 1U : 0U);

cleanup:
    if (index_buffer != NULL) {
        free(index_buffer);
    }
    if (entries != NULL) {
        free(entries);
    }
    if (file != NULL) {
        fclose(file);
    }
    if (!ok) {
        ink_xtc_book_close(book);
    }
    return ok;
}

bool ink_xtc_book_attach_page_entries(
    ink_xtc_book_t *book,
    const char *path,
    const ink_xtc_file_header_t *header,
    ink_xtc_page_entry_t *page_entries,
    size_t page_entry_count)
{
    char path_copy[256];
    ink_xtc_file_header_t header_copy;
    const bool reusing_owned_page_entries =
        book != NULL && page_entries != NULL && book->page_entries == page_entries;

    if (book == NULL
        || path == NULL
        || path[0] == '\0'
        || strlen(path) >= sizeof(path_copy)
        || header == NULL
        || page_entries == NULL
        || page_entry_count == 0U
        || page_entry_count != header->page_count) {
        return false;
    }

    snprintf(path_copy, sizeof(path_copy), "%s", path);
    header_copy = *header;

    ink_xtc_book_release_owned_resources(book, reusing_owned_page_entries ? page_entries : NULL);
    memset(book->path, 0, sizeof(book->path));
    memset(&book->header, 0, sizeof(book->header));
    memset(&book->metadata, 0, sizeof(book->metadata));
    book->has_metadata = false;
    book->opened = false;
    book->page_entry_count = 0U;
    book->chapter_entry_count = 0U;
    book->current_page = 0U;
    book->opened = true;
    snprintf(book->path, sizeof(book->path), "%s", path_copy);
    book->header = header_copy;
    book->page_entries = page_entries;
    book->page_entry_count = page_entry_count;
    book->current_page = 0U;
    return true;
}

bool ink_xtc_book_set_current_page(ink_xtc_book_t *book, size_t page_index)
{
    if (book == NULL || !book->opened || page_index >= book->page_entry_count) {
        return false;
    }

    book->current_page = page_index;
    return true;
}

bool ink_xtc_book_next_page(ink_xtc_book_t *book)
{
    if (book == NULL || !book->opened) {
        return false;
    }
    if (book->current_page + 1U >= book->page_entry_count) {
        return false;
    }

    return ink_xtc_book_set_current_page(book, book->current_page + 1U);
}

bool ink_xtc_book_previous_page(ink_xtc_book_t *book)
{
    if (book == NULL || !book->opened || book->current_page == 0U) {
        return false;
    }

    return ink_xtc_book_set_current_page(book, book->current_page - 1U);
}

const ink_xtc_page_entry_t *ink_xtc_book_current_page_entry(const ink_xtc_book_t *book)
{
    if (book == NULL || !book->opened || book->current_page >= book->page_entry_count) {
        return NULL;
    }

    return &book->page_entries[book->current_page];
}

const ink_xtc_chapter_entry_t *ink_xtc_book_chapter_for_page(const ink_xtc_book_t *book, size_t page_index, size_t *chapter_index_out)
{
    const ink_xtc_chapter_entry_t *result = NULL;
    size_t result_index = 0U;

    if (chapter_index_out != NULL) {
        *chapter_index_out = 0U;
    }
    if (book == NULL || !book->opened || book->chapter_entries == NULL || book->chapter_entry_count == 0U) {
        return NULL;
    }

    for (size_t i = 0; i < book->chapter_entry_count; ++i) {
        const ink_xtc_chapter_entry_t *chapter = &book->chapter_entries[i];
        if (page_index < chapter->start_page) {
            break;
        }
        result = chapter;
        result_index = i;
    }

    if (result == NULL) {
        result = &book->chapter_entries[0];
        result_index = 0U;
    }
    if (chapter_index_out != NULL) {
        *chapter_index_out = result_index;
    }
    return result;
}

const ink_xtc_chapter_entry_t *ink_xtc_book_current_chapter(const ink_xtc_book_t *book, size_t *chapter_index_out)
{
    return ink_xtc_book_chapter_for_page(book, book != NULL ? book->current_page : 0U, chapter_index_out);
}

bool ink_xtc_book_load_page_bitmap(
    const ink_xtc_book_t *book,
    size_t page_index,
    uint8_t *bitmap_buffer,
    size_t bitmap_length)
{
    if (book == NULL || !book->opened || page_index >= book->page_entry_count) {
        return false;
    }

    return load_page_bitmap_from_entry(
        book,
        &book->page_entries[page_index],
        bitmap_buffer,
        bitmap_length);
}

bool ink_xtc_book_load_current_page_bitmap(
    const ink_xtc_book_t *book,
    uint8_t *bitmap_buffer,
    size_t bitmap_length)
{
    return ink_xtc_book_load_page_bitmap(book, book != NULL ? book->current_page : 0U, bitmap_buffer, bitmap_length);
}

bool ink_xtc_book_self_test(void)
{
    static const uint8_t kHeaderBytes[56] = {
        'X', 'T', 'C', 0,
        1, 0,
        3, 0,
        0, 1, 0, 1,
        1, 0, 0, 0,
        0x38, 0, 0, 0, 0, 0, 0, 0,
        0x98, 0x01, 0, 0, 0, 0, 0, 0,
        0xC8, 0x01, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0,
        0x38, 0x01, 0, 0, 0, 0, 0, 0
    };
    static const uint8_t kPageIndexBytes[3 * 16] = {
        0xC8, 0x01, 0, 0, 0, 0, 0, 0,
        0x76, 0xBB, 0, 0,
        0xE0, 0x01,
        0x20, 0x03,

        0x3E, 0xBD, 0, 0, 0, 0, 0, 0,
        0x76, 0xBB, 0, 0,
        0xE0, 0x01,
        0x20, 0x03,

        0xB4, 0x78, 0x01, 0, 0, 0, 0, 0,
        0x76, 0xBB, 0, 0,
        0xE0, 0x01,
        0x20, 0x03
    };
    static const ink_xtc_chapter_entry_t kChapters[2] = {
        {.name = "C1", .start_page = 0U, .end_page = 1U},
        {.name = "C2", .start_page = 2U, .end_page = 2U},
    };
    ink_xtc_book_t book;
    ink_xtc_file_header_t header;
    ink_xtc_page_entry_t *entries = NULL;
    ink_xtc_page_entry_t *entries_reattach = NULL;
    ink_xtc_page_entry_t *entries_alias = NULL;
    size_t entry_count = 0;
    size_t chapter_index = 0U;
    const ink_xtc_chapter_entry_t *chapter = NULL;
    bool ok = false;

    ink_xtc_book_prepare_default(&book);
    if (book.opened || book.page_entry_count != 0U) {
        return false;
    }
    if (!ink_xtc_parse_header_bytes(kHeaderBytes, sizeof(kHeaderBytes), &header)) {
        return false;
    }

    entries = calloc(header.page_count, sizeof(*entries));
    if (entries == NULL) {
        return false;
    }
    if (!ink_xtc_parse_page_index_bytes(
            kPageIndexBytes,
            sizeof(kPageIndexBytes),
            &header,
            entries,
            header.page_count,
            &entry_count)) {
        goto cleanup;
    }
    if (!ink_xtc_book_attach_page_entries(&book, "/sdcard/books/demo.xtc", &header, entries, entry_count)) {
        goto cleanup;
    }
    entries = NULL;

    book.chapter_entries = calloc(2U, sizeof(*book.chapter_entries));
    if (book.chapter_entries == NULL) {
        goto cleanup;
    }
    memcpy(book.chapter_entries, kChapters, sizeof(kChapters));
    book.chapter_entry_count = 2U;

    if (ink_xtc_book_attach_page_entries(
            &book,
            "/sdcard/books/this/path/is/intentionally/made/very/very/very/very/very/very/very/very/very/very/"
            "very/very/very/very/very/very/very/very/very/very/very/very/very/very/very/very/very/very/very/"
            "very/very/very/very/very/very/very/very/very/very/very/very/very/very/very/very/very/very/very/very/very/very/long/"
            "demo.xtc",
            &header,
            book.page_entries,
            book.page_entry_count)) {
        goto cleanup;
    }

    entries_reattach = calloc(header.page_count, sizeof(*entries_reattach));
    if (entries_reattach == NULL) {
        goto cleanup;
    }
    for (size_t i = 0; i < header.page_count; ++i) {
        entries_reattach[i] = book.page_entries[i];
    }

    if (!ink_xtc_book_set_current_page(&book, 0U)) {
        goto cleanup;
    }
    if (!ink_xtc_book_next_page(&book) || book.current_page != 1U) {
        goto cleanup;
    }
    if (ink_xtc_book_current_page_entry(&book) == NULL
        || ink_xtc_book_current_page_entry(&book)->data_offset != 48446U) {
        goto cleanup;
    }
    chapter = ink_xtc_book_current_chapter(&book, &chapter_index);
    if (chapter == NULL || chapter_index != 0U || strcmp(chapter->name, "C1") != 0) {
        goto cleanup;
    }
    if (!ink_xtc_book_previous_page(&book) || book.current_page != 0U) {
        goto cleanup;
    }
    if (ink_xtc_book_previous_page(&book)) {
        goto cleanup;
    }
    if (!ink_xtc_book_set_current_page(&book, 2U) || ink_xtc_book_next_page(&book)) {
        goto cleanup;
    }
    chapter = ink_xtc_book_current_chapter(&book, &chapter_index);
    if (chapter == NULL || chapter_index != 1U || strcmp(chapter->name, "C2") != 0) {
        goto cleanup;
    }
    if (!ink_xtc_book_attach_page_entries(
            &book,
            "/sdcard/books/reattach.xtc",
            &header,
            entries_reattach,
            header.page_count)) {
        goto cleanup;
    }
    entries_reattach = NULL;
    if (strcmp(book.path, "/sdcard/books/reattach.xtc") != 0
        || book.current_page != 0U
        || book.page_entries == NULL
        || book.page_entries[2].data_offset != 96436U) {
        goto cleanup;
    }
    entries_alias = calloc(book.page_entry_count, sizeof(*entries_alias));
    if (entries_alias == NULL) {
        goto cleanup;
    }
    for (size_t i = 0; i < book.page_entry_count; ++i) {
        entries_alias[i] = book.page_entries[i];
    }
    if (!ink_xtc_book_attach_page_entries(
            &book,
            book.path,
            &book.header,
            entries_alias,
            book.page_entry_count)) {
        goto cleanup;
    }
    entries_alias = NULL;
    if (strcmp(book.path, "/sdcard/books/reattach.xtc") != 0
        || book.header.page_count != 3U
        || book.page_entries == NULL
        || book.page_entries[1].width != 480U) {
        goto cleanup;
    }
    if (!ink_xtc_book_attach_page_entries(
            &book,
            book.path,
            &book.header,
            book.page_entries,
            book.page_entry_count)) {
        goto cleanup;
    }
    if (strcmp(book.path, "/sdcard/books/reattach.xtc") != 0
        || book.header.page_count != 3U
        || book.page_entries == NULL
        || book.page_entries[0].height != 800U
        || book.page_entries[2].width != 480U) {
        goto cleanup;
    }

    ok = true;

cleanup:
    if (entries != NULL) {
        free(entries);
    }
    if (entries_reattach != NULL) {
        free(entries_reattach);
    }
    if (entries_alias != NULL) {
        free(entries_alias);
    }
    ink_xtc_book_close(&book);
    return ok;
}
