#include "ink_display_mailbox.h"

#include <stdlib.h>
#include <string.h>

static portMUX_TYPE s_mailbox_lock = portMUX_INITIALIZER_UNLOCKED;
static bool mailbox_has_pending_request_locked(const ink_display_mailbox_t *mailbox);

void ink_display_mailbox_init(
    ink_display_mailbox_t *mailbox,
    TaskHandle_t notify_task,
    uint8_t *bitmap_snapshot_a,
    uint8_t *bitmap_snapshot_b,
    uint8_t *native_snapshot_a,
    uint8_t *native_snapshot_b)
{
    if (mailbox == NULL) {
        return;
    }

    memset(mailbox, 0, sizeof(*mailbox));
    mailbox->notify_task = notify_task;
    mailbox->bitmap_snapshot_buffers[0] = bitmap_snapshot_a;
    mailbox->bitmap_snapshot_buffers[1] = bitmap_snapshot_b;
    mailbox->native_snapshot_buffers[0] = native_snapshot_a;
    mailbox->native_snapshot_buffers[1] = native_snapshot_b;
}

void ink_display_mailbox_set_notify_task(ink_display_mailbox_t *mailbox, TaskHandle_t notify_task)
{
    bool should_notify = false;

    if (mailbox == NULL) {
        return;
    }

    taskENTER_CRITICAL(&s_mailbox_lock);
    mailbox->notify_task = notify_task;
    should_notify = notify_task != NULL && mailbox_has_pending_request_locked(mailbox);
    taskEXIT_CRITICAL(&s_mailbox_lock);

    if (should_notify) {
        xTaskNotifyGive(notify_task);
    }
}

uint32_t ink_display_mailbox_submit(
    ink_display_mailbox_t *mailbox,
    const ink_display_request_t *request)
{
    uint32_t seq = 0;
    TaskHandle_t notify_task = NULL;

    if (mailbox == NULL || request == NULL) {
        return 0;
    }

    taskENTER_CRITICAL(&s_mailbox_lock);
    if (mailbox->latest_seq != mailbox->completed_seq) {
        ++mailbox->stats.merged_count;
    }
    seq = mailbox->latest_seq + 1U;
    mailbox->latest_seq = seq;
    mailbox->latest_request = *request;
    if (request->use_bitmap_page
        && request->bitmap_page_buffer != NULL
        && request->bitmap_page_length >= EPD_GDEY0426T82_BUFFER_SIZE) {
        const uint8_t next_slot = (uint8_t)((mailbox->latest_bitmap_slot + 1U) & 0x01U);
        uint8_t *snapshot = mailbox->bitmap_snapshot_buffers[next_slot];
        if (snapshot != NULL) {
            memcpy(snapshot, request->bitmap_page_buffer, EPD_GDEY0426T82_BUFFER_SIZE);
            mailbox->latest_request.bitmap_page_buffer = snapshot;
            mailbox->latest_request.bitmap_page_length = EPD_GDEY0426T82_BUFFER_SIZE;
            mailbox->latest_bitmap_slot = next_slot;
        } else {
            mailbox->latest_request.use_bitmap_page = false;
            mailbox->latest_request.bitmap_page_buffer = NULL;
            mailbox->latest_request.bitmap_page_length = 0U;
        }
    } else {
        mailbox->latest_request.bitmap_page_buffer = NULL;
        mailbox->latest_request.bitmap_page_length = 0U;
    }
    if (request->use_native_page
        && request->native_page_buffer != NULL
        && request->native_page_length >= EPD_GDEY0426T82_NATIVE_BUFFER_SIZE) {
        const uint8_t next_slot = (uint8_t)((mailbox->latest_native_slot + 1U) & 0x01U);
        uint8_t *snapshot = mailbox->native_snapshot_buffers[next_slot];
        if (snapshot != NULL) {
            memcpy(snapshot, request->native_page_buffer, EPD_GDEY0426T82_NATIVE_BUFFER_SIZE);
            mailbox->latest_request.native_page_buffer = snapshot;
            mailbox->latest_request.native_page_length = EPD_GDEY0426T82_NATIVE_BUFFER_SIZE;
            mailbox->latest_native_slot = next_slot;
        } else {
            mailbox->latest_request.use_native_page = false;
            mailbox->latest_request.native_page_buffer = NULL;
            mailbox->latest_request.native_page_length = 0U;
        }
    } else {
        mailbox->latest_request.native_page_buffer = NULL;
        mailbox->latest_request.native_page_length = 0U;
    }
    mailbox->latest_request.seq = seq;
    ++mailbox->stats.submitted_count;
    notify_task = mailbox->notify_task;
    taskEXIT_CRITICAL(&s_mailbox_lock);

    if (notify_task != NULL) {
        xTaskNotifyGive(notify_task);
    }

    return seq;
}

