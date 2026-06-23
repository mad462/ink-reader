from __future__ import annotations

import struct
import subprocess
import sys
from pathlib import Path

from fontTools.ttLib import TTFont
from fontTools.ttLib.tables.E_B_D_T_ import _data2binary


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "tools" / "convert_embedded_bitmap_ttf_to_cpfont.py"
INPUT_FILE = Path(r"D:\Desktop\Small SimSun.ttf")
TEST_CODEPOINT = 0x98DE  # 飞


def read_u16(buf: bytes, offset: int) -> int:
    return struct.unpack_from("<H", buf, offset)[0]


def read_i16(buf: bytes, offset: int) -> int:
    return struct.unpack_from("<h", buf, offset)[0]


def read_u32(buf: bytes, offset: int) -> int:
    return struct.unpack_from("<I", buf, offset)[0]


def cpfont_rows_for_codepoint(path: Path, codepoint: int) -> list[str]:
    data = path.read_bytes()
    toc = data[32:64]
    interval_count = read_u32(toc, 4)
    glyph_count = read_u32(toc, 8)
    data_offset = read_u32(toc, 24)

    glyph_index = None
    for i in range(interval_count):
        off = data_offset + i * 12
        first = read_u32(data, off)
        last = read_u32(data, off + 4)
        base = read_u32(data, off + 8)
        if first <= codepoint <= last:
            glyph_index = base + (codepoint - first)
            break

    assert glyph_index is not None, f"glyph U+{codepoint:04X} missing in cpfont"

    glyph_off = data_offset + interval_count * 12 + glyph_index * 16
    width = data[glyph_off]
    height = data[glyph_off + 1]
    data_length = read_u16(data, glyph_off + 8)
    bitmap_offset = read_u32(data, glyph_off + 12)
    bitmap_base = data_offset + interval_count * 12 + glyph_count * 16
    bitmap = data[bitmap_base + bitmap_offset : bitmap_base + bitmap_offset + data_length]

    rows: list[str] = []
    for y in range(height):
        row_chars: list[str] = []
        for x in range(width):
            pixel = y * width + x
            packed = bitmap[pixel >> 2]
            raw = (packed >> ((3 - (pixel & 3)) * 2)) & 0x3
            row_chars.append("#" if raw > 0 else ".")
        rows.append("".join(row_chars))
    return rows


def embedded_rows_for_codepoint(path: Path, pixel_size: int, codepoint: int) -> list[str]:
    font = TTFont(path)
    strike_index = pixel_size - 6
    glyph_name = font.getBestCmap()[codepoint]
    glyph = font["EBDT"].strikeData[strike_index][glyph_name]
    metrics = glyph.metrics
    rows: list[str] = []
    for y in range(metrics.height):
        bits = _data2binary(
            glyph.getRow(y, bitDepth=1, metrics=metrics, reverseBytes=True),
            metrics.width,
        )
        rows.append("".join("#" if bit == "1" else "." for bit in bits))
    return rows


def test_convert_embedded_bitmap_ttf_to_cpfont_preserves_glyph_pixels(tmp_path: Path) -> None:
    assert INPUT_FILE.exists(), f"missing input TTF: {INPUT_FILE}"
    output_file = tmp_path / "SmallSimSunEmbedded_16.cpfont"

    result = subprocess.run(
        [
            sys.executable,
            str(SCRIPT),
            str(INPUT_FILE),
            "--pixel-size",
            "16",
            "--output",
            str(output_file),
        ],
        cwd=ROOT,
        capture_output=True,
        text=True,
    )

    assert result.returncode == 0, result.stderr or result.stdout
    assert output_file.exists(), "converter did not produce cpfont output"

    cpfont_rows = cpfont_rows_for_codepoint(output_file, TEST_CODEPOINT)
    embedded_rows = embedded_rows_for_codepoint(INPUT_FILE, 16, TEST_CODEPOINT)

    assert cpfont_rows == embedded_rows
