from pathlib import Path


def source(path: str) -> str:
    return Path(path).read_text(encoding="utf-8")


def test_launcher_has_no_sd_or_cpfont_startup() -> None:
    text = source("apps/launcher/main/app_main.c")
    defaults = source("apps/launcher/sdkconfig.defaults")
    ui = source("components/ink_epd_ui/ink_epd_ui.c")

    assert "ink_sd_mount(" not in text
    assert "ink_fonts_load(" not in text
    assert "CONFIG_FATFS_" not in defaults
    assert "ink_fonts_utf8_truncate_tail" not in ui
    assert 'APP_START name=launcher' in text
    assert 'APP_STAGE name=launcher' in text


def test_reader_starts_in_library_with_catalog_state_and_menu_fonts() -> None:
    text = source("apps/reader/main/app_main.c")
    cmake = source("apps/reader/main/CMakeLists.txt")

    assert "ink_reader_catalog_load(" in text
    assert "ink_reader_state_load(INK_READER_STATE_PATH" in text
    assert "ink_fonts_load(&s_menu_font, INK_FONT_MENU)" in text
    assert "ink_fonts_load(&s_footer_font, INK_FONT_FOOTER)" in text
    assert "ink_epd_ui_draw_library(" in text
    assert "ink_reader_open_first_book(" not in text
    assert '"reader_app_model.c"' in cmake
    assert "ink_fonts" in cmake
    assert 'APP_START name=reader' in text
    assert 'APP_STAGE name=reader' in text


def test_reader_app_model_owns_library_state_machine() -> None:
    header = source("apps/reader/main/reader_app_model.h")
    model = source("apps/reader/main/reader_app_model.c")
    app = source("apps/reader/main/app_main.c")

    for symbol in (
        "READER_APP_PAGE_LIBRARY",
        "READER_APP_PAGE_READING",
        "READER_LIBRARY_TAB_RECENT",
        "READER_LIBRARY_TAB_ALL",
        "READER_LIBRARY_TAB_FAVORITES",
        "READER_LIBRARY_FOCUS_ITEMS",
        "READER_LIBRARY_FOCUS_TABS",
        "READER_LIBRARY_FOCUS_POPUP",
        "READER_APP_EFFECT_OPEN_SELECTED",
        "READER_APP_EFFECT_TOGGLE_FAVORITE",
        "READER_APP_EFFECT_RETURN_LAUNCHER",
        "reader_app_model_reduce",
        "reader_app_model_rebuild",
        "reader_app_model_self_test",
    ):
        assert symbol in header or symbol in model
    assert "recent_order" in model
    assert "visible_catalog" in model
    assert "reader_app_model_reduce(" in app
    assert "reader_app_model_rebuild(" in app
    assert "candidate_model->focus = READER_LIBRARY_FOCUS_ITEMS" in app
    assert "ink_reader_state_remember_open(" in app


def test_reader_page_turn_uses_partial_refresh_with_cleanup_and_rollback() -> None:
    text = source("apps/reader/main/app_main.c")
    page_turn = text[
        text.index("static void handle_reading_input(") :
        text.index("static bool input_event(")
    ]

    assert "READER_PARTIAL_REFRESH_LIMIT = 50" in text
    assert "!reader_should_cleanup(48)" in text
    assert "reader_should_cleanup(49)" in text
    assert "ink_hw_partial_refresh_area(" in page_turn
    assert 'PAGE_REFRESH mode=partial' in page_turn
    assert 'PAGE_REFRESH mode=cleanup_full' in page_turn
    assert 'PAGE_REFRESH mode=recovery_full' in page_turn
    assert "bool screen_ready = false;" in text
    assert "*screen_ready = false;" in page_turn
    assert "book->current_page = previous_page;" in page_turn
    assert "memcpy(framebuffer, candidate_framebuffer" in page_turn


def test_reader_memory_error_keeps_back_navigation_available() -> None:
    text = source("apps/reader/main/app_main.c")
    allocation_error = text[
        text.index("if (!framebuffer || !candidate_framebuffer)") :
        text.index("const esp_err_t display_ret")
    ]

    assert '"MEMORY ERROR"' in allocation_error
    assert "wait_for_launcher(" in allocation_error


def test_photo_starts_in_preview_and_font_task_never_touches_epd() -> None:
    text = source("apps/photo/main/app_main.c")

    assert "enum photo_view view = PREVIEW;" in text
    assert "xTaskCreate" in text
    assert "xEventGroupSetBits" in text
    assert "PHOTO_FONT_DONE" in text
    task = text[text.index("static void photo_font_task") : text.index("void app_main")]
    assert "ink_hw_" not in task

    status = text[text.index("static bool show_status_page") :
                  text.index("static bool try_decode_item")]
    assert "s_photo_fonts" not in status

    main = text[text.index("void app_main") :]
    assert main.index("xTaskCreate") < main.index(
        "first_result = refresh_decoded_photo"
    )


