import re
from pathlib import Path

from tools.generate_ui_text_bitmaps import PHRASES, Phrase, generate


FONT_PATH = Path(
    r"D:\FUCKIDF\ink-reader\tools\ebook-canvas-studio\fonts\LXGWWenKai-Regular.ttf"
)
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


def test_phrase_catalog_is_complete() -> None:
    assert PHRASES == REQUIRED_PHRASES


def test_generate_emits_asset_table_with_nonempty_bitmaps(tmp_path: Path) -> None:
    output_path = tmp_path / "ink_ui_text_assets.c"

    generate(FONT_PATH, output_path)

    source = output_path.read_text(encoding="utf-8")
    assert "static const ink_ui_text_asset_t s_assets[]" in source
    assert "ink_ui_text_asset_find" in source
    for phrase in REQUIRED_PHRASES:
        assert phrase.text in source

    bitmap_bytes = [int(value, 16) for value in re.findall(r"0x([0-9A-F]{2})", source)]
    assert bitmap_bytes
    assert any(bitmap_bytes)
