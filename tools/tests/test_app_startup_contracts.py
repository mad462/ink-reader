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


def test_reader_library_has_complete_ascii_font_fallback() -> None:
    app = source("apps/reader/main/app_main.c")

    assert "bool localized" in app
    assert "ink_cpfont_is_loaded(s_library_fonts.body)" in app
    for label in (
        "LIBRARY",
        "RECENT",
        "ALL",
        "FAVORITES",
        "NO BOOKS",
        "NO RECENT",
        "NO FAVORITES",
        "OPEN",
        "ADD FAVORITE",
        "REMOVE FAVORITE",
        "UNREAD",
    ):
        assert f'"{label}"' in app


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
    assert "reader_refresh_state_init(&refresh_state);" in text
    assert "reader_refresh_state_choose(" in page_turn
    assert "reader_refresh_state_record(refresh_state, false);" in page_turn
    assert "book->current_page = previous_page;" in page_turn
    assert "memcpy(framebuffer, candidate_framebuffer" in page_turn


def test_reader_all_refresh_paths_share_recovery_state() -> None:
    text = source("apps/reader/main/app_main.c")
    policy = source("apps/reader/main/reader_refresh_policy.c")
    library_refresh = text[
        text.index("static bool refresh_library_candidate(") :
        text.index("static bool save_state(")
    ]

    assert "reader_refresh_state_choose(" in library_refresh
    assert "READER_REFRESH_FULL" in library_refresh
    assert "reader_refresh_state_record(refresh_state, ret == ESP_OK);" in library_refresh
    assert "!state->screen_ready ? READER_REFRESH_FULL : preferred" in policy


def test_reader_menu_integration_is_transactional_and_redecodes_pages() -> None:
    text = source("apps/reader/main/app_main.c")
    mutation = text[
        text.index("static bool apply_bookmark_mutation(") :
        text.index("static bool jump_from_reader_menu(")
    ]

    for symbol in (
        "build_reader_menu_view(",
        "refresh_reader_menu_candidate(",
        "refresh_reader_page_candidate(",
        "reader_app_model_reduce_reading(",
        "ink_reader_book_jump_to_chapter(",
        "reader_app_model_bookmark_slot(",
        "ink_reader_state_bookmark_add_or_replace(",
        "ink_reader_state_bookmark_overwrite(",
        "ink_reader_state_bookmark_remove_at(candidate_state, slot)",
        "ink_reader_state_save(INK_READER_STATE_PATH, candidate_state)",
    ):
        assert symbol in text
    assert '"T+%u:%02u"' in text
    assert "reader_app_model_close_reader_menu(candidate_model);" in text
    assert "ink_epd_ui_draw_reader_menu(" in text
    assert "ink_hw_partial_refresh_area(" in text
    assert "memcpy(&s_state, candidate_state, sizeof(s_state));" in text
    assert mutation.index("refresh_reader_menu_candidate(") < mutation.index(
        "ink_reader_state_save(INK_READER_STATE_PATH, candidate_state)"
    )
    assert mutation.count("refresh_reader_menu_candidate(") >= 2


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