def test_photo_gray_refresh_interrupts_busy_wait_for_latest_navigation() -> None:
    photo = source("apps/photo/main/app_main.c")
    hw_header = source("components/ink_hw/include/ink_hw.h")
    hw = source("components/ink_hw/ink_hw.c")

    assert "ink_hw_gray_refresh_with_poll(" in hw_header
    assert "ink_hw_gray_refresh_with_poll(" in photo
    assert "photo_navigation_poll" in photo
    assert "photo_navigation_self_test()" in photo
    assert "photo_navigation_commit_displayed" in photo
    assert "ESP_ERR_NOT_FINISHED" in photo
    assert "wait_ready(" in hw
    assert "poll(context)" in hw
    assert 'wait_ready("gray_update", poll, context, WAIT_CANCEL_IMMEDIATE)' in hw
    assert "WAIT_CANCEL_AFTER_READY" in hw
    assert "cancel_mode == WAIT_CANCEL_IMMEDIATE" in hw
    assert hw.count(
        "if (poll && poll(context)) return ESP_ERR_NOT_FINISHED;"
    ) >= 4
    gray_refresh = hw[hw.index("esp_err_t ink_hw_gray_refresh_with_poll(") :]
    assert "ESP_RETURN_ON_ERROR(update(" not in gray_refresh
    assert "aggressive" not in hw.lower()
    assert "busy_wait aborted" not in hw


def test_photo_list_omits_position_counter() -> None:
    ui = source("components/ink_epd_ui/ink_epd_ui.c")
    draw_list = ui[
        ui.index("void ink_epd_ui_draw_photo_list_with_fonts") :
        ui.index("void ink_epd_ui_draw_photo_list(")
    ]

    assert "snprintf(counter" not in draw_list
    assert "counter_x" not in draw_list


def test_reader_library_ui_is_a_pure_bounded_renderer() -> None:
    header = source("components/ink_epd_ui/include/ink_epd_ui.h")
    cmake = source("components/ink_epd_ui/CMakeLists.txt")
    common_ui = source("components/ink_epd_ui/ink_epd_ui.c")
    reader_ui = source("components/ink_epd_ui/ink_reader_ui.c")

    assert "INK_EPD_UI_MENU_TAB_CAPACITY 3" in header
    assert "INK_EPD_UI_MENU_CARD_CAPACITY 8" in header
    assert "INK_EPD_UI_MENU_ACTION_CAPACITY 4" in header
    for field in (
        "header_title",
        "header_meta",
        "tabs",
        "active",
        "focused",
        "cards",
        "title",
        "line1",
        "line2",
        "selected",
        "trailing_favorite",
        "popup_title",
        "actions",
    ):
        assert field in header
    assert "ink_epd_ui_draw_library(" in header
    assert "ink_epd_ui_library_selection_region(" in header
    assert "ink_epd_ui_reader_self_test(void)" in header
    assert "tabs_focused" in header
    assert '"ink_reader_ui.c"' in cmake
    assert "ink_epd_ui_reader_self_test()" in common_ui
    assert "previous->tabs_focused != current->tabs_focused" in reader_ui
    assert "current.tabs_focused = true;" in reader_ui
    assert "previous.tabs_focused = true;" in reader_ui

    for geometry in (
        "LIBRARY_PAGE_X = 8",
        "LIBRARY_PAGE_Y = 8",
        "LIBRARY_PAGE_WIDTH = 464",
        "LIBRARY_PAGE_HEIGHT = 776",
        "LIBRARY_HEADER_HEIGHT = 38",
        "LIBRARY_TAB_X = 24",
        "LIBRARY_TAB_Y = 58",
        "LIBRARY_TAB_WIDTH = 138",
        "LIBRARY_TAB_GAP = 8",
        "LIBRARY_TAB_HEIGHT = 42",
        "LIBRARY_CARD_X = 24",
        "LIBRARY_CARD_Y = 108",
        "LIBRARY_CARD_WIDTH = 432",
        "LIBRARY_CARD_HEIGHT = 58",
        "LIBRARY_CARD_GAP = 6",
        "LIBRARY_POPUP_X = 54",
        "LIBRARY_POPUP_Y = 314",
        "LIBRARY_POPUP_WIDTH = 372",
        "LIBRARY_POPUP_HEIGHT = 164",
        "LIBRARY_ACTION_HEIGHT = 34",
        "LIBRARY_ACTION_GAP = 10",
    ):
        assert geometry in reader_ui

    assert "draw_heart_icon(" in reader_ui
    assert "draw_clipped_text(" in reader_ui
    assert "ink_epd_ui_reader_self_test" in reader_ui
    for forbidden in (
        "ink_system_runtime",
        "ink_display_mailbox",
        "ink_display_request",
        "ink_system_services",
        "ink_resource_coordinator",
        "wifi",
        "voice",
        "usb",
    ):
        assert forbidden not in reader_ui.lower()
