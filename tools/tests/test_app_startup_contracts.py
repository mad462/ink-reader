from pathlib import Path


def source(path: str) -> str:
    return Path(path).read_text(encoding="utf-8")


def test_launcher_has_no_sd_or_cpfont_startup() -> None:
    text = source("apps/launcher/main/app_main.c")

    assert "ink_sd_mount(" not in text
    assert "ink_fonts_load(" not in text
    assert 'APP_START name=launcher' in text
    assert 'APP_STAGE name=launcher' in text


def test_reader_does_not_load_fonts_for_prerendered_pages() -> None:
    text = source("apps/reader/main/app_main.c")

    assert "ink_fonts_load(" not in text
    assert 'APP_START name=reader' in text
    assert 'APP_STAGE name=reader' in text
