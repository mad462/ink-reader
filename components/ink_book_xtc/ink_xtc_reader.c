#include "ink_xtc_reader.h"

#include <string.h>

enum {
    INK_XTC_HEADER_SIZE_LEGACY = 48,
    INK_XTC_HEADER_SIZE = 56,
    INK_XTC_METADATA_SIZE = 256,
    INK_XTC_CHAPTER_ENTRY_SIZE = 96,
    INK_XTC_PAGE_INDEX_ENTRY_SIZE = 16,
    INK_XTC_MAGIC = 0x00435458UL,
    INK_XTCH_MAGIC = 0x48435458UL,
    INK_XTC_VERSION_SUPPORTED = 1,
    INK_XTC_VERSION_SUPPORTED_LEGACY = 256,
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

static uint64_t read_le64(const uint8_t *p)
{
    return (uint64_t)p[0]
        | ((uint64_t)p[1] << 8)
        | ((uint64_t)p[2] << 16)
        | ((uint64_t)p[3] << 24)
        | ((uint64_t)p[4] << 32)
        | ((uint64_t)p[5] << 40)
        | ((uint64_t)p[6] << 48)
        | ((uint64_t)p[7] << 56);
}

static bool range_has_valid_end_u64(uint64_t offset, uint64_t length, uint64_t *end_out)
{
    const uint64_t end = offset + length;
    if (end < offset) {
        return false;
    }
    if (end_out != NULL) {
        *end_out = end;
    }
    return true;
}

static void copy_utf8_field(char *dst, size_t dst_size, const uint8_t *src, size_t src_size)
{
    size_t copy_len = 0;

    if (dst == NULL || dst_size == 0U) {
        return;
    }
    dst[0] = '\0';
    if (src == NULL || src_size == 0U) {
        return;
    }

    while (copy_len < src_size && src[copy_len] != 0U) {
        ++copy_len;
    }
    if (copy_len >= dst_size) {
        copy_len = dst_size - 1U;
    }
    if (copy_len > 0U) {
        memcpy(dst, src, copy_len);
    }
    dst[copy_len] = '\0';
}

bool ink_xtc_parse_header_bytes(
    const uint8_t *raw,
    size_t length,
    ink_xtc_file_header_t *out)
{
    uint64_t header_size = INK_XTC_HEADER_SIZE;

    if (raw == NULL || out == NULL || length < INK_XTC_HEADER_SIZE) {
        return false;
    }

    memset(out, 0, sizeof(*out));
    out->magic = read_le32(raw + 0);
    out->version = read_le16(raw + 4);
    out->page_count = read_le16(raw + 6);
    out->read_direction = raw[8];
    out->has_metadata = raw[9] != 0U;
    out->has_thumbnails = raw[10] != 0U;
    out->has_chapters = raw[11] != 0U;
    out->current_page = read_le32(raw + 12);
    out->metadata_offset = read_le64(raw + 16);
    out->page_index_offset = read_le64(raw + 24);
    out->data_offset = read_le64(raw + 32);
    if (out->magic != INK_XTC_MAGIC && out->magic != INK_XTCH_MAGIC) {
        return false;
    }
    if (out->version != INK_XTC_VERSION_SUPPORTED
        && out->version != INK_XTC_VERSION_SUPPORTED_LEGACY) {
        return false;
    }
    out->is_hq_container = out->magic == INK_XTCH_MAGIC;

    if (out->version == INK_XTC_VERSION_SUPPORTED_LEGACY) {
        header_size = INK_XTC_HEADER_SIZE_LEGACY;
        out->thumbnail_offset = read_le64(raw + 40);
        out->chapter_offset = 0U;
        if (out->has_chapters) {
            if (out->has_metadata) {
                out->chapter_offset = out->metadata_offset + INK_XTC_METADATA_SIZE;
            } else {
                out->chapter_offset = INK_XTC_HEADER_SIZE_LEGACY;
            }
        }
    } else {
        out->thumbnail_offset = read_le64(raw + 40);
        out->chapter_offset = read_le64(raw + 48);
    }

    if (out->page_count == 0U) {
        return false;
    }
    if (out->page_index_offset < header_size || out->data_offset <= out->page_index_offset) {
        return false;
    }
    {
        uint64_t metadata_end = 0;
        uint64_t chapter_end = 0;
        uint64_t page_index_end = 0;
        const uint64_t expected_index_length = (uint64_t)out->page_count * INK_XTC_PAGE_INDEX_ENTRY_SIZE;

        if (!range_has_valid_end_u64(out->page_index_offset, expected_index_length, &page_index_end)) {
            return false;
        }
        if (out->has_metadata) {
            if (out->metadata_offset < header_size
                || !range_has_valid_end_u64(out->metadata_offset, INK_XTC_METADATA_SIZE, &metadata_end)
                || metadata_end > out->page_index_offset) {
                return false;
            }
        } else if (out->metadata_offset != 0U) {
            return false;
        }
        if (out->has_chapters) {
            if (out->chapter_offset < header_size
                || out->chapter_offset >= out->page_index_offset) {
                return false;
            }
            if (out->has_metadata && out->chapter_offset < metadata_end) {
                return false;
            }
            if (((out->page_index_offset - out->chapter_offset) % INK_XTC_CHAPTER_ENTRY_SIZE) != 0U) {
                return false;
            }
            if (!range_has_valid_end_u64(out->chapter_offset, 0U, &chapter_end)) {
                return false;
            }
            (void)chapter_end;
        } else if (out->chapter_offset != 0U) {
            return false;
        }
        if (page_index_end > out->data_offset) {
            return false;
        }
    }

    return true;
}

bool ink_xtc_parse_metadata_bytes(
    const uint8_t *raw,
    size_t length,
    ink_xtc_metadata_t *out)
{
    if (raw == NULL || out == NULL || length < INK_XTC_METADATA_SIZE) {
        return false;
    }

    memset(out, 0, sizeof(*out));
    copy_utf8_field(out->title, sizeof(out->title), raw + 0, 128U);
    copy_utf8_field(out->author, sizeof(out->author), raw + 128, 64U);
    copy_utf8_field(out->publisher, sizeof(out->publisher), raw + 200, 32U);
    copy_utf8_field(out->language, sizeof(out->language), raw + 224, 16U);
    out->create_time = read_le32(raw + 192);
    out->cover_page = read_le16(raw + 244);
    out->chapter_count = read_le16(raw + 196);
    if (out->chapter_count == 0U) {
        out->chapter_count = read_le16(raw + 246);
    }
    return true;
}

bool ink_xtc_parse_page_index_bytes(
    const uint8_t *raw,
    size_t length,
    const ink_xtc_file_header_t *header,
    ink_xtc_page_entry_t *entries,
    size_t entry_capacity,
    size_t *entry_count_out)
{
    uint64_t expected_index_length = 0;
    uint64_t page_index_region_end = 0;

    if (raw == NULL || header == NULL || entries == NULL || entry_count_out == NULL) {
        return false;
    }
    expected_index_length = (uint64_t)header->page_count * INK_XTC_PAGE_INDEX_ENTRY_SIZE;
    if (header->page_count == 0U
        || entry_capacity < header->page_count
        || (uint64_t)length < expected_index_length
        || !range_has_valid_end_u64(
            header->page_index_offset,
            expected_index_length,
            &page_index_region_end)) {
        return false;
    }

    uint64_t previous_data_end = header->data_offset;
    for (size_t i = 0; i < header->page_count; ++i) {
        const uint8_t *entry = raw + (i * INK_XTC_PAGE_INDEX_ENTRY_SIZE);
        ink_xtc_page_entry_t *out = &entries[i];
        uint64_t data_end = 0;

        memset(out, 0, sizeof(*out));
        out->data_offset = read_le64(entry + 0);
        out->encoded_size = read_le32(entry + 8);
        out->width = read_le16(entry + 12);
        out->height = read_le16(entry + 14);

        if (out->data_offset < header->data_offset
            || out->encoded_size == 0U
            || out->width == 0U
            || out->height == 0U
            || !range_has_valid_end_u64(out->data_offset, out->encoded_size, &data_end)
            || out->data_offset < previous_data_end) {
            return false;
        }
        previous_data_end = data_end;
    }

    *entry_count_out = header->page_count;
    return true;
}

bool ink_xtc_parse_chapter_bytes(
    const uint8_t *raw,
    size_t length,
    uint32_t page_count,
    ink_xtc_chapter_entry_t *entries,
    size_t entry_capacity,
    size_t *entry_count_out)
{
    size_t chapter_count = 0U;

    if (entries == NULL || entry_count_out == NULL) {
        return false;
    }
    *entry_count_out = 0U;
    if (raw == NULL) {
        return false;
    }
    if (length == 0U) {
        return true;
    }
    if ((length % INK_XTC_CHAPTER_ENTRY_SIZE) != 0U) {
        return false;
    }

    chapter_count = length / INK_XTC_CHAPTER_ENTRY_SIZE;
    if (entry_capacity < chapter_count) {
        return false;
    }

    for (size_t i = 0; i < chapter_count; ++i) {
        const uint8_t *src = raw + (i * INK_XTC_CHAPTER_ENTRY_SIZE);
        ink_xtc_chapter_entry_t *dst = &entries[i];

        memset(dst, 0, sizeof(*dst));
        copy_utf8_field(dst->name, sizeof(dst->name), src + 0, 80U);
        dst->start_page = read_le16(src + 80);
        dst->end_page = read_le16(src + 82);

        if (dst->end_page < dst->start_page) {
            dst->end_page = dst->start_page;
        }
        if (page_count > 0U) {
            if (dst->start_page >= page_count) {
                return false;
            }
            if (dst->end_page >= page_count) {
                dst->end_page = (uint16_t)(page_count - 1U);
            }
        }
        if (i > 0U && dst->start_page < entries[i - 1U].start_page) {
            return false;
        }
    }

    *entry_count_out = chapter_count;
    return true;
}

bool ink_xtc_reader_self_test(void)
{
    static const uint8_t kGoodHeader[INK_XTC_HEADER_SIZE] = {
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
    static const uint8_t kGoodXtchHeader[INK_XTC_HEADER_SIZE] = {
        'X', 'T', 'C', 'H',
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
    static const uint8_t kGoodLegacyHeader[INK_XTC_HEADER_SIZE] = {
        'X', 'T', 'C', 0,
        0, 1,
        3, 0,
        0, 1, 0, 1,
        1, 0, 0, 0,
        0x30, 0, 0, 0, 0, 0, 0, 0,
        0x98, 0x01, 0, 0, 0, 0, 0, 0,
        0xC8, 0x01, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0,
        'B', 'o', 'o', 'k', ' ', 'T', 'i', 't'
    };
    static const uint8_t kGoodPageIndex[3 * INK_XTC_PAGE_INDEX_ENTRY_SIZE] = {
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
    static uint8_t kMetadata[INK_XTC_METADATA_SIZE] = {0};
    static uint8_t kChapters[2 * INK_XTC_CHAPTER_ENTRY_SIZE] = {0};
    static uint8_t bad_header[INK_XTC_HEADER_SIZE];
    static uint8_t bad_page_index[sizeof(kGoodPageIndex)];
    ink_xtc_file_header_t header;
    ink_xtc_metadata_t metadata;
    ink_xtc_page_entry_t entries[3];
    ink_xtc_chapter_entry_t chapters[2];
    size_t entry_count = 0;
    size_t chapter_count = 0;

    memcpy(kMetadata + 0, "Book Title", 10);
    memcpy(kMetadata + 128, "Author", 6);
    memcpy(kMetadata + 200, "Pub", 3);
    memcpy(kMetadata + 224, "zh-CN", 5);
    kMetadata[192] = 0x78;
    kMetadata[193] = 0x56;
    kMetadata[194] = 0x34;
    kMetadata[195] = 0x12;
    kMetadata[196] = 0x02;
    kMetadata[197] = 0x00;
    kMetadata[244] = 0x02;
    kMetadata[245] = 0x00;

    memcpy(kChapters + 0, "Chapter 1", 9);
    kChapters[80] = 0;
    kChapters[82] = 2;
    memcpy(kChapters + INK_XTC_CHAPTER_ENTRY_SIZE, "Chapter 2", 9);
    kChapters[INK_XTC_CHAPTER_ENTRY_SIZE + 80] = 2;
    kChapters[INK_XTC_CHAPTER_ENTRY_SIZE + 82] = 2;

    if (!ink_xtc_parse_header_bytes(kGoodHeader, sizeof(kGoodHeader), &header)) {
        return false;
    }
    if (header.page_count != 3U
        || header.version != INK_XTC_VERSION_SUPPORTED
        || header.is_hq_container
        || header.metadata_offset != 56U
        || header.page_index_offset != 408U
        || header.data_offset != 456U) {
        return false;
    }
    if (!ink_xtc_parse_header_bytes(kGoodXtchHeader, sizeof(kGoodXtchHeader), &header)) {
        return false;
    }
    if (!header.is_hq_container || header.magic != INK_XTCH_MAGIC) {
        return false;
    }
    if (!ink_xtc_parse_header_bytes(kGoodLegacyHeader, sizeof(kGoodLegacyHeader), &header)) {
        return false;
    }
    if (header.version != INK_XTC_VERSION_SUPPORTED_LEGACY
        || header.metadata_offset != 48U
        || header.chapter_offset != 304U
        || header.page_index_offset != 408U
        || header.data_offset != 456U) {
        return false;
    }
    if (!ink_xtc_parse_metadata_bytes(kMetadata, sizeof(kMetadata), &metadata)) {
        return false;
    }
    if (strcmp(metadata.title, "Book Title") != 0
        || strcmp(metadata.author, "Author") != 0
        || strcmp(metadata.publisher, "Pub") != 0
        || strcmp(metadata.language, "zh-CN") != 0
        || metadata.chapter_count != 2U) {
        return false;
    }
    if (!ink_xtc_parse_header_bytes(kGoodHeader, sizeof(kGoodHeader), &header)) {
        return false;
    }
    if (!ink_xtc_parse_page_index_bytes(
            kGoodPageIndex,
            sizeof(kGoodPageIndex),
            &header,
            entries,
            sizeof(entries) / sizeof(entries[0]),
            &entry_count)) {
        return false;
    }
    if (entry_count != 3U
        || entries[0].data_offset != 456U
        || entries[1].width != 480U
        || entries[2].height != 800U) {
        return false;
    }
    if (!ink_xtc_parse_chapter_bytes(kChapters, sizeof(kChapters), 3U, chapters, 2U, &chapter_count)) {
        return false;
    }
    if (chapter_count != 2U
        || strcmp(chapters[0].name, "Chapter 1") != 0
        || chapters[0].start_page != 0U
        || chapters[0].end_page != 2U
        || chapters[1].start_page != 2U) {
        return false;
    }

    if (ink_xtc_parse_header_bytes(kGoodHeader, INK_XTC_HEADER_SIZE - 1U, &header)) {
        return false;
    }

    memcpy(bad_header, kGoodHeader, sizeof(bad_header));
    bad_header[0] = 'B';
    if (ink_xtc_parse_header_bytes(bad_header, sizeof(bad_header), &header)) {
        return false;
    }

    memcpy(bad_header, kGoodHeader, sizeof(bad_header));
    bad_header[24] = 0xFF;
    bad_header[25] = 0xFF;
    bad_header[26] = 0xFF;
    bad_header[27] = 0xFF;
    if (ink_xtc_parse_header_bytes(bad_header, sizeof(bad_header), &header)) {
        return false;
    }

    memcpy(bad_header, kGoodHeader, sizeof(bad_header));
    bad_header[4] = 2;
    bad_header[5] = 0;
    if (ink_xtc_parse_header_bytes(bad_header, sizeof(bad_header), &header)) {
        return false;
    }

    memcpy(bad_header, kGoodHeader, sizeof(bad_header));
    bad_header[6] = 0;
    bad_header[7] = 0;
    if (ink_xtc_parse_header_bytes(bad_header, sizeof(bad_header), &header)) {
        return false;
    }

    memcpy(bad_header, kGoodHeader, sizeof(bad_header));
    bad_header[32] = 0x90;
    bad_header[33] = 0x01;
    if (ink_xtc_parse_header_bytes(bad_header, sizeof(bad_header), &header)) {
        return false;
    }

    if (ink_xtc_parse_page_index_bytes(
            kGoodPageIndex,
            sizeof(kGoodPageIndex) - 1U,
            &header,
            entries,
            sizeof(entries) / sizeof(entries[0]),
            &entry_count)) {
        return false;
    }

    memcpy(bad_page_index, kGoodPageIndex, sizeof(bad_page_index));
    bad_page_index[8] = 0;
    bad_page_index[9] = 0;
    bad_page_index[10] = 0;
    bad_page_index[11] = 0;
    if (ink_xtc_parse_page_index_bytes(
            bad_page_index,
            sizeof(bad_page_index),
            &header,
            entries,
            sizeof(entries) / sizeof(entries[0]),
            &entry_count)) {
        return false;
    }

    memcpy(bad_page_index, kGoodPageIndex, sizeof(bad_page_index));
    bad_page_index[16] = 0xC7;
    bad_page_index[17] = 0x01;
    if (ink_xtc_parse_page_index_bytes(
            bad_page_index,
            sizeof(bad_page_index),
            &header,
            entries,
            sizeof(entries) / sizeof(entries[0]),
            &entry_count)) {
        return false;
    }

    memcpy(bad_page_index, kGoodPageIndex, sizeof(bad_page_index));
    bad_page_index[12] = 0;
    bad_page_index[13] = 0;
    if (ink_xtc_parse_page_index_bytes(
            bad_page_index,
            sizeof(bad_page_index),
            &header,
            entries,
            sizeof(entries) / sizeof(entries[0]),
            &entry_count)) {
        return false;
    }

    header.page_count = 4U;
    if (ink_xtc_parse_page_index_bytes(
            kGoodPageIndex,
            sizeof(kGoodPageIndex),
            &header,
            entries,
            sizeof(entries) / sizeof(entries[0]),
            &entry_count)) {
        return false;
    }

    if (ink_xtc_parse_chapter_bytes(kChapters, sizeof(kChapters) - 1U, 3U, chapters, 2U, &chapter_count)) {
        return false;
    }

    return true;
}
