#!/usr/bin/env python3
"""Convert an embedded bitmap strike from a TTF into cpfont v4.

This path bypasses FreeType rasterization and preserves the exact pixels stored
in EBDT/EBLC strike data.
"""

from __future__ import annotations

import argparse
import struct
from dataclasses import dataclass
from pathlib import Path

from fontTools.ttLib import TTFont
from fontTools.ttLib.tables.E_B_D_T_ import _data2binary


CPFONT_MAGIC = b"CPFONT\x00\x00"
CPFONT_VERSION = 4
CPFONT_FLAGS_2BIT = 1


@dataclass(frozen=True)
class GlyphRecord:
    codepoint: int
    width: int
    height: int
    advance_x_26_6: int
    left: int
    top: int
    bitmap_2bit: bytes


@dataclass(frozen=True)
class StrikeGlyphContext:
    glyph: object
    metrics: object


def pack_cpfont_header(style_count: int) -> bytes:
    return struct.pack(
        "<8sHHB19s",
        CPFONT_MAGIC,
        CPFONT_VERSION,
        CPFONT_FLAGS_2BIT,
        style_count,
        bytes(19),
    )


def pack_style_toc(
    interval_count: int,
    glyph_count: int,
    advance_y: int,
    ascender: int,
    descender: int,
    data_offset: int,
) -> bytes:
    return struct.pack(
        "<B3xIIBhhHHBBBI4x",
        0,
        interval_count,
        glyph_count,
        advance_y & 0xFF,
        ascender,
        descender,
        0,
        0,
        0,
        0,
        0,
        data_offset,
    )


def find_strike_index(font: TTFont, pixel_size: int) -> int:
    strikes = font["EBLC"].strikes
    for i, strike in enumerate(strikes):
        bst = strike.bitmapSizeTable
        if int(bst.ppemX) == pixel_size and int(bst.ppemY) == pixel_size:
            return i
    available = ", ".join(
        f"{int(s.bitmapSizeTable.ppemX)}x{int(s.bitmapSizeTable.ppemY)}"
        for s in strikes
    )
    raise ValueError(f"no embedded bitmap strike for {pixel_size}px; available: {available}")


def row_to_bits(row_data: bytes, width: int) -> list[int]:
    return [1 if bit == "1" else 0 for bit in _data2binary(row_data, width)]