def test_reader_menu_ui_is_a_bounded_overlay_renderer() -> None:
    header = source("components/ink_epd_ui/include/ink_epd_ui.h")
    reader_ui = source("components/ink_epd_ui/ink_reader_ui.c")

    assert "INK_EPD_UI_READER_MENU_TAB_CAPACITY 2" in header
    assert "INK_EPD_UI_READER_MENU_ITEM_CAPACITY 8" in header
    assert "INK_EPD_UI_READER_MENU_BOOKMARK_VISIBLE 6" in header
    assert "INK_EPD_UI_READER_MENU_ACTION_CAPACITY 3" in header
    assert "ink_epd_ui_reader_menu_view_t" in header
    assert "ink_epd_ui_reader_menu_focus_t" in header
    assert "ink_epd_ui_draw_reader_menu(" in header
    assert "ink_epd_ui_reader_menu_selection_region(" in header

    for geometry in (
        "READER_MENU_PANEL_X = 24",
        "READER_MENU_PANEL_Y = 118",
        "READER_MENU_PANEL_WIDTH = 432",
        "READER_MENU_PANEL_HEIGHT = 534",
        "READER_MENU_TAB_Y = 136",
        "READER_MENU_TAB_HEIGHT = 42",
        "READER_MENU_TAB_GAP = 10",
        "READER_MENU_CHAPTER_Y = 200",
        "READER_MENU_CHAPTER_HEIGHT = 48",
        "READER_MENU_CHAPTER_GAP = 8",
        "READER_MENU_BOOKMARK_Y = 200",
        "READER_MENU_BOOKMARK_HEIGHT = 58",
        "READER_MENU_BOOKMARK_GAP = 8",
        "READER_MENU_POPUP_X = 70",
        "READER_MENU_POPUP_Y = 281",
        "READER_MENU_POPUP_WIDTH = 340",
        "READER_MENU_POPUP_HEIGHT = 208",
    ):
        assert geometry in reader_ui

    draw_menu = reader_ui[
        reader_ui.index("void ink_epd_ui_draw_reader_menu(") :
        reader_ui.index("ink_epd_region_t ink_epd_ui_reader_menu_selection_region(")
    ]
    assert "READER_MENU_PANEL_X" in draw_menu
    assert "READER_MENU_PANEL_Y" in draw_menu
    assert "INK_EPD_UI_READER_MENU_ITEM_CAPACITY" in draw_menu
    assert "INK_EPD_UI_READER_MENU_BOOKMARK_VISIBLE" in draw_menu
    assert "ink_epd_ui_clear(" not in draw_menu


def test_reader_footer_is_bounded_and_overlaid_after_page_decode() -> None:
    header = source("components/ink_epd_ui/include/ink_epd_ui.h")
    reader_ui = source("components/ink_epd_ui/ink_reader_ui.c")
    core_header = source("components/ink_reader_core/include/ink_reader_core.h")
    core = source("components/ink_reader_core/ink_reader_core.c")
    app = source("apps/reader/main/app_main.c")

    assert "ink_epd_ui_draw_reader_footer(" in header
    assert "ink_reader_book_resolve_display_chapter(" in core_header
    assert "ink_reader_chapter_title_is_displayable(" in core_header
    assert "ink_reader_chapter_title_is_displayable(" in core
    assert "ink_reader_book_resolve_display_chapter(" in core
    for geometry in (
        "READER_FOOTER_BAND_Y = 780",
        "READER_FOOTER_BAND_HEIGHT = 20",
        "READER_FOOTER_SEPARATOR_Y = 779",
        "READER_FOOTER_TEXT_Y = 782",
    ):
        assert geometry in reader_ui

    draw_footer = reader_ui[
        reader_ui.index("void ink_epd_ui_draw_reader_footer(") :
        reader_ui.index("bool ink_epd_ui_reader_self_test(void)")
    ]
    assert "ink_epd_ui_fill_rect(" in draw_footer
    assert "draw_clipped_text(" in draw_footer
    assert '"%u%% %u/%u"' in app
    assert "draw_reader_footer(book, candidate_framebuffer);" in app
    assert app.count("draw_reader_footer(book, candidate_framebuffer);") >= 2


def test_reader_back_navigation_remains_two_level() -> None:
    app = source("apps/reader/main/app_main.c")
    model = source("apps/reader/main/reader_app_model.c")

    assert 'save_state("return_library")' in app
    assert 'BOOT_SWITCH from=reader to=launcher' in app
    assert "READER_APP_EFFECT_RETURN_LAUNCHER" in app
    assert "READER_APP_PAGE_READING" in model
    assert "READER_LIBRARY_FOCUS_TABS" in model
    assert "READER_APP_EFFECT_CLOSE_READER_MENU" in model
