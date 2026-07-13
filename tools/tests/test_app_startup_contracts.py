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


def test_reader_does_not_load_fonts_for_prerendered_pages() -> None:
    text = source("apps/reader/main/app_main.c")

    assert "ink_fonts_load(" not in text
    assert 'APP_START name=reader' in text
    assert 'APP_STAGE name=reader' in text


def test_reader_page_turn_uses_partial_refresh_with_cleanup_and_rollback() -> None:
    text = source("apps/reader/main/app_main.c")
    page_turn = text[
        text.index("if (target_page != book.current_page)") :
        text.index("if (ink_input_was_pressed(&input, INK_BUTTON_BACK))")
    ]

    assert "READER_PARTIAL_REFRESH_LIMIT = 50" in text
    assert "ink_hw_partial_refresh_area(" in page_turn
    assert 'PAGE_REFRESH mode=partial' in page_turn
    assert 'PAGE_REFRESH mode=cleanup_full' in page_turn
    assert "book.current_page = previous_page;" in page_turn
    assert "memcpy(framebuffer, previous_framebuffer" in page_turn


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
        "photo_ready = refresh_decoded_photo"
    )
