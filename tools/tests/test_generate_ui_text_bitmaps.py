import re
import subprocess
import sys
from pathlib import Path

import pytest
from fontTools.ttLib import TTFont

import tools.generate_ui_text_bitmaps as generator
from tools.generate_ui_text_bitmaps import DEFAULT_FONT_PATH, PHRASES, Phrase, generate


ROOT = Path(__file__).resolve().parents[2]
FONT_PATH = ROOT / "tools" / "testdata" / "LXGWWenKai-ui-subset.ttf"
CHECKED_IN_SOURCE = ROOT / "components" / "ink_epd_ui" / "ink_ui_text_assets.c"
REQUIRED_PHRASES = (
    Phrase("启动器", 24),
    Phrase("阅读 / 相册", 16),
    Phrase("书库", 12),
    Phrase("相册", 12),
    Phrase("打开图书与最近阅读", 16),
    Phrase("浏览 TF 卡灰阶图片", 16),
    Phrase("正在加载", 16),
    Phrase("字体不可用", 16),
    Phrase("未找到图片", 16),
    Phrase("未找到可显示图片", 16),
    Phrase("未找到书籍", 16),
    Phrase("SD 卡错误", 16),
    Phrase("书籍格式错误", 16),
)
ARRAY_RE = re.compile(
    r"static const uint8_t (?P<name>s_bitmap_\d+)\[\] = \{(?P<body>.*?)\n\};",
    re.DOTALL,
)
RECORD_RE = re.compile(
    r'\{"(?P<text>(?:\\x[0-9A-F]{2})+)", (?P<size>\d+), '
    r"(?P<width>\d+), (?P<height>\d+), (?P<bitmap>s_bitmap_\d+)\},"
)


def _decode_c_bytes(value: str) -> str:
    return bytes(int(byte, 16) for byte in re.findall(r"\\x([0-9A-F]{2})", value)).decode(
        "utf-8"
    )


def _parse_assets(source: str) -> tuple[dict[str, bytes], list[tuple[str, int, int, int, str]]]:
    arrays = {
        match.group("name"): bytes(
            int(byte, 16) for byte in re.findall(r"0x([0-9A-F]{2})", match.group("body"))
        )
        for match in ARRAY_RE.finditer(source)
    }
    records = [
        (
            _decode_c_bytes(match.group("text")),
            int(match.group("size")),
            int(match.group("width")),
            int(match.group("height")),
            match.group("bitmap"),
        )
        for match in RECORD_RE.finditer(source)
    ]
    return arrays, records


def test_phrase_catalog_and_default_font_are_repository_local() -> None:
    assert PHRASES == REQUIRED_PHRASES
    assert DEFAULT_FONT_PATH == FONT_PATH
    assert FONT_PATH.is_file()
    assert FONT_PATH.stat().st_size < 100_000
    assert (FONT_PATH.parent / "OFL.txt").is_file()

    cmap = TTFont(FONT_PATH).getBestCmap()
    required_codepoints = {ord(character) for phrase in PHRASES for character in phrase.text}
    assert set(cmap) == required_codepoints


def test_generate_emits_valid_asset_for_every_phrase(tmp_path: Path) -> None:
    output_path = tmp_path / "ink_ui_text_assets.c"

    generate(FONT_PATH, output_path)

    source = output_path.read_text(encoding="ascii")
    assert "static const ink_ui_text_asset_t s_assets[]" in source
    assert "ink_ui_text_asset_find" in source
    arrays, records = _parse_assets(source)
    assert [(text, size) for text, size, _, _, _ in records] == [
        (phrase.text, phrase.pixel_size) for phrase in REQUIRED_PHRASES
    ]
    assert len(arrays) == len(records) == len(REQUIRED_PHRASES)

    for _, _, width, height, bitmap_name in records:
        bitmap = arrays[bitmap_name]
        stride = (width + 7) // 8
        assert len(bitmap) == stride * height
        assert any(bitmap)
        if width % 8:
            padding_mask = (1 << (8 - width % 8)) - 1
            assert all(bitmap[(row + 1) * stride - 1] & padding_mask == 0 for row in range(height))


def test_generate_matches_checked_in_source_byte_for_byte(tmp_path: Path) -> None:
    output_path = tmp_path / "ink_ui_text_assets.c"

    generate(FONT_PATH, output_path)

    assert output_path.read_bytes() == CHECKED_IN_SOURCE.read_bytes()


def test_cli_uses_repository_font_by_default(tmp_path: Path) -> None:
    output_path = tmp_path / "ink_ui_text_assets.c"

    subprocess.run(
        [
            sys.executable,
            str(ROOT / "tools" / "generate_ui_text_bitmaps.py"),
            "--output",
            str(output_path),
        ],
        check=True,
        cwd=ROOT,
    )

    assert output_path.read_bytes() == CHECKED_IN_SOURCE.read_bytes()


def test_c_string_encodes_all_utf8_bytes_as_hex_escapes() -> None:
    assert generator._c_string('A\n"\0中') == r"\x41\x0A\x22\x00\xE4\xB8\xAD"


@pytest.mark.parametrize("pixel_size", [0, 256])
def test_generate_rejects_pixel_size_outside_uint8(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, pixel_size: int
) -> None:
    monkeypatch.setattr(generator, "PHRASES", (Phrase("A", pixel_size),))
    monkeypatch.setattr(
        generator, "_render", lambda phrase, font_path: pytest.fail("invalid size was rendered")
    )

    with pytest.raises(ValueError, match="pixel_size"):
        generate(FONT_PATH, tmp_path / "output.c")


@pytest.mark.parametrize(
    ("width", "height", "field"),
    [(0, 1, "width"), (65536, 1, "width"), (1, 0, "height"), (1, 65536, "height")],
)
def test_generate_rejects_dimensions_outside_uint16(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
    width: int,
    height: int,
    field: str,
) -> None:
    monkeypatch.setattr(generator, "PHRASES", (Phrase("A", 16),))
    monkeypatch.setattr(generator, "_render", lambda phrase, font_path: (width, height, b"\x80"))

    with pytest.raises(ValueError, match=field):
        generate(FONT_PATH, tmp_path / "output.c")
