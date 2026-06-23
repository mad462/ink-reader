#!/usr/bin/env python3
"""Convert simple EPDFont binaries into cpfont v4 regular-style binaries."""

from __future__ import annotations

import argparse
import struct
from dataclasses import dataclass
from pathlib import Path


EPDFONT_MAGIC = 0x46445045  # "EPDF"
EPDFONT_VERSION = 1

CPFONT_MAGIC = b"CPFONT\x00\x00"
CPFONT_VERSION = 4
CPFONT_FLAGS_2BIT = 1


@dataclass(frozen=True)
class EpdFontHeader:
    magic: int
    version: int
    is_2bit: int
    advance_y: int
    ascender: int
    descender: int
    interval_count: int
    glyph_count: int
    intervals_offset: int
    glyphs_offset: int
    bitmap_offset: int


@dataclass(frozen=True)
class Glyph:
    width: int
    height: int
    advance_x: int
    left: int
    top: int
    data_length: int
    data_offset: int


@dataclass(frozen=True)
class Interval:
    start: int
    end: int
    offset: int


def read_epdfont_header(data: bytes) -> EpdFontHeader:
    if len(data) < 32:
        raise ValueError("EPDFont too small for header")

    magic, version, is_2bit, _reserved1, advance_y, asc_i8, desc_i8, _reserved2 = struct.unpack_from(
        "<IHBBBBBB", data, 0
    )
    interval_count, glyph_count, intervals_offset, glyphs_offset, bitmap_offset = struct.unpack_from(
        "<IIIII", data, 12
    )
    if magic != EPDFONT_MAGIC:
        raise ValueError(f"bad EPDFont magic: 0x{magic:08x}")
    if version != EPDFONT_VERSION:
        raise ValueError(f"unsupported EPDFont version: {version}")

    return EpdFontHeader(
        magic=magic,
        version=version,
        is_2bit=is_2bit,
        advance_y=advance_y,
        ascender=struct.unpack("b", bytes([asc_i8]))[0],
        descender=struct.unpack("b", bytes([desc_i8]))[0],
        interval_count=interval_count,
        glyph_count=glyph_count,
        intervals_offset=intervals_offset,
        glyphs_offset=glyphs_offset,
        bitmap_offset=bitmap_offset,
    )


def unpack_1bit_bitmap(bitmap: bytes, width: int, height: int) -> list[int]:
    pixels: list[int] = []
    total_pixels = width * height
    needed_bytes = (total_pixels + 7) // 8
    if len(bitmap) != needed_bytes:
        raise ValueError(
            f"1bit bitmap length mismatch: expected {needed_bytes}, got {len(bitmap)}"
        )
    for pixel in range(total_pixels):
        bit = (bitmap[pixel >> 3] >> (7 - (pixel & 7))) & 0x1
        pixels.append(3 if bit else 0)
    return pixels


def repack_2bit_bitmap(pixels: list[int]) -> bytes:
    out = bytearray((len(pixels) + 3) // 4)
    for i, level in enumerate(pixels):
        byte_index = i >> 2
        shift = (3 - (i & 3)) * 2
        out[byte_index] |= (level & 0x3) << shift
    return bytes(out)


def convert_bitmap_to_cpfont_2bit(
    epd_bitmap: bytes,
    width: int,
    height: int,
    epd_is_2bit: bool,
) -> bytes:
    if epd_is_2bit:
        return epd_bitmap
    pixels = unpack_1bit_bitmap(epd_bitmap, width, height)
    return repack_2bit_bitmap(pixels)


def convert_epdfont_to_cpfont(epd_data: bytes) -> bytes:
    header = read_epdfont_header(epd_data)

    intervals_size = header.interval_count * 12
    glyphs_size = header.glyph_count * 16
    intervals_data = epd_data[header.intervals_offset : header.intervals_offset + intervals_size]
    glyphs_data = epd_data[header.glyphs_offset : header.glyphs_offset + glyphs_size]

    if len(intervals_data) != intervals_size:
        raise ValueError("truncated intervals data")
    if len(glyphs_data) != glyphs_size:
        raise ValueError("truncated glyphs data")

    intervals: list[Interval] = []
    for i in range(header.interval_count):
        start, end, offset = struct.unpack_from("<III", intervals_data, i * 12)
        intervals.append(Interval(start=start, end=end, offset=offset))

    glyph_records: list[Glyph] = []
    cpfont_glyphs = bytearray()
    cpfont_bitmaps = bytearray()
    epd_bitmap_data = epd_data[header.bitmap_offset:]

    for i in range(header.glyph_count):
        off = i * 16
        width, height, advance_x_u8, _reserved, left, top, data_length, data_offset = struct.unpack_from(
            "<4B2h2I", glyphs_data, off
        )
        glyph = Glyph(
            width=width,
            height=height,
            advance_x=advance_x_u8 << 4,
            left=left,
            top=top,
            data_length=data_length,
            data_offset=data_offset,
        )
        glyph_records.append(glyph)

        epd_bitmap = epd_bitmap_data[data_offset : data_offset + data_length]
        if len(epd_bitmap) != data_length:
            raise ValueError(f"truncated bitmap data for glyph {i}")

        cpfont_bitmap = convert_bitmap_to_cpfont_2bit(
            epd_bitmap,
            glyph.width,
            glyph.height,
            bool(header.is_2bit),
        )
        cpfont_bitmap_offset = len(cpfont_bitmaps)
        cpfont_bitmaps.extend(cpfont_bitmap)

        cpfont_glyphs.extend(
            struct.pack(
                "<BBHhhH2xI",
                glyph.width,
                glyph.height,
                glyph.advance_x,
                glyph.left,
                glyph.top,
                len(cpfont_bitmap),
                cpfont_bitmap_offset,
            )
        )

    cpfont_header = struct.pack(
        "<8sHHB19s",
        CPFONT_MAGIC,
        CPFONT_VERSION,
        CPFONT_FLAGS_2BIT,
        1,
        bytes(19),
    )

    style_toc = struct.pack(
        "<B3xIIBhhHHBBBI4x",
        0,
        header.interval_count,
        header.glyph_count,
        header.advance_y & 0xFF,
        header.ascender,
        header.descender,
        0,
        0,
        0,
        0,
        0,
        64,
    )

    cpfont_intervals = bytearray()
    for interval in intervals:
        cpfont_intervals.extend(struct.pack("<III", interval.start, interval.end, interval.offset))

    return b"".join([cpfont_header, style_toc, cpfont_intervals, cpfont_glyphs, cpfont_bitmaps])


def main() -> int:
    parser = argparse.ArgumentParser(description="Convert EPDFont to cpfont v4.")
    parser.add_argument("input", help="Input .epdfont path")
    parser.add_argument("-o", "--output", required=True, help="Output .cpfont path")
    args = parser.parse_args()

    input_path = Path(args.input)
    output_path = Path(args.output)

    epd_data = input_path.read_bytes()
    cpfont_data = convert_epdfont_to_cpfont(epd_data)

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(cpfont_data)

    print(f"converted {input_path} -> {output_path}")
    print(f"size={len(cpfont_data)} bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
