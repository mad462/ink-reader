#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "epd_gdey0426t82.h"
#include "ink_app_state.h"
#include "ink_button_input.h"
#include "ink_file_browser.h"
#include "ink_reader_session.h"
#include "ink_runtime_shell.h"
#include "ink_system_services.h"
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
    INK_UI_TASK_STACK_BYTES = 10240,
    INK_EPD_TASK_STACK_BYTES = 8192,
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

typedef enum {
    INK_READER_MENU_TAB_CHAPTERS = 0,
    INK_READER_MENU_TAB_BOOKMARKS,
    INK_READER_MENU_TAB_COUNT
} ink_reader_menu_tab_t;

typedef enum {
    INK_LIBRARY_TAB_RECENT = 0,
    INK_LIBRARY_TAB_ALL,
    INK_LIBRARY_TAB_FAVORITES,
    INK_LIBRARY_TAB_COUNT
} ink_library_tab_t;

typedef enum {
    INK_LIBRARY_FOCUS_ITEMS = 0,
    INK_LIBRARY_FOCUS_TABS,
    INK_LIBRARY_FOCUS_POPUP,
} ink_library_focus_t;

typedef enum {
    INK_READER_MENU_LEVEL_TABS = 0,
    INK_READER_MENU_LEVEL_ITEMS,
    INK_READER_MENU_LEVEL_BOOKMARK_ACTIONS,
} ink_reader_menu_level_t;

typedef struct {
    bool open;
    ink_reader_menu_tab_t active_tab;
    ink_reader_menu_level_t level;
    size_t chapter_item_index;
    size_t bookmark_item_index;
    uint8_t bookmark_action_index;
} ink_reader_menu_state_t;

typedef struct {
    ink_library_tab_t active_tab;
    ink_library_focus_t focus;
    size_t selected_index[INK_LIBRARY_TAB_COUNT];
    bool popup_open;
    uint8_t popup_action_index;
} ink_library_state_t;

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
    ink_reader_session_t reader_session;
    ink_file_browser_t browser;
    ink_runtime_shell_t shell;
    ink_runtime_shell_button_state_t buttons;
    ink_app_state_t app_state;
    ink_fast_browse_state_t fast_browse;
    ink_library_state_t library;
    ink_reader_menu_state_t reader_menu;
    bool reader_fast_full_commit_pending;
    bool reader_nav_pending;
    ink_fast_browse_dir_t reader_nav_pending_dir;
    uint32_t reader_nav_pending_start_ms;
    ink_tuning_lab_state_t lab;
} ink_ui_model_t;

typedef struct {
    ink_system_services_t services;
    ink_ui_model_t model;
    bool aggressive_interrupt_mode;
} ink_app_context_t;

typedef struct {
    ink_app_context_t *app;
    uint32_t seq;
    bool pin_during_transmitting;
} ink_epd_cancel_ctx_t;
