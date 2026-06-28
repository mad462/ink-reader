#include "ink_reader_session.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"

#include "epd_gdey0426t82.h"
#include "epd_test_pattern.h"

static const char *TAG = "ink_reader_session";

typedef struct {
    uint32_t load_ms;
    uint32_t convert_ms;
    uint32_t total_ms;
} ink_reader_session_cache_timing_t;

static bool chapter_name_has_prefix(const char *name, const char *prefix);
static bool chapter_name_is_displayable(const char *name);
static const ink_xtc_chapter_entry_t *find_display_chapter_at_or_before(
    const ink_reader_session_t *session,
    size_t page_index,
    size_t *raw_index_out);

static bool chapter_name_has_prefix(const char *name, const char *prefix)
{
    size_t prefix_len = 0U;

    if (name == NULL || prefix == NULL) {
        return false;
    }
    prefix_len = strlen(prefix);
    return prefix_len > 0U && strncmp(name, prefix, prefix_len) == 0;
}

static bool chapter_name_is_displayable(const char *name)
{
    if (name == NULL || name[0] == '\0') {
        return false;
    }
    if (strcmp(name, "译序") == 0 || strcmp(name, "序") == 0 || strcmp(name, "序章") == 0) {
        return false;
    }
    if (strcmp(name, "上篇") == 0 || strcmp(name, "下篇") == 0) {
        return false;
    }
    if (chapter_name_has_prefix(name, "楔子")
        || chapter_name_has_prefix(name, "前言")
        || chapter_name_has_prefix(name, "后记")
        || chapter_name_has_prefix(name, "附录")) {
        return false;
    }
    return true;
}

static const ink_xtc_chapter_entry_t *find_display_chapter_at_or_before(
    const ink_reader_session_t *session,
    size_t page_index,
    size_t *raw_index_out)
{
    const ink_xtc_chapter_entry_t *last_displayable = NULL;
    size_t last_displayable_index = 0U;

    if (raw_index_out != NULL) {
        *raw_index_out = 0U;
    }
    if (session == NULL || !session->xtc_active) {
        return NULL;
    }

    for (size_t i = 0; i < session->xtc_book.chapter_entry_count; ++i) {
        const ink_xtc_chapter_entry_t *chapter = &session->xtc_book.chapter_entries[i];
        if (page_index < chapter->start_page) {
            break;
        }
        if (chapter_name_is_displayable(chapter->name)) {
            last_displayable = chapter;
            last_displayable_index = i;
        }
    }

    if (last_displayable != NULL && raw_index_out != NULL) {
        *raw_index_out = last_displayable_index;
    }
    return last_displayable;
}

static void refresh_chapter_state(ink_reader_session_t *session)
{
    size_t chapter_index = 0U;
    const ink_xtc_chapter_entry_t *chapter;

    if (session == NULL) {
        return;
    }

    session->current_chapter_index = 0U;
    session->total_chapters = session->xtc_book.chapter_entry_count;
    session->current_chapter_name[0] = '\0';

    chapter = ink_xtc_book_chapter_for_page(&session->xtc_book, session->current_page, &chapter_index);
    if (chapter == NULL) {
        return;
    }

    session->current_chapter_index = chapter_index;
    if (chapter->name[0] != '\0') {
        snprintf(session->current_chapter_name, sizeof(session->current_chapter_name), "%s", chapter->name);
    }
}

static void format_xtc_status(ink_reader_session_t *session)
{
    if (session == NULL) {
        return;
    }

    if (session->total_chapters > 0U) {
        snprintf(
            session->text_view.status,
            sizeof(session->text_view.status),
            "XTC %03u/%03u CH %02u/%02u",
            (unsigned)(session->current_page + 1U),
            (unsigned)session->total_pages,
            (unsigned)(session->current_chapter_index + 1U),
            (unsigned)session->total_chapters);
    } else {
        snprintf(
            session->text_view.status,
            sizeof(session->text_view.status),
            "XTC %03u/%03u",
            (unsigned)(session->current_page + 1U),
            (unsigned)session->total_pages);
    }
}

static void clear_cache_slot(ink_reader_session_page_cache_slot_t *slot)
{
    if (slot == NULL) {
        return;
    }

    slot->valid = false;
    slot->page_index = 0U;
}

