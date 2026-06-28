#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ink_app_state.h"
#include "ink_xtc_book.h"

#define INK_READER_SESSION_TITLE_LENGTH 63
#define INK_READER_SESSION_BODY_LINES 4
#define INK_READER_SESSION_LINE_LENGTH 63
#define INK_READER_SESSION_PAGE_CACHE_SLOTS 3

typedef struct {
    char title[INK_READER_SESSION_TITLE_LENGTH + 1];
    char lines[INK_READER_SESSION_BODY_LINES][INK_READER_SESSION_LINE_LENGTH + 1];
    char status[INK_READER_SESSION_LINE_LENGTH + 1];
} ink_reader_session_view_t;

typedef struct {
    bool valid;
    size_t page_index;
    uint8_t *bitmap_buffer;
    uint8_t *native_buffer;
} ink_reader_session_page_cache_slot_t;

typedef bool (*ink_reader_session_should_abort_fn)(void *ctx);

typedef struct {
    bool active;
    bool has_text_view;
    bool has_prepared_page;
    bool has_native_page;
    bool xtc_active;
    bool prepared_page_is_alias;
    bool native_page_is_alias;
    ink_reader_session_view_t text_view;
    uint8_t *prepared_page_buffer;
    size_t prepared_page_capacity;
    size_t prepared_page_length;
    const uint8_t *native_page_buffer;
    size_t native_page_length;
    char source_path[INK_APP_STATE_PATH_LENGTH + 1];
    size_t current_page;
    size_t total_pages;
    size_t current_chapter_index;
    size_t total_chapters;
    char current_chapter_name[81];
    int current_cache_slot;
    ink_reader_session_page_cache_slot_t page_cache[INK_READER_SESSION_PAGE_CACHE_SLOTS];
    ink_xtc_book_t xtc_book;
} ink_reader_session_t;

void ink_reader_session_init(ink_reader_session_t *session);
void ink_reader_session_close(ink_reader_session_t *session);
bool ink_reader_session_set_text_view(
    ink_reader_session_t *session,
    const ink_reader_session_view_t *view
);
bool ink_reader_session_get_text_view(
    const ink_reader_session_t *session,
    ink_reader_session_view_t *view
);
bool ink_reader_session_set_prepared_page(
    ink_reader_session_t *session,
    const uint8_t *page_buffer,
    size_t page_buffer_length
);
bool ink_reader_session_is_xtc_active(const ink_reader_session_t *session);
bool ink_reader_session_open_xtc(
    ink_reader_session_t *session,
    const char *path,
    ink_app_state_t *state
);
bool ink_reader_session_jump_to_page(
    ink_reader_session_t *session,
    size_t page_index,
    ink_app_state_t *state
);
bool ink_reader_session_next_page(
    ink_reader_session_t *session,
    ink_app_state_t *state
);
bool ink_reader_session_previous_page(
    ink_reader_session_t *session,
    ink_app_state_t *state
);
bool ink_reader_session_skip_pages(
    ink_reader_session_t *session,
    int32_t delta_pages,
    ink_app_state_t *state
);
void ink_reader_session_prefetch_next(
    ink_reader_session_t *session,
    ink_reader_session_should_abort_fn should_abort,
    void *ctx
);
bool ink_reader_session_has_prepared_page(const ink_reader_session_t *session);
const uint8_t *ink_reader_session_prepared_page_buffer(const ink_reader_session_t *session);
size_t ink_reader_session_prepared_page_length(const ink_reader_session_t *session);
bool ink_reader_session_has_native_page(const ink_reader_session_t *session);
const uint8_t *ink_reader_session_native_page_buffer(const ink_reader_session_t *session);
size_t ink_reader_session_native_page_length(const ink_reader_session_t *session);
size_t ink_reader_session_resolve_chapter_for_page(
    const ink_reader_session_t *session,
    size_t page_index,
    const char **chapter_name_out,
    size_t *chapter_total_out);
bool ink_reader_session_resolve_display_chapter_for_page(
    const ink_reader_session_t *session,
    size_t page_index,
    size_t *display_chapter_index_out,
    size_t *display_chapter_total_out,
    const char **display_chapter_title_out);
bool ink_reader_session_self_test(void);