def bits_to_packed_2bit(bits: list[int]) -> bytes:
    out = bytearray((len(bits) + 3) // 4)
    for i, bit in enumerate(bits):
        if bit == 0:
            continue
        shift = (3 - (i & 3)) * 2
        out[i >> 2] |= 0x3 << shift
    return bytes(out)


def metric_value(metrics: object, primary: str, fallback: str | None = None) -> int:
    value = getattr(metrics, primary, None)
    if value is None and fallback is not None:
        value = getattr(metrics, fallback, None)
    if value is None:
        return 0
    return int(value)


def build_strike_contexts(font: TTFont, strike_index: int) -> dict[str, StrikeGlyphContext]:
    strike = font["EBLC"].strikes[strike_index]
    strike_glyphs = font["EBDT"].strikeData[strike_index]
    contexts: dict[str, StrikeGlyphContext] = {}

    for subtable in strike.indexSubTables:
        subtable_metrics = getattr(subtable, "metrics", None)
        for glyph_name in subtable.names:
            glyph = strike_glyphs[glyph_name]
            glyph_metrics = getattr(glyph, "metrics", None)
            metrics = glyph_metrics if glyph_metrics is not None else subtable_metrics
            if metrics is None:
                continue
            contexts[glyph_name] = StrikeGlyphContext(glyph=glyph, metrics=metrics)

    return contexts


def build_glyph_records(font: TTFont, strike_index: int) -> list[GlyphRecord]:
    cmap = font.getBestCmap()
    strike_contexts = build_strike_contexts(font, strike_index)
    records: list[GlyphRecord] = []

    for codepoint, glyph_name in sorted(cmap.items()):
        context = strike_contexts.get(glyph_name)
        if context is None:
            continue
        glyph = context.glyph
        metrics = context.metrics
        width = metric_value(metrics, "width")
        height = metric_value(metrics, "height")
        if width <= 0 or height <= 0:
            continue
        rows_bits: list[int] = []
        for y in range(height):
            row = glyph.getRow(y, bitDepth=1, metrics=metrics, reverseBytes=True)
            rows_bits.extend(row_to_bits(row, width))
        records.append(
            GlyphRecord(
                codepoint=codepoint,
                width=width,
                height=height,
                advance_x_26_6=metric_value(metrics, "Advance", "horiAdvance") << 4,
                left=metric_value(metrics, "BearingX", "horiBearingX"),
                top=metric_value(metrics, "BearingY", "horiBearingY"),
                bitmap_2bit=bits_to_packed_2bit(rows_bits),
            )
        )

    if not records:
        raise ValueError("no cmap-mapped glyphs found in selected embedded bitmap strike")
    return records


def build_intervals(records: list[GlyphRecord]) -> list[tuple[int, int, int]]:
    intervals: list[tuple[int, int, int]] = []
    start_cp = records[0].codepoint
    end_cp = start_cp
    start_index = 0

    for i in range(1, len(records)):
        cp = records[i].codepoint
        if cp == end_cp + 1:
            end_cp = cp
            continue
        intervals.append((start_cp, end_cp, start_index))
        start_cp = cp
        end_cp = cp
        start_index = i

    intervals.append((start_cp, end_cp, start_index))
    return intervals


def strike_vertical_metrics(records: list[GlyphRecord], fallback_pixel_size: int) -> tuple[int, int, int]:
    if not records:
        return fallback_pixel_size, fallback_pixel_size, 0

    ascender = max(record.top for record in records)
    descender = min(record.top - record.height for record in records)
    advance_y = ascender - descender
    if advance_y <= 0:
        advance_y = fallback_pixel_size
    return advance_y, ascender, descender


def convert_embedded_bitmap_ttf_to_cpfont(input_path: Path, pixel_size: int, output_path: Path) -> None:
    font = TTFont(input_path)
    strike_index = find_strike_index(font, pixel_size)
    records = build_glyph_records(font, strike_index)
    intervals = build_intervals(records)
    advance_y, ascender, descender = strike_vertical_metrics(records, pixel_size)

    header = pack_cpfont_header(1)
    data_offset = len(header) + 32
    toc = pack_style_toc(
        interval_count=len(intervals),
        glyph_count=len(records),
        advance_y=advance_y,
        ascender=ascender,
        descender=descender,
        data_offset=data_offset,
    )

    interval_blob = bytearray()
    for first, last, offset in intervals:
        interval_blob.extend(struct.pack("<III", first, last, offset))

    glyph_blob = bytearray()
    bitmap_blob = bytearray()
    bitmap_offset = 0
    for record in records:
        glyph_blob.extend(
            struct.pack(
                "<BBHhhH2xI",
                record.width,
                record.height,
                record.advance_x_26_6,
                record.left,
                record.top,
                len(record.bitmap_2bit),
                bitmap_offset,
            )
        )
        bitmap_blob.extend(record.bitmap_2bit)
        bitmap_offset += len(record.bitmap_2bit)

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(bytes(header + toc + interval_blob + glyph_blob + bitmap_blob))


def main() -> int:
    parser = argparse.ArgumentParser(description="Convert a TTF embedded bitmap strike into cpfont v4.")
    parser.add_argument("input", help="Input TTF path")
    parser.add_argument("--pixel-size", type=int, required=True, help="Embedded bitmap strike size, e.g. 16")
    parser.add_argument("--output", required=True, help="Output cpfont path")
    args = parser.parse_args()

    input_path = Path(args.input)
    output_path = Path(args.output)
    convert_embedded_bitmap_ttf_to_cpfont(input_path, args.pixel_size, output_path)
    print(f"converted {input_path} ({args.pixel_size}px strike) -> {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