static void release_cache_slot_buffers(ink_reader_session_page_cache_slot_t *slot)
{
    if (slot == NULL) {
        return;
    }

    if (slot->bitmap_buffer != NULL) {
        free(slot->bitmap_buffer);
        slot->bitmap_buffer = NULL;
    }
    if (slot->native_buffer != NULL) {
        free(slot->native_buffer);
        slot->native_buffer = NULL;
    }
    clear_cache_slot(slot);
}

static void reset_cached_page_aliases(ink_reader_session_t *session)
{
    if (session == NULL) {
        return;
    }

    session->has_prepared_page = false;
    session->has_native_page = false;
    session->prepared_page_length = 0U;
    session->native_page_length = 0U;
    if (session->prepared_page_is_alias) {
        session->prepared_page_buffer = NULL;
        session->prepared_page_capacity = 0U;
        session->prepared_page_is_alias = false;
    }
    if (session->native_page_is_alias) {
        session->native_page_buffer = NULL;
        session->native_page_is_alias = false;
    }
}

static bool ensure_bitmap_copy_buffer(ink_reader_session_t *session)
{
    if (session == NULL) {
        return false;
    }

    if (session->prepared_page_is_alias) {
        session->prepared_page_buffer = NULL;
        session->prepared_page_capacity = 0U;
        session->prepared_page_is_alias = false;
    }
    if (session->prepared_page_buffer != NULL && session->prepared_page_capacity >= EPD_GDEY0426T82_BUFFER_SIZE) {
        return true;
    }

    uint8_t *buffer = realloc(session->prepared_page_buffer, EPD_GDEY0426T82_BUFFER_SIZE);
    if (buffer == NULL) {
        session->prepared_page_buffer = NULL;
        session->prepared_page_capacity = 0U;
        return false;
    }
    session->prepared_page_buffer = buffer;
    session->prepared_page_capacity = EPD_GDEY0426T82_BUFFER_SIZE;
    return true;
}

static int find_cache_slot_by_page(const ink_reader_session_t *session, size_t page_index)
{
    if (session == NULL) {
        return -1;
    }

    for (int i = 0; i < INK_READER_SESSION_PAGE_CACHE_SLOTS; ++i) {
        if (session->page_cache[i].valid && session->page_cache[i].page_index == page_index) {
            return i;
        }
    }
    return -1;
}

static int choose_cache_slot_for_page(ink_reader_session_t *session, size_t page_index)
{
    if (session == NULL) {
        return -1;
    }

    int existing = find_cache_slot_by_page(session, page_index);
    if (existing >= 0) {
        return existing;
    }
    for (int i = 0; i < INK_READER_SESSION_PAGE_CACHE_SLOTS; ++i) {
        if (!session->page_cache[i].valid) {
            return i;
        }
    }
    for (int i = 0; i < INK_READER_SESSION_PAGE_CACHE_SLOTS; ++i) {
        if (i != session->current_cache_slot) {
            return i;
        }
    }
    return session->current_cache_slot >= 0 ? session->current_cache_slot : 0;
}

