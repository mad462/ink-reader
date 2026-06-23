#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint32_t page_count;
    uint8_t read_direction;
    bool has_metadata;
    bool has_thumbnails;
    bool has_chapters;
    bool is_hq_container;
    uint32_t current_page;
    uint64_t metadata_offset;
    uint64_t page_index_offset;
    uint64_t data_offset;
    uint64_t thumbnail_offset;
    uint64_t chapter_offset;
} ink_xtc_file_header_t;

typedef struct {
    uint64_t data_offset;
    uint32_t encoded_size;
    uint16_t width;
    uint16_t height;
} ink_xtc_page_entry_t;

typedef struct {
    char title[129];
    char author[65];
    char publisher[33];
    char language[17];
    uint32_t create_time;
    uint16_t cover_page;
    uint16_t chapter_count;
} ink_xtc_metadata_t;

typedef struct {
    char name[81];
    uint16_t start_page;
    uint16_t end_page;
} ink_xtc_chapter_entry_t;

bool ink_xtc_parse_header_bytes(
    const uint8_t *raw,
    size_t length,
    ink_xtc_file_header_t *out);
bool ink_xtc_parse_metadata_bytes(
    const uint8_t *raw,
    size_t length,
    ink_xtc_metadata_t *out);
bool ink_xtc_parse_page_index_bytes(
    const uint8_t *raw,
    size_t length,
    const ink_xtc_file_header_t *header,
    ink_xtc_page_entry_t *entries,
    size_t entry_capacity,
    size_t *entry_count_out);
bool ink_xtc_parse_chapter_bytes(
    const uint8_t *raw,
    size_t length,
    uint32_t page_count,
    ink_xtc_chapter_entry_t *entries,
    size_t entry_capacity,
    size_t *entry_count_out);
bool ink_xtc_reader_self_test(void);
