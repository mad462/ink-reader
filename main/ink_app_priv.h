#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "epd_gdey0426t82.h"
#include "ink_app_state.h"
#include "ink_button_input.h"
#include "ink_cpfont.h"
#include "ink_display_mailbox.h"
#include "ink_file_browser.h"
#include "ink_reader_session.h"
#include "ink_runtime_shell.h"
#include "ink_tuning_lab.h"

#define INK_APP_MOUNT_POINT "/sdcard"
#define INK_APP_BOOKS_PATH "/sdcard/books"
#define INK_APP_STATE_FILE_PATH "/sdcard/.ink-reader/state.bin"

enum {
    INK_INPUT_TASK_PERIOD_MS = 5,
    INK_INPUT_HOLD_EVENT_MS = 125,
    INK_UI_IDLE_WAIT_MS = 250,
    INK_FAST_BROWSE_IDLE_TICK_MS = 60,
    INK_UI_QUEUE_LENGTH = 24,
    INK_INPUT_TASK_STACK_BYTES = 3072,
    INK_UI_TASK_STACK_BYTES = 6144,
    INK_EPD_TASK_STACK_BYTES = 5120,
    INK_FAST_BROWSE_ENTER_MS = 220,
};

typedef enum {
    INK_UI_EVENT_BUTTON = 0,
    INK_UI_EVENT_DISPLAY_DONE,
} ink_ui_event_kind_t;

typedef struct {
    ink_ui_event_kind_t kind;
    uint32_t event_ms;
    union {
        ink_button_snapshot_t snapshot;
        struct {
            uint32_t seq;
            esp_err_t result;
            epd_gdey0426t82_phase_t phase;
        } display_done;
    } data;
} ink_ui_event_t;

typedef enum {
    INK_FAST_BROWSE_DIR_BACKWARD = -1,
    INK_FAST_BROWSE_DIR_FORWARD = 1,
} ink_fast_browse_dir_t;

typedef struct {
    bool active;
    bool dirty;
    bool commit_pending;
    bool cancel_pending;
    bool commit_fast_full_pending;
    bool release_armed;
    size_t origin_page;
    size_t target_page;
    size_t visible_page;
    bool has_visible_page;
    size_t total_pages;
    ink_fast_browse_dir_t direction;
    uint32_t hold_start_ms;
    uint32_t last_step_ms;
} ink_fast_browse_state_t;

typedef struct {
    bool tf_ready;
    ink_reader_session_t reader_session;
    ink_file_browser_t browser;
    ink_runtime_shell_t shell;
    ink_runtime_shell_button_state_t buttons;
    ink_app_state_t app_state;
    ink_cpfont_t reader_font;
    ink_cpfont_t footer_font;
    ink_fast_browse_state_t fast_browse;
    bool reader_fast_full_commit_pending;
    bool reader_nav_pending;
    ink_fast_browse_dir_t reader_nav_pending_dir;
    uint32_t reader_nav_pending_start_ms;
    ink_tuning_lab_state_t lab;
} ink_ui_model_t;

typedef struct {
    ink_display_mailbox_t mailbox;
    QueueHandle_t ui_queue;
    ink_ui_model_t model;
    uint8_t *framebuffer;
    uint8_t *previous_framebuffer;
    uint8_t *bitmap_snapshot_a;
    uint8_t *bitmap_snapshot_b;
    uint8_t *native_snapshot_a;
    uint8_t *native_snapshot_b;
    bool aggressive_interrupt_mode;
} ink_app_context_t;

typedef struct {
    ink_app_context_t *app;
    uint32_t seq;
    bool pin_during_transmitting;
} ink_epd_cancel_ctx_t;
