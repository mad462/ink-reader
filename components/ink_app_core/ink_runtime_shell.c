#include "ink_runtime_shell.h"

#include <stdio.h>
#include <string.h>

enum {
    INK_RUNTIME_SHELL_HOLD_BUCKET_MS = 250,
};

static const char *s_button_names[INK_RUNTIME_SHELL_BUTTON_COUNT] = {
    [INK_RUNTIME_SHELL_BUTTON_BACK] = "BACK",
    [INK_RUNTIME_SHELL_BUTTON_CONFIRM] = "CONFIRM",
    [INK_RUNTIME_SHELL_BUTTON_LEFT] = "LEFT",
    [INK_RUNTIME_SHELL_BUTTON_RIGHT] = "RIGHT",
    [INK_RUNTIME_SHELL_BUTTON_POWER] = "POWER",
};

static void copy_text(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || dst_size == 0U) {
        return;
    }

    if (src == NULL) {
        dst[0] = '\0';
        return;
    }

    snprintf(dst, dst_size, "%s", src);
}

static void copy_browser_line_with_prefix(
    char *dst,
    size_t dst_size,
    const char *src,
    char prefix)
{
    if (dst == NULL || dst_size == 0U) {
        return;
    }
    if (src == NULL || src[0] == '\0') {
        dst[0] = '\0';
        return;
    }

    if (src[0] == '>' || src[0] == ' ') {
        snprintf(dst, dst_size, "%c%s", prefix, src + 1);
        return;
    }

    snprintf(dst, dst_size, "%c%s", prefix, src);
}

static uint32_t max_held_ms(const ink_runtime_shell_button_state_t *buttons, int *button_index)
{
    uint32_t best = 0U;
    int best_index = -1;

    if (buttons == NULL) {
        if (button_index != NULL) {
            *button_index = -1;
        }
        return 0U;
    }

    for (int i = 0; i < INK_RUNTIME_SHELL_BUTTON_COUNT; ++i) {
        if (buttons->is_down[i] && buttons->held_ms[i] >= best) {
            best = buttons->held_ms[i];
            best_index = i;
        }
    }

    if (button_index != NULL) {
        *button_index = best_index;
    }
    return best;
}

void ink_runtime_shell_init(ink_runtime_shell_t *shell)
{
    if (shell == NULL) {
        return;
    }

    memset(shell, 0, sizeof(*shell));
    shell->page = INK_RUNTIME_SHELL_PAGE_LIBRARY;
    shell->full_refresh_requested = true;
    copy_text(shell->last_event, sizeof(shell->last_event), "EVENT NONE");
}

bool ink_runtime_shell_handle_command(ink_runtime_shell_t *shell, ink_runtime_shell_command_t command)
{
    (void)shell;
    (void)command;
    return false;
}

bool ink_runtime_shell_note_buttons(
    ink_runtime_shell_t *shell,
    const ink_runtime_shell_button_state_t *buttons)
{
    bool changed = false;
    int hold_button = -1;
    uint32_t hold_ms;
    uint32_t hold_bucket;

    if (shell == NULL || buttons == NULL) {
        return false;
    }

    for (int i = 0; i < INK_RUNTIME_SHELL_BUTTON_COUNT; ++i) {
        if (shell->buttons.is_down[i] != buttons->is_down[i]
            || shell->buttons.was_pressed[i] != buttons->was_pressed[i]
            || shell->buttons.was_released[i] != buttons->was_released[i]) {
            changed = true;
        }
        shell->buttons.is_down[i] = buttons->is_down[i];
        shell->buttons.was_pressed[i] = buttons->was_pressed[i];
        shell->buttons.was_released[i] = buttons->was_released[i];
        shell->buttons.held_ms[i] = buttons->held_ms[i];
    }

    for (int i = 0; i < INK_RUNTIME_SHELL_BUTTON_COUNT; ++i) {
        if (buttons->was_pressed[i]) {
            snprintf(shell->last_event, sizeof(shell->last_event), "EVT %s PRESS", s_button_names[i]);
            changed = true;
            break;
        }
        if (buttons->was_released[i]) {
            snprintf(shell->last_event, sizeof(shell->last_event), "EVT %s RELEASE", s_button_names[i]);
            changed = true;
            break;
        }
    }

    hold_ms = max_held_ms(buttons, &hold_button);
    hold_bucket = hold_ms / INK_RUNTIME_SHELL_HOLD_BUCKET_MS;
    if (hold_bucket != shell->last_hold_bucket_ms) {
        shell->last_hold_bucket_ms = hold_bucket;
        if (hold_button >= 0) {
            snprintf(shell->last_event, sizeof(shell->last_event), "HOLD %s %04u", s_button_names[hold_button], (unsigned)hold_ms);
        }
        changed = true;
    }

    return changed;
}

