#include "ink_runtime_shell.h"

#include <stdio.h>
#include <string.h>

enum {
    INK_RUNTIME_SHELL_HOME_ITEM_BUTTON_TEST = 0,
    INK_RUNTIME_SHELL_HOME_ITEM_FILE_BROWSER,
    INK_RUNTIME_SHELL_HOME_ITEM_COUNT
};

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
    if (dst == NULL || dst_size == 0) {
        return;
    }

    if (src == NULL) {
        dst[0] = '\0';
        return;
    }

    snprintf(dst, dst_size, "%s", src);
}

static uint32_t max_held_ms(const ink_runtime_shell_button_state_t *buttons, int *button_index)
{
    uint32_t best = 0;
    int best_index = -1;

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
    memset(shell, 0, sizeof(*shell));
    shell->page = INK_RUNTIME_SHELL_PAGE_HOME;
    shell->home_index = INK_RUNTIME_SHELL_HOME_ITEM_BUTTON_TEST;
    shell->full_refresh_requested = true;
    copy_text(shell->last_event, sizeof(shell->last_event), "EVENT NONE");
}

bool ink_runtime_shell_handle_command(ink_runtime_shell_t *shell, ink_runtime_shell_command_t command)
{
    if (shell == NULL || command == INK_RUNTIME_SHELL_COMMAND_NONE) {
        return false;
    }

    switch (shell->page) {
        case INK_RUNTIME_SHELL_PAGE_HOME:
            if (command == INK_RUNTIME_SHELL_COMMAND_NAV_PREVIOUS) {
                shell->home_index =
                    (uint8_t)((shell->home_index + INK_RUNTIME_SHELL_HOME_ITEM_COUNT - 1)
                    % INK_RUNTIME_SHELL_HOME_ITEM_COUNT);
                return true;
            }
            if (command == INK_RUNTIME_SHELL_COMMAND_NAV_NEXT) {
                shell->home_index = (uint8_t)((shell->home_index + 1) % INK_RUNTIME_SHELL_HOME_ITEM_COUNT);
                return true;
            }
            if (command == INK_RUNTIME_SHELL_COMMAND_CONFIRM) {
                shell->page = shell->home_index == INK_RUNTIME_SHELL_HOME_ITEM_BUTTON_TEST
                    ? INK_RUNTIME_SHELL_PAGE_BUTTON_TEST
                    : INK_RUNTIME_SHELL_PAGE_FILE_BROWSER;
                shell->full_refresh_requested = true;
                return true;
            }
            return false;

        case INK_RUNTIME_SHELL_PAGE_BUTTON_TEST:
            if (command == INK_RUNTIME_SHELL_COMMAND_BACK) {
                shell->page = INK_RUNTIME_SHELL_PAGE_HOME;
                shell->full_refresh_requested = true;
                return true;
            }
            return false;

        case INK_RUNTIME_SHELL_PAGE_FILE_BROWSER:
        case INK_RUNTIME_SHELL_PAGE_TXT_PREVIEW:
            return false;

        default:
            return false;
    }
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

static void render_home(
    const ink_runtime_shell_t *shell,
    const ink_file_browser_t *browser,
    ink_runtime_shell_view_t *view)
{
    copy_text(view->title, sizeof(view->title), "CROSSPOINT S3");
    copy_text(
        view->line1,
        sizeof(view->line1),
        shell->home_index == INK_RUNTIME_SHELL_HOME_ITEM_BUTTON_TEST ? "> BUTTON TEST" : "  BUTTON TEST");
    copy_text(
        view->line2,
        sizeof(view->line2),
        shell->home_index == INK_RUNTIME_SHELL_HOME_ITEM_FILE_BROWSER ? "> FILE BROWSER" : "  FILE BROWSER");
    copy_text(view->line3, sizeof(view->line3), browser != NULL && browser->entry_count > 0 ? "TF BROWSER READY" : "TF ROOT EMPTY");
    copy_text(view->line4, sizeof(view->line4), "CONF OPEN");
    copy_text(view->line5, sizeof(view->line5), "LEFT/RIGHT MOVE");
}

static void render_button_test(const ink_runtime_shell_t *shell, ink_runtime_shell_view_t *view)
{
    int hold_button = -1;
    const uint32_t hold_ms = max_held_ms(&shell->buttons, &hold_button);

    copy_text(view->title, sizeof(view->title), "BUTTON TEST");
    snprintf(
        view->line1,
        sizeof(view->line1),
        "B%d C%d L%d R%d P%d",
        shell->buttons.is_down[INK_RUNTIME_SHELL_BUTTON_BACK] ? 1 : 0,
        shell->buttons.is_down[INK_RUNTIME_SHELL_BUTTON_CONFIRM] ? 1 : 0,
        shell->buttons.is_down[INK_RUNTIME_SHELL_BUTTON_LEFT] ? 1 : 0,
        shell->buttons.is_down[INK_RUNTIME_SHELL_BUTTON_RIGHT] ? 1 : 0,
        shell->buttons.is_down[INK_RUNTIME_SHELL_BUTTON_POWER] ? 1 : 0);
    copy_text(view->line2, sizeof(view->line2), shell->last_event);
    if (hold_button >= 0) {
        snprintf(view->line3, sizeof(view->line3), "HOLD %s %04uMS", s_button_names[hold_button], (unsigned)hold_ms);
    } else {
        copy_text(view->line3, sizeof(view->line3), "HOLD IDLE");
    }
    copy_text(view->line4, sizeof(view->line4), "TRY ALL FIVE KEYS");
    copy_text(view->line5, sizeof(view->line5), "BACK HOME");
}

static void render_file_browser(
    const ink_file_browser_t *browser,
    ink_runtime_shell_view_t *view)
{
    ink_file_browser_view_t browser_view;

    ink_file_browser_render(browser, &browser_view);
    copy_text(view->title, sizeof(view->title), browser_view.title);
    copy_text(view->line1, sizeof(view->line1), browser_view.lines[0]);
    copy_text(view->line2, sizeof(view->line2), browser_view.lines[1]);
    copy_text(view->line3, sizeof(view->line3), browser_view.lines[2]);
    copy_text(view->line4, sizeof(view->line4), browser_view.lines[3]);
    copy_text(view->line5, sizeof(view->line5), browser_view.status);
}

static void render_txt_preview(
    const ink_runtime_shell_t *shell,
    const ink_txt_preview_t *preview,
    ink_runtime_shell_view_t *view)
{
    (void)shell;

    if (preview == NULL) {
        copy_text(view->title, sizeof(view->title), "TXT PREVIEW");
        copy_text(view->line1, sizeof(view->line1), "NO PREVIEW DATA");
        copy_text(view->line2, sizeof(view->line2), "");
        copy_text(view->line3, sizeof(view->line3), "");
        copy_text(view->line4, sizeof(view->line4), "");
        copy_text(view->line5, sizeof(view->line5), "BACK HOME");
        return;
    }

    copy_text(view->title, sizeof(view->title), preview->title);
    copy_text(view->line1, sizeof(view->line1), preview->lines[0]);
    copy_text(view->line2, sizeof(view->line2), preview->lines[1]);
    copy_text(view->line3, sizeof(view->line3), preview->lines[2]);
    copy_text(view->line4, sizeof(view->line4), preview->lines[3]);
    copy_text(view->line5, sizeof(view->line5), preview->status);
}

void ink_runtime_shell_render(
    const ink_runtime_shell_t *shell,
    const ink_file_browser_t *browser,
    const ink_txt_preview_t *preview,
    ink_runtime_shell_view_t *view)
{
    memset(view, 0, sizeof(*view));

    switch (shell->page) {
        case INK_RUNTIME_SHELL_PAGE_HOME:
            render_home(shell, browser, view);
            break;
        case INK_RUNTIME_SHELL_PAGE_BUTTON_TEST:
            render_button_test(shell, view);
            break;
        case INK_RUNTIME_SHELL_PAGE_FILE_BROWSER:
            render_file_browser(browser, view);
            break;
        case INK_RUNTIME_SHELL_PAGE_TXT_PREVIEW:
            render_txt_preview(shell, preview, view);
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
    static ink_runtime_shell_t shell;
    static ink_runtime_shell_view_t view;
    static ink_file_browser_t browser;
    static ink_txt_preview_t preview;
    static ink_runtime_shell_button_state_t buttons;

    memset(&buttons, 0, sizeof(buttons));
    memset(&browser, 0, sizeof(browser));
    ink_txt_preview_prepare_default(&preview);
    strcpy(preview.title, "SAMPLE.TXT");
    strcpy(preview.lines[0], "FILE FOUND");
    strcpy(preview.lines[1], "HELLO");
    strcpy(preview.status, "ASCII TEXT OK");
    preview.found_file = true;
    browser.entry_count = 1;

    ink_runtime_shell_init(&shell);
    ink_runtime_shell_render(&shell, &browser, &preview, &view);
    if (strcmp(view.title, "CROSSPOINT S3") != 0) {
        return false;
    }
    if (strcmp(view.line1, "> BUTTON TEST") != 0) {
        return false;
    }

    if (!ink_runtime_shell_handle_command(&shell, INK_RUNTIME_SHELL_COMMAND_NAV_NEXT)) {
        return false;
    }
    ink_runtime_shell_render(&shell, &browser, &preview, &view);
    if (strcmp(view.line2, "> FILE BROWSER") != 0) {
        return false;
    }

    if (!ink_runtime_shell_handle_command(&shell, INK_RUNTIME_SHELL_COMMAND_CONFIRM)) {
        return false;
    }
    shell.page = INK_RUNTIME_SHELL_PAGE_TXT_PREVIEW;
    ink_runtime_shell_render(&shell, &browser, &preview, &view);
    if (strcmp(view.title, "SAMPLE.TXT") != 0) {
        return false;
    }
    if (strcmp(view.line5, "ASCII TEXT OK") != 0) {
        return false;
    }

    shell.page = INK_RUNTIME_SHELL_PAGE_HOME;
    shell.home_index = INK_RUNTIME_SHELL_HOME_ITEM_BUTTON_TEST;
    if (!ink_runtime_shell_handle_command(&shell, INK_RUNTIME_SHELL_COMMAND_CONFIRM)) {
        return false;
    }
    memset(&buttons, 0, sizeof(buttons));
    buttons.is_down[INK_RUNTIME_SHELL_BUTTON_LEFT] = true;
    buttons.was_pressed[INK_RUNTIME_SHELL_BUTTON_LEFT] = true;
    buttons.held_ms[INK_RUNTIME_SHELL_BUTTON_LEFT] = 320;
    if (!ink_runtime_shell_note_buttons(&shell, &buttons)) {
        return false;
    }
    ink_runtime_shell_render(&shell, &browser, &preview, &view);
    if (strcmp(view.title, "BUTTON TEST") != 0) {
        return false;
    }
    if (strcmp(view.line1, "B0 C0 L1 R0 P0") != 0) {
        return false;
    }
    if (strstr(view.line2, "LEFT") == NULL) {
        return false;
    }
    if (strstr(view.line3, "0320MS") == NULL) {
        return false;
    }

    return true;
}