bool ink_display_mailbox_try_claim_latest(
    ink_display_mailbox_t *mailbox,
    ink_display_request_t *request)
{
    bool claimed = false;

    if (mailbox == NULL || request == NULL) {
        return false;
    }

    taskENTER_CRITICAL(&s_mailbox_lock);
    if (mailbox->latest_seq != 0U
        && mailbox->latest_seq != mailbox->completed_seq
        && mailbox->latest_seq != mailbox->active_seq) {
        mailbox->active_seq = mailbox->latest_seq;
        mailbox->active_bitmap_slot = mailbox->latest_bitmap_slot;
        mailbox->active_native_slot = mailbox->latest_native_slot;
        *request = mailbox->latest_request;
        claimed = true;
    }
    taskEXIT_CRITICAL(&s_mailbox_lock);

    return claimed;
}

bool ink_display_mailbox_has_newer_than(const ink_display_mailbox_t *mailbox, uint32_t seq)
{
    bool newer = false;

    if (mailbox == NULL) {
        return false;
    }

    taskENTER_CRITICAL(&s_mailbox_lock);
    newer = mailbox->latest_seq > seq;
    taskEXIT_CRITICAL(&s_mailbox_lock);
    return newer;
}

bool ink_display_mailbox_is_idle(const ink_display_mailbox_t *mailbox)
{
    bool idle = false;

    if (mailbox == NULL) {
        return true;
    }

    taskENTER_CRITICAL(&s_mailbox_lock);
    idle = mailbox->active_seq == 0U && mailbox->latest_seq == mailbox->completed_seq;
    taskEXIT_CRITICAL(&s_mailbox_lock);
    return idle;
}

void ink_display_mailbox_invalidate_pending(ink_display_mailbox_t *mailbox)
{
    if (mailbox == NULL) {
        return;
    }

    taskENTER_CRITICAL(&s_mailbox_lock);
    mailbox->latest_seq = mailbox->completed_seq;
    mailbox->active_seq = 0U;
    taskEXIT_CRITICAL(&s_mailbox_lock);
}

void ink_display_mailbox_discard_queued_only(ink_display_mailbox_t *mailbox)
{
    if (mailbox == NULL) {
        return;
    }

    taskENTER_CRITICAL(&s_mailbox_lock);
    if (mailbox->latest_seq != mailbox->active_seq) {
        mailbox->latest_seq = mailbox->completed_seq;
    }
    taskEXIT_CRITICAL(&s_mailbox_lock);
}

void ink_display_mailbox_note_cancelled(ink_display_mailbox_t *mailbox)
{
    if (mailbox == NULL) {
        return;
    }

    taskENTER_CRITICAL(&s_mailbox_lock);
    ++mailbox->stats.cancelled_count;
    mailbox->active_seq = 0;
    taskEXIT_CRITICAL(&s_mailbox_lock);
}

void ink_display_mailbox_note_discarded_stale(ink_display_mailbox_t *mailbox)
{
    if (mailbox == NULL) {
        return;
    }

    taskENTER_CRITICAL(&s_mailbox_lock);
    ++mailbox->stats.discarded_stale_count;
    mailbox->active_seq = 0;
    taskEXIT_CRITICAL(&s_mailbox_lock);
}

void ink_display_mailbox_note_completed(ink_display_mailbox_t *mailbox, uint32_t seq)
{
    if (mailbox == NULL) {
        return;
    }

    taskENTER_CRITICAL(&s_mailbox_lock);
    mailbox->completed_seq = seq;
    mailbox->active_seq = 0;
    ++mailbox->stats.completed_count;
    taskEXIT_CRITICAL(&s_mailbox_lock);
}