static void render_library(
    const ink_runtime_shell_t *shell,
    const ink_file_browser_t *browser,
    ink_runtime_shell_view_t *view)
{
    ink_file_browser_view_t browser_view;
    (void)shell;

    copy_text(view->title, sizeof(view->title), "书库");

    if (browser == NULL) {
        copy_text(view->line1, sizeof(view->line1), "暂无书籍");
        return;
    }

    ink_file_browser_render(browser, &browser_view);
    copy_text(view->line1, sizeof(view->line1), browser_view.lines[0]);
    copy_text(view->line2, sizeof(view->line2), browser_view.lines[1]);
    copy_text(view->line3, sizeof(view->line3), browser_view.lines[2]);
    copy_text(view->line4, sizeof(view->line4), browser_view.lines[3]);
    view->line5[0] = '\0';
}

static void render_reader(
    const ink_reader_session_t *session,
    ink_runtime_shell_view_t *view)
{
    ink_reader_session_view_t session_view;

    if (ink_reader_session_get_text_view(session, &session_view)) {
        copy_text(view->title, sizeof(view->title), session_view.title);
        copy_text(view->line1, sizeof(view->line1), session_view.lines[0]);
        copy_text(view->line2, sizeof(view->line2), session_view.lines[1]);
        copy_text(view->line3, sizeof(view->line3), session_view.lines[2]);
        copy_text(view->line4, sizeof(view->line4), session_view.lines[3]);
        copy_text(view->line5, sizeof(view->line5), session_view.status);
        return;
    }

    copy_text(view->title, sizeof(view->title), "XTC READER");
    copy_text(view->line1, sizeof(view->line1), "NO ACTIVE BOOK");
    copy_text(view->line5, sizeof(view->line5), "BACK LIBRARY");
}

void ink_runtime_shell_render(
    const ink_runtime_shell_t *shell,
    const ink_file_browser_t *browser,
    const ink_reader_session_t *session,
    ink_runtime_shell_view_t *view)
{
    if (shell == NULL || view == NULL) {
        return;
    }

    memset(view, 0, sizeof(*view));
    switch (shell->page) {
        case INK_RUNTIME_SHELL_PAGE_LIBRARY:
            render_library(shell, browser, view);
            break;

        case INK_RUNTIME_SHELL_PAGE_READER:
            render_reader(session, view);
            break;

        default:
            copy_text(view->title, sizeof(view->title), "INK READER");
            copy_text(view->line1, sizeof(view->line1), "INVALID PAGE");
            break;
    }
}

bool ink_runtime_shell_requires_full_refresh(const ink_runtime_shell_t *shell)
{
    return shell != NULL && shell->full_refresh_requested;
}

void ink_runtime_shell_mark_rendered(ink_runtime_shell_t *shell)
{
    if (shell != NULL) {
        shell->full_refresh_requested = false;
    }
}

bool ink_runtime_shell_self_test(void)
{
    ink_runtime_shell_t shell;
    ink_runtime_shell_view_t view;
    ink_file_browser_t browser;
    ink_reader_session_t session;
    ink_reader_session_view_t session_view = {
        .title = "SESSION VIEW",
        .lines = {"A", "B", "C", "D"},
        .status = "S READY",
    };

    memset(&browser, 0, sizeof(browser));
    ink_reader_session_init(&session);
    browser.entry_count = 2U;
    snprintf(browser.mount_point, sizeof(browser.mount_point), "%s", "/sdcard/books");
    snprintf(browser.current_path, sizeof(browser.current_path), "%s", "/sdcard/books");
    browser.entries[0].type = INK_FILE_BROWSER_ENTRY_XTC;
    snprintf(browser.entries[0].name, sizeof(browser.entries[0].name), "%s", "A.XTC");
    snprintf(browser.entries[0].full_path, sizeof(browser.entries[0].full_path), "%s", "/sdcard/books/A.XTC");
    browser.entries[1].type = INK_FILE_BROWSER_ENTRY_XTC;
    snprintf(browser.entries[1].name, sizeof(browser.entries[1].name), "%s", "B.XTCH");
    snprintf(browser.entries[1].full_path, sizeof(browser.entries[1].full_path), "%s", "/sdcard/books/B.XTCH");
    browser.selected_index = 0U;

    ink_runtime_shell_init(&shell);
    ink_runtime_shell_render(&shell, &browser, &session, &view);
    if (strcmp(view.title, "书库") != 0) {
        return false;
    }
    if (strcmp(view.line1, ">A.XTC") != 0) {
        return false;
    }
    if (view.line5[0] != '\0') {
        return false;
    }

    shell.page = INK_RUNTIME_SHELL_PAGE_READER;
    if (!ink_reader_session_set_text_view(&session, &session_view)) {
        return false;
    }
    ink_runtime_shell_render(&shell, &browser, &session, &view);
    if (strcmp(view.title, "SESSION VIEW") != 0) {
        return false;
    }
    if (strcmp(view.line5, "S READY") != 0) {
        return false;
    }

    ink_reader_session_close(&session);
    return true;
}
