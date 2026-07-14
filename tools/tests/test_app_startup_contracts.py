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


def test_reader_reserves_stack_for_book_open_and_menu_rendering() -> None:
    defaults = source("apps/reader/sdkconfig.defaults")
    app = source("apps/reader/main/app_main.c")

    assert "CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192" in defaults
    assert "READER_MIN_MAIN_TASK_STACK_SIZE" in app
    assert "CONFIG_ESP_MAIN_TASK_STACK_SIZE >= READER_MIN_MAIN_TASK_STACK_SIZE" in app


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
    transaction = source("apps/reader/main/reader_page_turn.c")
    page_turn = text[
        text.index("static void handle_reading_page_turn(") :
        text.index("static void handle_reading_input(")
    ]
    refresh = text[
        text.index("static bool refresh_reader_page(") :
        text.index("static void handle_reading_page_turn(")
    ]

    assert "READER_PARTIAL_REFRESH_LIMIT = 50" in text
    assert "!reader_should_cleanup(48)" in text
    assert "reader_should_cleanup(49)" in text
    assert "PAGE_REFRESH mode=partial_full_window" in refresh
    assert (
        "ink_hw_partial_refresh_area_with_work_and_pump(\n"
        "        candidate, INK_EPD_BUFFER_SIZE, 0U, 0U, INK_HW_WIDTH, INK_HW_HEIGHT"
    ) in refresh
    assert "region.x" not in page_turn
    assert "region.y" not in page_turn
    assert "region.width" not in page_turn
    assert "region.height" not in page_turn
    assert 'PAGE_REFRESH mode=cleanup_full' in refresh
    assert 'PAGE_REFRESH mode=recovery_full' in refresh
    assert "reader_refresh_state_init(&refresh_state);" in text
    assert "reader_refresh_state_choose(" in refresh
    assert "reader_refresh_state_record(refresh_state, false);" in page_turn
    assert "*options->current_page = previous_page;" in transaction
    assert "memcpy(options->framebuffer, options->candidate" in transaction
    assert "*successful_page_turns + 1U" in page_turn
    assert "*successful_page_turns + page_step" not in page_turn


def test_partial_refresh_work_runs_after_activation_before_busy_wait() -> None:
    header = source("components/ink_hw/include/ink_hw.h")
    hw = source("components/ink_hw/ink_hw.c")

    assert "typedef esp_err_t (*ink_hw_refresh_work_fn)(void *context);" in header
    assert "ink_hw_partial_refresh_area_with_work(" in header
    wrapper = hw[
        hw.index("esp_err_t ink_hw_partial_refresh_area(") :
        hw.index("esp_err_t ink_hw_partial_refresh_area_with_work(")
    ]
    assert "ink_hw_partial_refresh_area_with_work(" in wrapper
    assert "NULL, NULL" in wrapper

    with_work_wrapper = hw[
        hw.index("esp_err_t ink_hw_partial_refresh_area_with_work(") :
        hw.index("esp_err_t ink_hw_partial_refresh_area_with_work_and_pump(")
    ]
    assert "ink_hw_partial_refresh_area_with_work_and_pump(" in with_work_wrapper
    assert "context, NULL, NULL" in with_work_wrapper

    with_work = hw[
        hw.index("esp_err_t ink_hw_partial_refresh_area_with_work_and_pump(") :
        hw.index("esp_err_t ink_hw_gray_refresh(")
    ]
    activate = with_work.index('command(0x20), TAG, "partial activate"')
    callback = with_work.index("if (work) (void)work(work_context);")
    wait = with_work.index("wait_ready_with_pump(")
    assert activate < callback < wait
    assert with_work.count("if (work) (void)work(work_context);") == 1


