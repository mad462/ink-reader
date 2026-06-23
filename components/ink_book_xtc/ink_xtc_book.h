#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "esp_err.h"
#include "ink_xtc_reader.h"

typedef struct {
    bool opened;
    FILE *file;
    char path[256];
    ink_xtc_file_header_t header;
    ink_xtc_metadata_t metadata;
    bool has_metadata;
    ink_xtc_chapter_entry_t *chapter_entries;
    size_t chapter_entry_count;
    /* Owned by the book after a successful attach and released by close/reattach. */
    ink_xtc_page_entry_t *page_entries;
    size_t page_entry_count;
    size_t current_page;
} ink_xtc_book_t;

void ink_xtc_book_prepare_default(ink_xtc_book_t *book);
void ink_xtc_book_close(ink_xtc_book_t *book);
bool ink_xtc_book_open(ink_xtc_book_t *book, const char *path);
/*
 * Ownership of page_entries transfers to book on success. Caller retains ownership on failure.
 * Reattaching the exact same page_entries pointer already owned by book is allowed.
 */
bool ink_xtc_book_attach_page_entries(
    ink_xtc_book_t *book,
    const char *path,
    const ink_xtc_file_header_t *header,
    ink_xtc_page_entry_t *page_entries,
    size_t page_entry_count);
bool ink_xtc_book_set_current_page(ink_xtc_book_t *book, size_t page_index);
bool ink_xtc_book_next_page(ink_xtc_book_t *book);
bool ink_xtc_book_previous_page(ink_xtc_book_t *book);
bool ink_xtc_book_load_page_bitmap(
    const ink_xtc_book_t *book,
    size_t page_index,
    uint8_t *bitmap_buffer,
    size_t bitmap_length);
bool ink_xtc_book_load_current_page_bitmap(
    const ink_xtc_book_t *book,
    uint8_t *bitmap_buffer,
    size_t bitmap_length);
const ink_xtc_page_entry_t *ink_xtc_book_current_page_entry(const ink_xtc_book_t *book);
const ink_xtc_chapter_entry_t *ink_xtc_book_chapter_for_page(const ink_xtc_book_t *book, size_t page_index, size_t *chapter_index_out);
const ink_xtc_chapter_entry_t *ink_xtc_book_current_chapter(const ink_xtc_book_t *book, size_t *chapter_index_out);
bool ink_xtc_book_self_test(void);