static bool ensure_cache_slot_buffers(ink_reader_session_page_cache_slot_t *slot)
{
    if (slot == NULL) {
        return false;
    }

    if (slot->bitmap_buffer == NULL) {
        slot->bitmap_buffer = heap_caps_malloc(
            EPD_GDEY0426T82_BUFFER_SIZE,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (slot->bitmap_buffer == NULL) {
            slot->bitmap_buffer = malloc(EPD_GDEY0426T82_BUFFER_SIZE);
        }
    }
    if (slot->native_buffer == NULL) {
        slot->native_buffer = heap_caps_malloc(
            EPD_GDEY0426T82_NATIVE_BUFFER_SIZE,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (slot->native_buffer == NULL) {
            slot->native_buffer = malloc(EPD_GDEY0426T82_NATIVE_BUFFER_SIZE);
        }
    }
    return slot->bitmap_buffer != NULL && slot->native_buffer != NULL;
}

static bool build_cache_slot_for_page(
    ink_reader_session_t *session,
    size_t page_index,
    ink_reader_session_cache_timing_t *timing)
{
    int slot_index;
    ink_reader_session_page_cache_slot_t *slot;
    uint32_t phase_start_ms;

    if (session == NULL || !session->xtc_book.opened || page_index >= session->total_pages) {
        return false;
    }

    slot_index = choose_cache_slot_for_page(session, page_index);
    if (slot_index < 0) {
        return false;
    }
    slot = &session->page_cache[slot_index];
    if (slot->valid && slot->page_index == page_index) {
        if (timing != NULL) {
            memset(timing, 0, sizeof(*timing));
        }
        return true;
    }
    if (!ensure_cache_slot_buffers(slot)) {
        ESP_LOGW(TAG, "cache slot alloc failed: slot=%d", slot_index);
        return false;
    }

    if (timing != NULL) {
        memset(timing, 0, sizeof(*timing));
    }
    phase_start_ms = (uint32_t)pdTICKS_TO_MS(xTaskGetTickCount());
    if (!ink_xtc_book_load_page_bitmap(
            &session->xtc_book,
            page_index,
            slot->bitmap_buffer,
            EPD_GDEY0426T82_BUFFER_SIZE)) {
        ESP_LOGW(TAG, "xtc load page bitmap failed: page=%u", (unsigned)(page_index + 1U));
        clear_cache_slot(slot);
        return false;
    }
    if (timing != NULL) {
        timing->load_ms = (uint32_t)pdTICKS_TO_MS(xTaskGetTickCount()) - phase_start_ms;
    }

    phase_start_ms = (uint32_t)pdTICKS_TO_MS(xTaskGetTickCount());
    epd_gdey0426t82_convert_portrait_to_native(slot->bitmap_buffer, slot->native_buffer);
    if (timing != NULL) {
        timing->convert_ms = (uint32_t)pdTICKS_TO_MS(xTaskGetTickCount()) - phase_start_ms;
        timing->total_ms = timing->load_ms + timing->convert_ms;
    }

    slot->valid = true;
    slot->page_index = page_index;
    ESP_LOGI(
        TAG,
        "cache page=%u slot=%d load=%ums convert=%ums total=%ums",
        (unsigned)(page_index + 1U),
        slot_index,
        timing != NULL ? (unsigned)timing->load_ms : 0U,
        timing != NULL ? (unsigned)timing->convert_ms : 0U,
        timing != NULL ? (unsigned)timing->total_ms : 0U);
    return true;
}

static void bind_current_cache_slot(ink_reader_session_t *session, int slot_index)
{
    ink_reader_session_page_cache_slot_t *slot;

    if (session == NULL || slot_index < 0 || slot_index >= INK_READER_SESSION_PAGE_CACHE_SLOTS) {
        return;
    }
    slot = &session->page_cache[slot_index];
    if (!slot->valid) {
        return;
    }

    reset_cached_page_aliases(session);
    session->current_cache_slot = slot_index;
    session->prepared_page_buffer = slot->bitmap_buffer;
    session->prepared_page_capacity = EPD_GDEY0426T82_BUFFER_SIZE;
    session->prepared_page_length = EPD_GDEY0426T82_BUFFER_SIZE;
    session->prepared_page_is_alias = true;
    session->has_prepared_page = true;
    session->native_page_buffer = slot->native_buffer;
    session->native_page_length = EPD_GDEY0426T82_NATIVE_BUFFER_SIZE;
    session->native_page_is_alias = true;
    session->has_native_page = true;
}

static bool activate_page_cache(
    ink_reader_session_t *session,
    size_t page_index,
    ink_reader_session_cache_timing_t *timing)
{
    int slot_index;

    if (!build_cache_slot_for_page(session, page_index, timing)) {
        return false;
    }
    slot_index = find_cache_slot_by_page(session, page_index);
    if (slot_index < 0) {
        return false;
    }
    bind_current_cache_slot(session, slot_index);
    return true;
}

static bool update_xtc_view_and_page(
    ink_reader_session_t *session,
    const ink_xtc_page_entry_t *entry)
{
    const char *leaf = NULL;
    ink_reader_session_cache_timing_t timing = {0};

    if (session == NULL || entry == NULL) {
        return false;
    }

    leaf = strrchr(session->source_path, '/');
    leaf = (leaf != NULL && leaf[1] != '\0') ? leaf + 1 : session->source_path;

    refresh_chapter_state(session);

    memset(&session->text_view, 0, sizeof(session->text_view));
    snprintf(session->text_view.title, sizeof(session->text_view.title), "XTC READER");
    snprintf(
        session->text_view.lines[0],
        sizeof(session->text_view.lines[0]),
        "FILE %.58s",
        leaf != NULL && leaf[0] != '\0' ? leaf : "N/A");
    snprintf(
        session->text_view.lines[1],
        sizeof(session->text_view.lines[1]),
        "PAGE %03u OF %03u",
        (unsigned)(session->current_page + 1U),
        (unsigned)session->total_pages);
    if (session->current_chapter_name[0] != '\0') {
        snprintf(
            session->text_view.lines[2],
            sizeof(session->text_view.lines[2]),
            "CH %02u/%02u %.38s",
            (unsigned)(session->current_chapter_index + 1U),
            (unsigned)session->total_chapters,
            session->current_chapter_name);
    } else if (session->total_chapters > 0U) {
        snprintf(
            session->text_view.lines[2],
            sizeof(session->text_view.lines[2]),
            "CH %02u/%02u",
            (unsigned)(session->current_chapter_index + 1U),
            (unsigned)session->total_chapters);
    } else {
        snprintf(
            session->text_view.lines[2],
            sizeof(session->text_view.lines[2]),
            "FRAME %ux%u",
            (unsigned)entry->width,
            (unsigned)entry->height);
    }
    snprintf(
        session->text_view.lines[3],
        sizeof(session->text_view.lines[3]),
        "DATA %u SIZE %u",
        (unsigned)entry->data_offset,
        (unsigned)entry->encoded_size);
    format_xtc_status(session);

    if (!activate_page_cache(session, session->current_page, &timing)) {
        ESP_LOGW(
            TAG,
            "xtc activate page cache failed: path=%s page=%u/%u offset=%u size=%u frame=%ux%u",
            session->source_path,
            (unsigned)(session->current_page + 1U),
            (unsigned)session->total_pages,
            (unsigned)entry->data_offset,
            (unsigned)entry->encoded_size,
            (unsigned)entry->width,
            (unsigned)entry->height);
        return false;
    }

    session->active = true;
    session->xtc_active = true;
    session->has_text_view = true;
    return true;
}

static bool persist_xtc_progress(ink_reader_session_t *session, ink_app_state_t *state)
{
    const char *leaf;

    if (session == NULL || state == NULL || !session->xtc_active) {
        return false;
    }

    ink_app_state_remember_xtc_open_book(
        state,
        session->source_path,
        session->current_page,
        session->current_chapter_index,
        session->total_pages);
    leaf = strrchr(session->source_path, '/');
    leaf = (leaf != NULL && leaf[1] != '\0') ? leaf + 1 : session->source_path;
    (void)ink_app_state_note_xtc_opened(
        state,
        session->source_path,
        leaf,
        session->current_page,
        session->current_chapter_index,
        session->total_pages,
        session->current_chapter_name);
    return true;
}

void ink_reader_session_init(ink_reader_session_t *session)
{
    if (session == NULL) {
        return;
    }

    memset(session, 0, sizeof(*session));
    session->current_cache_slot = -1;
}

void ink_reader_session_close(ink_reader_session_t *session)
{
    if (session == NULL) {
        return;
    }

    session->active = false;
    session->has_text_view = false;
    session->xtc_active = false;
    memset(&session->text_view, 0, sizeof(session->text_view));
    if (!session->prepared_page_is_alias && session->prepared_page_buffer != NULL) {
        free(session->prepared_page_buffer);
    }
    session->prepared_page_buffer = NULL;
    session->prepared_page_capacity = 0U;
    session->prepared_page_length = 0;
    session->prepared_page_is_alias = false;
    session->has_prepared_page = false;
    session->native_page_buffer = NULL;
    session->native_page_length = 0U;
    session->native_page_is_alias = false;
    session->has_native_page = false;
    for (int i = 0; i < INK_READER_SESSION_PAGE_CACHE_SLOTS; ++i) {
        release_cache_slot_buffers(&session->page_cache[i]);
    }
    session->current_cache_slot = -1;
    memset(session->source_path, 0, sizeof(session->source_path));
    session->current_page = 0U;
    session->total_pages = 0U;
    session->current_chapter_index = 0U;
    session->total_chapters = 0U;
    session->current_chapter_name[0] = '\0';
    ink_xtc_book_close(&session->xtc_book);
}

bool ink_reader_session_set_text_view(
    ink_reader_session_t *session,
    const ink_reader_session_view_t *view)
{
    if (session == NULL || view == NULL) {
        return false;
    }

    session->active = true;
    session->has_text_view = true;
    session->text_view = *view;
    return true;
}

bool ink_reader_session_get_text_view(
    const ink_reader_session_t *session,
    ink_reader_session_view_t *view)
{
    if (session == NULL || view == NULL || !session->active || !session->has_text_view) {
        return false;
    }

    *view = session->text_view;
    return true;
}

bool ink_reader_session_set_prepared_page(
    ink_reader_session_t *session,
    const uint8_t *page_buffer,
    size_t page_buffer_length)
{
    if (session == NULL) {
        return false;
    }

    if (page_buffer == NULL || page_buffer_length < EPD_GDEY0426T82_BUFFER_SIZE) {
        reset_cached_page_aliases(session);
        return false;
    }
    if (!ensure_bitmap_copy_buffer(session)) {
        session->has_prepared_page = false;
        session->prepared_page_length = 0U;
        return false;
    }

    session->active = true;
    session->has_prepared_page = true;
    memcpy(session->prepared_page_buffer, page_buffer, EPD_GDEY0426T82_BUFFER_SIZE);
    session->prepared_page_length = EPD_GDEY0426T82_BUFFER_SIZE;
    return true;
}

bool ink_reader_session_is_xtc_active(const ink_reader_session_t *session)
{
    return session != NULL && session->active && session->xtc_active;
}

bool ink_reader_session_open_xtc(
    ink_reader_session_t *session,
    const char *path,
    ink_app_state_t *state)
{
    if (session == NULL || path == NULL || path[0] == '\0') {
        return false;
    }

    ESP_LOGI(
        TAG,
        "xtc open begin path=%s heap_internal=%u heap_8bit=%u",
        path,
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT));
    ink_reader_session_close(session);
    ink_reader_session_init(session);
    if (!ink_xtc_book_open(&session->xtc_book, path)) {
        ESP_LOGW(TAG, "xtc book open failed: path=%s", path);
        return false;
    }

    snprintf(session->source_path, sizeof(session->source_path), "%s", path);
    session->current_page = 0U;
    session->total_pages = session->xtc_book.page_entry_count;
    refresh_chapter_state(session);
    if (!update_xtc_view_and_page(session, ink_xtc_book_current_page_entry(&session->xtc_book))) {
        ESP_LOGW(TAG, "xtc first page prepare failed: path=%s pages=%u", path, (unsigned)session->total_pages);
        ink_reader_session_close(session);
        return false;
    }

    ESP_LOGI(
        TAG,
        "xtc open ok path=%s pages=%u chapters=%u current_chapter=%u name=%s",
        path,
        (unsigned)session->total_pages,
        (unsigned)session->total_chapters,
        (unsigned)(session->current_chapter_index + 1U),
        session->current_chapter_name[0] != '\0' ? session->current_chapter_name : "<none>");
    for (size_t i = 0; i < session->xtc_book.chapter_entry_count && i < 8U; ++i) {
        const ink_xtc_chapter_entry_t *entry = &session->xtc_book.chapter_entries[i];
        ESP_LOGI(
            TAG,
            "chapter[%u] start=%u end=%u name=%s",
            (unsigned)(i + 1U),
            (unsigned)(entry->start_page + 1U),
            (unsigned)(entry->end_page + 1U),
            entry->name[0] != '\0' ? entry->name : "<none>");
    }
    (void)persist_xtc_progress(session, state);
    return true;
}

bool ink_reader_session_jump_to_page(
    ink_reader_session_t *session,
    size_t page_index,
    ink_app_state_t *state)
{
    const ink_xtc_page_entry_t *entry;

    if (!ink_reader_session_is_xtc_active(session)) {
        return false;
    }
    if (!ink_xtc_book_set_current_page(&session->xtc_book, page_index)) {
        return false;
    }

    session->current_page = session->xtc_book.current_page;
    session->total_pages = session->xtc_book.page_entry_count;
    refresh_chapter_state(session);
    entry = ink_xtc_book_current_page_entry(&session->xtc_book);
    if (!update_xtc_view_and_page(session, entry)) {
        return false;
    }
    (void)persist_xtc_progress(session, state);
    return true;
}

bool ink_reader_session_next_page(
    ink_reader_session_t *session,
    ink_app_state_t *state)
{
    if (!ink_reader_session_is_xtc_active(session)) {
        return false;
    }
    if (!ink_xtc_book_next_page(&session->xtc_book)) {
        return false;
    }
    return ink_reader_session_jump_to_page(session, session->xtc_book.current_page, state);
}

bool ink_reader_session_previous_page(
    ink_reader_session_t *session,
    ink_app_state_t *state)
{
    if (!ink_reader_session_is_xtc_active(session)) {
        return false;
    }
    if (!ink_xtc_book_previous_page(&session->xtc_book)) {
        return false;
    }
    return ink_reader_session_jump_to_page(session, session->xtc_book.current_page, state);
}

bool ink_reader_session_skip_pages(
    ink_reader_session_t *session,
    int32_t delta_pages,
    ink_app_state_t *state)
{
    size_t target_page;
    int64_t raw_target;

    if (!ink_reader_session_is_xtc_active(session) || delta_pages == 0) {
        return false;
    }

    raw_target = (int64_t)session->xtc_book.current_page + (int64_t)delta_pages;
    if (raw_target < 0) {
        raw_target = 0;
    }
    if (session->xtc_book.page_entry_count == 0U) {
        return false;
    }
    if ((uint64_t)raw_target >= (uint64_t)session->xtc_book.page_entry_count) {
        raw_target = (int64_t)session->xtc_book.page_entry_count - 1;
    }

    target_page = (size_t)raw_target;
    if (target_page == session->xtc_book.current_page) {
        return false;
    }

    return ink_reader_session_jump_to_page(session, target_page, state);
}

void ink_reader_session_prefetch_next(
    ink_reader_session_t *session,
    ink_reader_session_should_abort_fn should_abort,
    void *ctx)
{
    size_t next_page;
    ink_reader_session_cache_timing_t timing = {0};

    if (!ink_reader_session_is_xtc_active(session) || session->total_pages == 0U) {
        return;
    }
    if (should_abort != NULL && should_abort(ctx)) {
        ESP_LOGI(TAG, "prefetch skipped: newer request pending");
        return;
    }
    if (session->current_page + 1U >= session->total_pages) {
        return;
    }

    next_page = session->current_page + 1U;
    (void)build_cache_slot_for_page(session, next_page, &timing);
    ESP_LOGI(
        TAG,
        "prefetch next page=%u load=%ums convert=%ums total=%ums",
        (unsigned)(next_page + 1U),
        (unsigned)timing.load_ms,
        (unsigned)timing.convert_ms,
        (unsigned)timing.total_ms);
}

bool ink_reader_session_has_prepared_page(const ink_reader_session_t *session)
{
    return session != NULL
        && session->active
        && session->has_prepared_page
        && session->prepared_page_buffer != NULL
        && session->prepared_page_length >= EPD_GDEY0426T82_BUFFER_SIZE;
}

const uint8_t *ink_reader_session_prepared_page_buffer(const ink_reader_session_t *session)
{
    return ink_reader_session_has_prepared_page(session) ? session->prepared_page_buffer : NULL;
}

size_t ink_reader_session_prepared_page_length(const ink_reader_session_t *session)
{
    return ink_reader_session_has_prepared_page(session) ? session->prepared_page_length : 0U;
}

bool ink_reader_session_has_native_page(const ink_reader_session_t *session)
{
    return session != NULL
        && session->active
        && session->has_native_page
        && session->native_page_buffer != NULL
        && session->native_page_length >= EPD_GDEY0426T82_NATIVE_BUFFER_SIZE;
}

const uint8_t *ink_reader_session_native_page_buffer(const ink_reader_session_t *session)
{
    return ink_reader_session_has_native_page(session) ? session->native_page_buffer : NULL;
}

size_t ink_reader_session_native_page_length(const ink_reader_session_t *session)
{
    return ink_reader_session_has_native_page(session) ? session->native_page_length : 0U;
}

size_t ink_reader_session_resolve_chapter_for_page(
    const ink_reader_session_t *session,
    size_t page_index,
    const char **chapter_name_out,
    size_t *chapter_total_out)
{
    size_t chapter_index = 0U;
    const ink_xtc_chapter_entry_t *chapter;

    if (chapter_name_out != NULL) {
        *chapter_name_out = NULL;
    }
    if (chapter_total_out != NULL) {
        *chapter_total_out = 0U;
    }
    if (session == NULL || !session->xtc_active) {
        return 0U;
    }

    chapter = ink_xtc_book_chapter_for_page(&session->xtc_book, page_index, &chapter_index);
    if (chapter_total_out != NULL) {
        *chapter_total_out = session->xtc_book.chapter_entry_count;
    }
    if (chapter_name_out != NULL && chapter != NULL && chapter->name[0] != '\0') {
        *chapter_name_out = chapter->name;
    }
    return chapter_index;
}

bool ink_reader_session_resolve_display_chapter_for_page(
    const ink_reader_session_t *session,
    size_t page_index,
    size_t *display_chapter_index_out,
    size_t *display_chapter_total_out,
    const char **display_chapter_title_out)
{
    const ink_xtc_chapter_entry_t *chapter = NULL;
    size_t raw_index = 0U;
    size_t display_index = 0U;
    size_t display_total = 0U;

    if (display_chapter_index_out != NULL) {
        *display_chapter_index_out = 0U;
    }
    if (display_chapter_total_out != NULL) {
        *display_chapter_total_out = 0U;
    }
    if (display_chapter_title_out != NULL) {
        *display_chapter_title_out = NULL;
    }
    if (session == NULL || !session->xtc_active) {
        return false;
    }

    chapter = find_display_chapter_at_or_before(session, page_index, &raw_index);
    for (size_t i = 0; i < session->xtc_book.chapter_entry_count; ++i) {
        const ink_xtc_chapter_entry_t *entry = &session->xtc_book.chapter_entries[i];
        if (!chapter_name_is_displayable(entry->name)) {
            continue;
        }
        if (i <= raw_index) {
            display_index = display_total;
        }
        ++display_total;
    }

    if (display_chapter_total_out != NULL) {
        *display_chapter_total_out = display_total;
    }
    if (chapter == NULL) {
        return false;
    }
    if (display_chapter_index_out != NULL) {
        *display_chapter_index_out = display_index;
    }
    if (display_chapter_title_out != NULL) {
        *display_chapter_title_out = chapter->name;
    }
    return true;
}

bool ink_reader_session_self_test(void)
{
    ink_reader_session_t session;
    ink_app_state_t state;
    uint8_t *page = malloc(EPD_GDEY0426T82_BUFFER_SIZE);
    ink_reader_session_view_t view = {
        .title = "SESSION",
        .lines = {"L1", "L2", "L3", "L4"},
        .status = "READY",
    };
    ink_reader_session_view_t restored;
    const char *chapter_name = NULL;
    size_t chapter_total = 0U;
    size_t display_chapter_index = 0U;
    size_t display_chapter_total = 0U;
    const char *display_chapter_title = NULL;

    if (page == NULL) {
        return false;
    }

    ink_reader_session_init(&session);
    ink_app_state_prepare_default(&state);
    if (session.active || ink_reader_session_has_prepared_page(&session)) {
        free(page);
        return false;
    }

    memset(&restored, 0, sizeof(restored));
    if (ink_reader_session_get_text_view(&session, &restored)) {
        free(page);
        return false;
    }
    if (!ink_reader_session_set_text_view(&session, &view)) {
        free(page);
        return false;
    }
    if (!ink_reader_session_get_text_view(&session, &restored)) {
        free(page);
        return false;
    }
    if (strcmp(restored.title, "SESSION") != 0 || strcmp(restored.status, "READY") != 0) {
        free(page);
        return false;
    }

    session.active = true;
    if (ink_reader_session_set_prepared_page(&session, NULL, EPD_GDEY0426T82_BUFFER_SIZE)) {
        free(page);
        return false;
    }
    if (ink_reader_session_has_prepared_page(&session)) {
        free(page);
        return false;
    }

    if (!ink_reader_session_set_prepared_page(&session, page, EPD_GDEY0426T82_BUFFER_SIZE)) {
        free(page);
        return false;
    }
    if (!ink_reader_session_has_prepared_page(&session)) {
        free(page);
        return false;
    }
    if (ink_reader_session_prepared_page_buffer(&session) == NULL
        || ink_reader_session_prepared_page_buffer(&session) == page
        || memcmp(ink_reader_session_prepared_page_buffer(&session), page, EPD_GDEY0426T82_BUFFER_SIZE) != 0) {
        free(page);
        return false;
    }
    if (ink_reader_session_prepared_page_length(&session) != EPD_GDEY0426T82_BUFFER_SIZE) {
        free(page);
        return false;
    }

    session.xtc_active = true;
    snprintf(session.source_path, sizeof(session.source_path), "%s", "/sdcard/books/demo.xtc");
    session.current_page = 2U;
    session.total_pages = 3U;
    session.has_native_page = true;
    session.native_page_buffer = (const uint8_t *)page;
    session.native_page_length = EPD_GDEY0426T82_NATIVE_BUFFER_SIZE;
    session.xtc_book.opened = true;
    session.xtc_book.page_entry_count = 3U;
    session.xtc_book.current_page = 2U;
    session.xtc_book.chapter_entries = calloc(2U, sizeof(*session.xtc_book.chapter_entries));
    if (session.xtc_book.chapter_entries == NULL) {
        free(page);
        return false;
    }
    snprintf(session.xtc_book.chapter_entries[0].name, sizeof(session.xtc_book.chapter_entries[0].name), "%s", "Chapter A");
    session.xtc_book.chapter_entries[0].start_page = 0U;
    session.xtc_book.chapter_entries[0].end_page = 1U;
    snprintf(session.xtc_book.chapter_entries[1].name, sizeof(session.xtc_book.chapter_entries[1].name), "%s", "Chapter B");
    session.xtc_book.chapter_entries[1].start_page = 2U;
    session.xtc_book.chapter_entries[1].end_page = 2U;
    session.xtc_book.chapter_entry_count = 2U;
    refresh_chapter_state(&session);
    ink_app_state_remember_xtc_open_book(&state, session.source_path, session.current_page, session.current_chapter_index, session.xtc_book.chapter_entry_count);
    if (!ink_reader_session_is_xtc_active(&session)
        || !state.has_open_book
        || state.open_book_kind != INK_APP_STATE_BOOK_KIND_XTC
        || state.open_book_page != 2U
        || session.total_pages != 3U
        || session.current_chapter_index != 1U
        || session.total_chapters != 2U) {
        free(page);
        return false;
    }
    if (strcmp(session.source_path, "/sdcard/books/demo.xtc") != 0
        || !ink_reader_session_has_prepared_page(&session)
        || !ink_reader_session_has_native_page(&session)) {
        free(page);
        return false;
    }
    if (ink_reader_session_resolve_chapter_for_page(&session, 2U, &chapter_name, &chapter_total) != 1U
        || chapter_total != 2U
        || chapter_name == NULL
        || strcmp(chapter_name, "Chapter B") != 0) {
        free(page);
        return false;
    }

    free(session.xtc_book.chapter_entries);
    session.xtc_book.chapter_entries = calloc(4U, sizeof(*session.xtc_book.chapter_entries));
    if (session.xtc_book.chapter_entries == NULL) {
        free(page);
        return false;
    }
    session.xtc_book.chapter_entry_count = 4U;
    snprintf(session.xtc_book.chapter_entries[0].name, sizeof(session.xtc_book.chapter_entries[0].name), "%s", "译序");
    session.xtc_book.chapter_entries[0].start_page = 0U;
    session.xtc_book.chapter_entries[0].end_page = 4U;
    snprintf(session.xtc_book.chapter_entries[1].name, sizeof(session.xtc_book.chapter_entries[1].name), "%s", "上篇");
    session.xtc_book.chapter_entries[1].start_page = 5U;
    session.xtc_book.chapter_entries[1].end_page = 5U;
    snprintf(session.xtc_book.chapter_entries[2].name, sizeof(session.xtc_book.chapter_entries[2].name), "%s", "一 飞驰的礁石");
    session.xtc_book.chapter_entries[2].start_page = 6U;
    session.xtc_book.chapter_entries[2].end_page = 19U;
    snprintf(session.xtc_book.chapter_entries[3].name, sizeof(session.xtc_book.chapter_entries[3].name), "%s", "二 赞成与反对");
    session.xtc_book.chapter_entries[3].start_page = 20U;
    session.xtc_book.chapter_entries[3].end_page = 31U;
    if (!ink_reader_session_resolve_display_chapter_for_page(
            &session,
            20U,
            &display_chapter_index,
            &display_chapter_total,
            &display_chapter_title)
        || display_chapter_index != 1U
        || display_chapter_total != 2U
        || display_chapter_title == NULL
        || strcmp(display_chapter_title, "二 赞成与反对") != 0) {
        free(page);
        return false;
    }

    ink_reader_session_close(&session);
    if (session.active || ink_reader_session_has_prepared_page(&session) || ink_reader_session_has_native_page(&session)) {
        free(page);
        return false;
    }

    free(page);
    return true;
}