def test_reader_page_cache_is_optional_transactional_and_prefetches() -> None:
    app = source("apps/reader/main/app_main.c")
    transaction = source("apps/reader/main/reader_page_turn.c")
    cmake = source("apps/reader/main/CMakeLists.txt")
    page_turn = app[
        app.index("static void handle_reading_page_turn(") :
        app.index("static void handle_reading_input(")
    ]
    open_book = app[
        app.index("static bool open_selected_book(") :
        app.index("static bool toggle_selected_favorite(")
    ]

    assert '#include "reader_page_cache.h"' in app
    assert '#include "reader_page_turn.h"' in app
    assert '"reader_page_cache.c"' in cmake
    assert '"reader_page_turn.c"' in cmake
    assert "READER_PAGE_CACHE_SLOT_COUNT * INK_READER_PAGE_SIZE" in app
    assert "page cache allocation failed; direct decode fallback" in app
    assert "reader_page_cache_reset(" in open_book
    assert "reader_page_cache_lookup(" in transaction
    assert "ink_reader_book_decode_page(" in app
    assert "reader_page_cache_insert(" in transaction
    assert "ink_reader_book_load_page(" not in page_turn
    assert 'PAGE_CACHE result=hit page=%u prepare_ms=%lld' in app
    assert 'PAGE_CACHE result=miss page=%u prepare_ms=%lld' in app
    assert "reader_hold_target_page(\n      target_page" in page_turn
    assert "reader_page_turn_execute(" in page_turn
    assert "ink_hw_partial_refresh_area_with_work_and_pump(" in app
    assert "prefetch_and_restore" in transaction
    assert 'PAGE_CACHE result=prefetch_hit page=%u load_ms=%lld' in app
    assert 'PAGE_CACHE result=prefetch_loaded page=%u load_ms=%lld' in app
    assert 'PAGE_CACHE result=prefetch_failed page=%u load_ms=%lld' in app
    assert "xTaskCreate" not in page_turn


def test_reader_partial_refresh_pumps_and_replays_deferred_input() -> None:
    app = source("apps/reader/main/app_main.c")
    cmake = source("apps/reader/main/CMakeLists.txt")
    hw_header = source("components/ink_hw/include/ink_hw.h")
    hw = source("components/ink_hw/ink_hw.c")

    assert '#include "reader_input_queue.h"' in app
    assert '"reader_input_queue.c"' in cmake
    assert "ink_hw_refresh_pump_fn" in hw_header
    assert "ink_hw_partial_refresh_area_with_work_and_pump(" in hw_header
    partial = hw[
        hw.index("esp_err_t ink_hw_partial_refresh_area_with_work_and_pump(") :
        hw.index("esp_err_t ink_hw_gray_refresh(")
    ]
    activate = partial.index('command(0x20), TAG, "partial activate"')
    first_pump = partial.index("if (pump) pump(pump_context);")
    work = partial.index("if (work) (void)work(work_context);")
    assert activate < first_pump < work
    assert "wait_ready_with_pump(" in partial
    assert "pump_reader_input" in app
    assert "reader_input_queue_push(" in app
    assert "reader_input_queue_pop(" in app
    input_source = app[
        app.index("static bool next_reader_input(") :
        app.index("typedef struct {", app.index("static bool next_reader_input("))
    ]
    assert input_source.index("reader_input_queue_pop(") < input_source.index(
        "ink_input_poll("
    )
    main_loop = app[app.index("ink_reader_book_t book;") :]
    assert "next_reader_input(now_ms, &input)" in main_loop


def test_reader_hold_paging_uses_raw_hold_state_and_shared_refresh_path() -> None:
    text = source("apps/reader/main/app_main.c")
    cmake = source("apps/reader/main/CMakeLists.txt")
    input_header = source("components/ink_input/include/ink_input.h")
    input_source = source("components/ink_input/ink_input.c")

    assert '#include "reader_hold_paging.h"' in text
    assert '"reader_hold_paging.c"' in cmake
    assert "reader_hold_paging_update(" in text
    assert "ink_input_held_ms(" in text
    assert "input->released & ink_input_mask(button)" in text
    assert "uint32_t raw_down;" in input_header
    assert "ink_input_is_raw_down(" in input_header
    assert "out->raw_down = state->raw;" in input_source
    assert "hold_paging->active" in text
    assert "? ink_input_is_raw_down(input, button)" in text
    assert "reader_hold_target_page(" in text
    assert "handle_reading_page_turn(" in text
    assert "reader_hold_paging_init(&hold_paging);" in text


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
        "LIBRARY_TAB_X = INK_LAUNCHER_HEADER_GUTTER",
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
    draw_library = reader_ui[
        reader_ui.index("void ink_epd_ui_draw_library(") :
        reader_ui.index("ink_epd_region_t ink_epd_ui_library_selection_region(")
    ]
    assert "INK_LAUNCHER_DIVIDER_Y" in draw_library
    assert "view->header_meta" not in draw_library
    assert "LIBRARY_PAGE_" not in draw_library
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
    assert "draw_reader_footer(&candidate_book, candidate_framebuffer);" in app
    assert "draw_reader_footer((const ink_reader_book_t *)context, buffer);" in app
    assert ".decorate = decorate_reader_page" in app


