from __future__ import annotations

import struct
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "tools" / "convert_epdfont_to_cpfont.py"
INPUT_FILE = Path(r"D:\Desktop\SimSun_16.epdfont")
OUTPUT_FILE = ROOT / "tmp" / "SimSun_16_from_epdfont.cpfont"


def read_u16(buf: bytes, offset: int) -> int:
    return struct.unpack_from("<H", buf, offset)[0]


def read_i16(buf: bytes, offset: int) -> int:
    return struct.unpack_from("<h", buf, offset)[0]


def read_u32(buf: bytes, offset: int) -> int:
    return struct.unpack_from("<I", buf, offset)[0]


def test_convert_epdfont_to_cpfont_header_roundtrip() -> None:
    assert INPUT_FILE.exists(), f"missing input epdfont: {INPUT_FILE}"
    if OUTPUT_FILE.exists():
        OUTPUT_FILE.unlink()

    result = subprocess.run(
        [sys.executable, str(SCRIPT), str(INPUT_FILE), "-o", str(OUTPUT_FILE)],
        cwd=ROOT,
        capture_output=True,
        text=True,
    )

    assert result.returncode == 0, result.stderr or result.stdout
    assert OUTPUT_FILE.exists(), "converter did not produce cpfont output"

    data = OUTPUT_FILE.read_bytes()
    assert data[:8] == b"CPFONT\x00\x00"
    assert read_u16(data, 8) == 4
    assert data[12] == 1  # style_count

    toc = data[32:64]
    assert toc[0] == 0  # regular style
    interval_count = read_u32(toc, 4)
    glyph_count = read_u32(toc, 8)
    advance_y = toc[12]
    ascender = read_i16(toc, 13)
    descender = read_i16(toc, 15)
    data_offset = read_u32(toc, 24)

    assert interval_count > 0
    assert glyph_count > 0
    assert advance_y == 16
    assert ascender > 0
    assert descender <= 0
    assert data_offset == 64

    first_interval = data[data_offset : data_offset + 12]
    first_first = read_u32(first_interval, 0)
    first_last = read_u32(first_interval, 4)
    first_offset = read_u32(first_interval, 8)
    assert first_first <= first_last
    assert first_offset == 0
