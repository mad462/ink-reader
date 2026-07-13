from pathlib import Path


def test_missing_font_candidate_is_filtered_before_cpfont_load() -> None:
    text = Path("components/ink_fonts/ink_fonts.c").read_text(encoding="utf-8")
    start = text.index("static bool try_load_path")
    end = text.index("static bool load_from_directory", start)
    body = text[start:end]

    assert body.index("stat(path") < body.index("ink_cpfont_load(font, path)")
    assert 'ESP_LOGD(TAG, "font candidate missing path=%s"' in body