def test_reader_back_navigation_remains_two_level() -> None:
    app = source("apps/reader/main/app_main.c")
    model = source("apps/reader/main/reader_app_model.c")

    assert 'save_state("return_library")' in app
    assert 'BOOT_SWITCH from=reader to=launcher' in app
    assert "READER_APP_EFFECT_RETURN_LAUNCHER" in app
    assert "READER_APP_PAGE_READING" in model
    assert "READER_LIBRARY_FOCUS_TABS" in model
    assert "READER_APP_EFFECT_CLOSE_READER_MENU" in model


def test_boot_loading_asset_is_flash_backed_and_fixed_geometry() -> None:
    header = source("components/ink_epd_ui/include/ink_epd_ui.h")
    cmake = source("components/ink_epd_ui/CMakeLists.txt")
    asset = source("components/ink_epd_ui/ink_loading_asset.c")

    assert "ink_epd_ui_loading_region(void)" in header
    assert "ink_epd_ui_draw_loading(uint8_t *buffer, size_t length)" in header
    assert '"ink_loading_asset.c"' in cmake
    for geometry in (
        "LOADING_REGION_X = 132",
        "LOADING_REGION_Y = 372",
        "LOADING_REGION_WIDTH = 216",
        "LOADING_REGION_HEIGHT = 56",
        "LOADING_IMAGE_X = 140",
        "LOADING_IMAGE_Y = 380",
        "LOADING_IMAGE_WIDTH = 200",
        "LOADING_IMAGE_HEIGHT = 40",
    ):
        assert geometry in asset
    assert "fopen(" not in asset
    assert "ink_sd" not in asset
    assert "ink_fonts" not in asset


def test_boot_loading_precedes_all_normal_boot_switches() -> None:
    launcher = source("apps/launcher/main/app_main.c")
    reader = source("apps/reader/main/app_main.c")
    photo = source("apps/photo/main/app_main.c")

    for app in (launcher, reader, photo):
        helper = app[
            app.index("static void show_boot_loading(") :
            app.index("\n}\n", app.index("static void show_boot_loading(")) + 3
        ]
        assert "ink_epd_ui_draw_loading(" in helper
        assert "ink_epd_ui_loading_region()" in helper
        assert "ink_hw_partial_refresh_area(" in helper
        assert 'BOOT_LOADING from=%s to=%s refresh=%s' in helper

    launcher_reader = launcher[
        launcher.index("static void boot_reader(") : launcher.index(
            "static void boot_photo("
        )
    ]
    launcher_photo = launcher[
        launcher.index("static void boot_photo(") : launcher.index(
            "static void boot_usb_msc("
        )
    ]
    for route, loading_call, switch_log, switch_call in (
        (
            launcher_reader,
            'show_boot_loading(framebuffer, "launcher", "reader")',
            'BOOT_SWITCH from=launcher to=reader',
            "ink_boot_switch_to_reader()",
        ),
        (
            launcher_photo,
            'show_boot_loading(framebuffer, "launcher", "photo")',
            'BOOT_SWITCH from=launcher to=photo',
            "ink_boot_switch_to_photo()",
        ),
    ):
        assert route.index(loading_call) < route.index(switch_log)
        assert route.index(switch_log) < route.index(switch_call)

    reader_return = reader[
        reader.index("if (effect == READER_APP_EFFECT_RETURN_LAUNCHER)") :
        reader.index("\n  }", reader.index("if (effect == READER_APP_EFFECT_RETURN_LAUNCHER)"))
    ]
    assert reader_return.index(
        'show_boot_loading(framebuffer, "reader", "launcher")'
    ) < reader_return.index('BOOT_SWITCH from=reader to=launcher')
    assert reader_return.index('BOOT_SWITCH from=reader to=launcher') < reader_return.index(
        "ink_boot_switch_to_launcher()"
    )

    photo_return = photo[
        photo.index("if (ink_input_was_pressed(&input, INK_BUTTON_BACK))") :
        photo.index("\n      }", photo.index("if (ink_input_was_pressed(&input, INK_BUTTON_BACK))"))
    ]
    assert photo_return.index(
        'show_boot_loading(lsb, "photo", "launcher")'
    ) < photo_return.index('BOOT_SWITCH from=photo to=launcher')
    assert photo_return.index('BOOT_SWITCH from=photo to=launcher') < photo_return.index(
        "ink_boot_switch_to_launcher()"
    )


