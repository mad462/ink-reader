#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ink_file_browser.h"
#include "ink_txt_reader.h"

#define INK_RUNTIME_SHELL_TEXT_LENGTH 23

typedef enum {
    INK_RUNTIME_SHELL_PAGE_HOME = 0,
    INK_RUNTIME_SHELL_PAGE_BUTTON_TEST,
    INK_RUNTIME_SHELL_PAGE_FILE_BROWSER,
    INK_RUNTIME_SHELL_PAGE_TXT_READER
} ink_runtime_shell_page_t;

typedef enum {
    INK_RUNTIME_SHELL_COMMAND_NONE = 0,
    INK_RUNTIME_SHELL_COMMAND_BACK,
    INK_RUNTIME_SHELL_COMMAND_CONFIRM,
    INK_RUNTIME_SHELL_COMMAND_NAV_PREVIOUS,
    INK_RUNTIME_SHELL_COMMAND_NAV_NEXT
} ink_runtime_shell_command_t;

typedef enum {
    INK_RUNTIME_SHELL_BUTTON_BACK = 0,
    INK_RUNTIME_SHELL_BUTTON_CONFIRM,
    INK_RUNTIME_SHELL_BUTTON_LEFT,
    INK_RUNTIME_SHELL_BUTTON_RIGHT,
    INK_RUNTIME_SHELL_BUTTON_POWER,
    INK_RUNTIME_SHELL_BUTTON_COUNT
} ink_runtime_shell_button_t;

typedef struct {
    bool is_down[INK_RUNTIME_SHELL_BUTTON_COUNT];
    bool was_pressed[INK_RUNTIME_SHELL_BUTTON_COUNT];
    bool was_released[INK_RUNTIME_SHELL_BUTTON_COUNT];
    uint32_t held_ms[INK_RUNTIME_SHELL_BUTTON_COUNT];
} ink_runtime_shell_button_state_t;

typedef struct {
    char title[INK_TXT_READER_TITLE_LENGTH + 1];
    char line1[INK_RUNTIME_SHELL_TEXT_LENGTH + 1];
    char line2[INK_RUNTIME_SHELL_TEXT_LENGTH + 1];
    char line3[INK_RUNTIME_SHELL_TEXT_LENGTH + 1];
    char line4[INK_RUNTIME_SHELL_TEXT_LENGTH + 1];
    char line5[INK_RUNTIME_SHELL_TEXT_LENGTH + 1];
} ink_runtime_shell_view_t;

typedef struct {
    ink_runtime_shell_page_t page;
    uint8_t home_index;
    bool full_refresh_requested;
    char last_event[INK_RUNTIME_SHELL_TEXT_LENGTH + 1];
    ink_runtime_shell_button_state_t buttons;
    uint32_t last_hold_bucket_ms;
} ink_runtime_shell_t;

void ink_runtime_shell_init(ink_runtime_shell_t *shell);
bool ink_runtime_shell_handle_command(ink_runtime_shell_t *shell, ink_runtime_shell_command_t command);
bool ink_runtime_shell_note_buttons(
    ink_runtime_shell_t *shell,
    const ink_runtime_shell_button_state_t *buttons
);
void ink_runtime_shell_render(
    const ink_runtime_shell_t *shell,
    const ink_file_browser_t *browser,
    const ink_txt_reader_t *reader,
    ink_runtime_shell_view_t *view
);
bool ink_runtime_shell_requires_full_refresh(const ink_runtime_shell_t *shell);
void ink_runtime_shell_mark_rendered(ink_runtime_shell_t *shell);
bool ink_runtime_shell_self_test(void);