void ink_display_mailbox_snapshot_stats(
    const ink_display_mailbox_t *mailbox,
    ink_display_mailbox_stats_t *stats)
{
    if (mailbox == NULL || stats == NULL) {
        return;
    }

    taskENTER_CRITICAL(&s_mailbox_lock);
    *stats = mailbox->stats;
    taskEXIT_CRITICAL(&s_mailbox_lock);
}

static bool mailbox_has_pending_request_locked(const ink_display_mailbox_t *mailbox)
{
    return mailbox != NULL
        && mailbox->latest_seq != 0U
        && mailbox->latest_seq != mailbox->completed_seq
        && mailbox->latest_seq != mailbox->active_seq;
}

bool ink_display_mailbox_self_test(void)
{
    static ink_display_mailbox_t mailbox;
    ink_display_request_t request = {
        .page = INK_RUNTIME_SHELL_PAGE_LIBRARY,
    };
    ink_display_request_t claimed = {0};
    ink_display_mailbox_stats_t stats = {0};
    uint8_t *page_a = malloc(EPD_GDEY0426T82_BUFFER_SIZE);
    uint8_t *page_b = malloc(EPD_GDEY0426T82_BUFFER_SIZE);
    uint8_t *native_a = malloc(EPD_GDEY0426T82_NATIVE_BUFFER_SIZE);
    uint8_t *native_b = malloc(EPD_GDEY0426T82_NATIVE_BUFFER_SIZE);

    if (page_a == NULL || page_b == NULL || native_a == NULL || native_b == NULL) {
        free(page_a);
        free(page_b);
        free(native_a);
        free(native_b);
        return false;
    }

    ink_display_mailbox_init(&mailbox, NULL, page_a, page_b, native_a, native_b);
    if (ink_display_mailbox_try_claim_latest(&mailbox, &claimed)) {
        free(page_a);
        free(page_b);
        free(native_a);
        free(native_b);
        return false;
    }

    request.input_ms = 10;
    request.submitted_ms = 20;
    request.use_bitmap_page = true;
    request.bitmap_page_length = EPD_GDEY0426T82_BUFFER_SIZE;
    request.use_native_page = true;
    request.native_page_length = EPD_GDEY0426T82_NATIVE_BUFFER_SIZE;
    memset(page_a, 0x12, EPD_GDEY0426T82_BUFFER_SIZE);
    memset(native_a, 0x34, EPD_GDEY0426T82_NATIVE_BUFFER_SIZE);
    request.bitmap_page_buffer = page_a;
    request.native_page_buffer = native_a;
    if (ink_display_mailbox_submit(&mailbox, &request) != 1U) {
        free(page_a);
        free(page_b);
        free(native_a);
        free(native_b);
        return false;
    }
    if (!ink_display_mailbox_try_claim_latest(&mailbox, &claimed)) {
        free(page_a);
        free(page_b);
        free(native_a);
        free(native_b);
        return false;
    }
    if (claimed.seq != 1U || claimed.input_ms != 10U) {
        free(page_a);
        free(page_b);
        free(native_a);
        free(native_b);
        return false;
    }
    if (!claimed.use_bitmap_page
        || claimed.bitmap_page_length != EPD_GDEY0426T82_BUFFER_SIZE
        || claimed.bitmap_page_buffer == NULL
        || claimed.bitmap_page_buffer == page_a
        || claimed.bitmap_page_buffer[0] != 0x12U) {
        free(page_a);
        free(page_b);
        free(native_a);
        free(native_b);
        return false;
    }
    if (!claimed.use_native_page
        || claimed.native_page_length != EPD_GDEY0426T82_NATIVE_BUFFER_SIZE
        || claimed.native_page_buffer == NULL
        || claimed.native_page_buffer == native_a
        || claimed.native_page_buffer[0] != 0x34U) {
        free(page_a);
        free(page_b);
        free(native_a);
        free(native_b);
        return false;
    }
    if (ink_display_mailbox_try_claim_latest(&mailbox, &claimed)) {
        free(page_a);
        free(page_b);
        free(native_a);
        free(native_b);
        return false;
    }

    request.input_ms = 11;
    request.submitted_ms = 21;
    if (ink_display_mailbox_submit(&mailbox, &request) != 2U) {
        free(page_a);
        free(page_b);
        free(native_a);
        free(native_b);
        return false;
    }
    if (!ink_display_mailbox_has_newer_than(&mailbox, 1U)) {
        free(page_a);
        free(page_b);
        free(native_a);
        free(native_b);
        return false;
    }
    if (ink_display_mailbox_is_idle(&mailbox)) {
        free(page_a);
        free(page_b);
        free(native_a);
        free(native_b);
        return false;
    }
    ink_display_mailbox_note_cancelled(&mailbox);
    if (!ink_display_mailbox_is_idle(&mailbox)) {
        free(page_a);
        free(page_b);
        free(native_a);
        free(native_b);
        return false;
    }

    request.input_ms = 11;
    request.submitted_ms = 21;
    (void)ink_display_mailbox_submit(&mailbox, &request);
    ink_display_mailbox_invalidate_pending(&mailbox);
    if (ink_display_mailbox_try_claim_latest(&mailbox, &claimed)) {
        free(page_a);
        free(page_b);
        free(native_a);
        free(native_b);
        return false;
    }
    if (!ink_display_mailbox_is_idle(&mailbox)) {
        free(page_a);
        free(page_b);
        free(native_a);
        free(native_b);
        return false;
    }

    request.input_ms = 12;
    request.submitted_ms = 22;
    if (ink_display_mailbox_submit(&mailbox, &request) != 3U) {
        free(page_a);
        free(page_b);
        free(native_a);
        free(native_b);
        return false;
    }
    if (!ink_display_mailbox_try_claim_latest(&mailbox, &claimed)) {
        free(page_a);
        free(page_b);
        free(native_a);
        free(native_b);
        return false;
    }
    if (claimed.seq != 3U || claimed.input_ms != 12U) {
        free(page_a);
        free(page_b);
        free(native_a);
        free(native_b);
        return false;
    }
    ink_display_mailbox_note_completed(&mailbox, claimed.seq);
    if (!ink_display_mailbox_is_idle(&mailbox)) {
        free(page_a);
        free(page_b);
        free(native_a);
        free(native_b);
        return false;
    }
    if (ink_display_mailbox_has_newer_than(&mailbox, claimed.seq)) {
        free(page_a);
        free(page_b);
        free(native_a);
        free(native_b);
        return false;
    }

    request.input_ms = 13;
    request.submitted_ms = 23;
    (void)ink_display_mailbox_submit(&mailbox, &request);
    if (!ink_display_mailbox_try_claim_latest(&mailbox, &claimed)) {
        free(page_a);
        free(page_b);
        free(native_a);
        free(native_b);
        return false;
    }
    ink_display_mailbox_note_discarded_stale(&mailbox);

    ink_display_mailbox_snapshot_stats(&mailbox, &stats);
    if (stats.submitted_count != 4U) {
        free(page_a);
        free(page_b);
        free(native_a);
        free(native_b);
        return false;
    }
    if (stats.merged_count != 2U) {
        free(page_a);
        free(page_b);
        free(native_a);
        free(native_b);
        return false;
    }
    if (stats.cancelled_count != 1U) {
        free(page_a);
        free(page_b);
        free(native_a);
        free(native_b);
        return false;
    }
    if (stats.discarded_stale_count != 1U) {
        free(page_a);
        free(page_b);
        free(native_a);
        free(native_b);
        return false;
    }
    if (stats.completed_count != 1U) {
        free(page_a);
        free(page_b);
        free(native_a);
        free(native_b);
        return false;
    }

    ink_display_mailbox_init(&mailbox, NULL, page_a, page_b, native_a, native_b);
    request.input_ms = 14;
    request.submitted_ms = 24;
    if (ulTaskNotifyTake(pdTRUE, 0) != 0U) {
        free(page_a);
        free(page_b);
        free(native_a);
        free(native_b);
        return false;
    }
    if (ink_display_mailbox_submit(&mailbox, &request) == 0U) {
        free(page_a);
        free(page_b);
        free(native_a);
        free(native_b);
        return false;
    }
    ink_display_mailbox_set_notify_task(&mailbox, xTaskGetCurrentTaskHandle());
    if (ulTaskNotifyTake(pdTRUE, 0) != 1U
        || !ink_display_mailbox_try_claim_latest(&mailbox, &claimed)
        || claimed.seq == 0U) {
        free(page_a);
        free(page_b);
        free(native_a);
        free(native_b);
        return false;
    }

    free(page_a);
    free(page_b);
    free(native_a);
    free(native_b);
    return true;
}