def test_boot_loading_covers_reader_memory_error_return() -> None:
    reader = source("apps/reader/main/app_main.c")
    wait = reader[
        reader.index("static void wait_for_launcher(") :
        reader.index("\n}\n", reader.index("static void wait_for_launcher(")) + 3
    ]
    allocation_error = reader[
        reader.index("if (!framebuffer || !candidate_framebuffer)") :
        reader.index("uint8_t *page_cache_storage")
    ]

    assert "uint8_t *framebuffer" in wait
    assert wait.index(
        'show_boot_loading(framebuffer, "reader", "launcher")'
    ) < wait.index('BOOT_SWITCH from=reader to=launcher')
    assert "wait_for_launcher(memory_input_ret, error_framebuffer);" in allocation_error


def test_boot_switch_settings_targets() -> None:
    header = source("components/ink_boot_switch/include/ink_boot_switch.h")
    implementation = source("components/ink_boot_switch/ink_boot_switch.c")

    assert "esp_err_t ink_boot_switch_to_usb_msc(void);" in header
    assert "esp_err_t ink_boot_switch_to_wifi_setup(void);" in header
    assert 'ink_boot_switch_to_usb_msc(void) { return switch_to("usb_msc"); }' in implementation
    assert 'ink_boot_switch_to_wifi_setup(void) { return switch_to("wifi_setup"); }' in implementation


def test_launcher_settings_navigation_and_routes_are_bounded() -> None:
    app = source("apps/launcher/main/app_main.c")
    header = source("components/ink_epd_ui/include/ink_epd_ui.h")
    cmake = source("apps/launcher/main/CMakeLists.txt")

    for symbol in (
        "INK_EPD_UI_LAUNCHER_PAGE_MAIN",
        "INK_EPD_UI_LAUNCHER_PAGE_SETTINGS",
        "INK_EPD_UI_LAUNCHER_MAIN_ITEM_COUNT",
        "INK_EPD_UI_LAUNCHER_SETTINGS_ITEM_COUNT",
        "ink_epd_ui_draw_launcher_page(",
        "ink_epd_ui_launcher_page_selection_region(",
    ):
        assert symbol in header

    assert "ink_epd_ui_launcher_page_t page = INK_EPD_UI_LAUNCHER_PAGE_MAIN;" in app
    assert "launcher_wrapped_selection(" in app
    assert "INK_BUTTON_LEFT" in app and "INK_BUTTON_RIGHT" in app
    assert "page = INK_EPD_UI_LAUNCHER_PAGE_SETTINGS;" in app
    assert "page = INK_EPD_UI_LAUNCHER_PAGE_MAIN;" in app
    assert "ink_hw_full_refresh(" in app
    assert "INK_BUTTON_BACK" in app
    assert "ink_epd_ui_launcher_page_selection_region(" in app
    assert "ink_hw_partial_refresh_area(" in app

    for forbidden in (
        "ink_sd_mount(",
        "ink_fonts_load(",
        "esp_wifi_init(",
        "tinyusb_driver_install(",
        "tinyusb_msc",
        "tusb_init(",
    ):
        assert forbidden not in app
    assert "ink_sd" not in cmake
    assert "ink_fonts" not in cmake

    usb_route = app[app.index("static void boot_usb_msc(") : app.index("static void boot_wifi_setup(")]
    wifi_route = app[app.index("static void boot_wifi_setup(") : app.index("void app_main(")]
    for route, loading, log, call in (
        (
            usb_route,
            'show_boot_loading(framebuffer, "launcher", "usb_msc")',
            'BOOT_SWITCH from=launcher to=usb_msc',
            "ink_boot_switch_to_usb_msc()",
        ),
        (
            wifi_route,
            'show_boot_loading(framebuffer, "launcher", "wifi_setup")',
            'BOOT_SWITCH from=launcher to=wifi_setup',
            "ink_boot_switch_to_wifi_setup()",
        ),
    ):
        assert route.index(loading) < route.index(log) < route.index(call)


def test_minimal_settings_apps_have_only_placeholder_runtime() -> None:
    expected = {
        "usb_msc": "USB MSC / NOT READY",
        "wifi_setup": "WIFI SETUP / NOT READY",
    }
    forbidden = (
        "ink_system_runtime",
        "ink_system_services",
        "resource_coordinator",
        "usb_msc_coordinator",
        "wifi_coordinator",
        "mpu",
        "tilt",
        "ink_sd_mount(",
        "sdmmc_",
        "tinyusb_",
        "tusb_init(",
        "esp_wifi_",
        "nvs_flash_",
    )

    for name, placeholder in expected.items():
        root = f"apps/{name}"
        project = source(f"{root}/CMakeLists.txt")
        defaults = source(f"{root}/sdkconfig.defaults")
        main_cmake = source(f"{root}/main/CMakeLists.txt")
        app = source(f"{root}/main/app_main.c")

        assert 'set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../components")' in project
        assert "idf_build_set_property(MINIMAL_BUILD ON)" in project
        assert f"project({name})" in project
        for setting in (
            'CONFIG_IDF_TARGET="esp32s3"',
            "CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y",
            "CONFIG_PARTITION_TABLE_CUSTOM=y",
            'CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="../../partitions/partitions_16mb.csv"',
            "CONFIG_SPIRAM=y",
            "CONFIG_SPIRAM_MODE_OCT=y",
            "CONFIG_SPIRAM_SPEED_80M=y",
            "CONFIG_SPIRAM_USE_MALLOC=y",
        ):
            assert setting in defaults
        for dependency in ("ink_boot_switch", "ink_epd_ui", "ink_hw", "ink_input", "esp_timer"):
            assert dependency in main_cmake

        assert f'APP_START name={name}' in app
        assert placeholder in app
        assert "MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT" in app
        assert "ink_hw_init()" in app
        assert "ink_input_init()" in app
        assert "ink_epd_ui_draw_status(" in app
        assert "ink_hw_full_refresh(" in app
        assert "INK_BUTTON_BACK" in app
        back = app[app.index("ink_input_was_pressed(&input, INK_BUTTON_BACK)") :]
        loading = f'show_boot_loading(framebuffer, "{name}", "launcher")'
        log = f'BOOT_SWITCH from={name} to=launcher'
        assert back.index(loading) < back.index(log) < back.index("ink_boot_switch_to_launcher()")
        lowered = app.lower()
        for token in forbidden:
            assert token not in lowered


def test_flash_layout_scripts_use_fixed_app_slots_and_single_full_write() -> None:
    offsets = {
        "launcher": "0x20000",
        "reader": "0x220000",
        "photo": "0x420000",
        "usb_msc": "0x620000",
        "wifi_setup": "0x820000",
    }
    for app, offset in offsets.items():
        script = source(f"tools/flash_{app}.ps1")
        assert ". (Join-Path $PSScriptRoot 'idf_env.ps1')" in script
        assert offset in script
        assert "monitor" not in script.lower()

    full = source("tools/flash_all_layout.ps1")
    assert "param([Parameter(Mandatory = $true)][string]$Port)" in full
    assert ". (Join-Path $PSScriptRoot 'idf_env.ps1')" in full
    for app, offset in offsets.items():
        assert f"'apps\\{app}'" in full
        assert offset in full
    for offset, artifact in (
        ("0x0", "build\\bootloader\\bootloader.bin"),
        ("0x8000", "build\\partition_table\\partition-table.bin"),
        ("0xF000", "build\\ota_data_initial.bin"),
    ):
        assert offset in full and artifact in full
    assert full.count("python -m esptool") == 1
    assert "write_flash" in full
    assert "0xA20000" not in full and "0xC20000" not in full
    assert "future_a" not in full and "future_b" not in full
    assert "monitor" not in full.lower()
