#!/usr/bin/env python3
"""Render .cpfont files to a local PNG preview sheet.

Examples:
    python tools/preview_cpfont.py \
      --font "TF卡内容/fonts/SmallSimSun_16.cpfont:2:old_outline" \
      --font "TF卡内容/fonts/SmallSimSunBitmap_16.cpfont:1:new_bitmap" \
      --text "65/1679 3%" \
      --text "飞翔的礁石" \
      --text "二十托雷斯海峡" \
      --text "21#" \
      --output tmp/cpfont_footer_compare.png
"""

from __future__ import annotations

import argparse
import struct
from dataclasses import dataclass
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


CPFONT_MAGIC = b"CPFONT\x00\x00"
CPFONT_VERSION = 4


@dataclass(frozen=True)
class Glyph:
    width: int
    height: int
    advance_x: int
    left: int
    top: int
    data_length: int
    data_offset: int


class CpFont:
    def __init__(self, path: Path):
        self.path = path
        self._fp = path.open("rb")
        self._load()

    def _load(self) -> None:
        header = self._fp.read(32)
        if len(header) != 32 or header[:8] != CPFONT_MAGIC:
            raise RuntimeError(f"invalid cpfont magic: {self.path}")

        version = struct.unpack_from("<H", header, 8)[0]
        if version != CPFONT_VERSION:
            raise RuntimeError(
                f"unsupported cpfont version {version}, expected {CPFONT_VERSION}: {self.path}"
            )

        style_count = header[12]
        if style_count <= 0:
            raise RuntimeError(f"cpfont has no styles: {self.path}")

        regular_toc = None
        for _ in range(style_count):
            toc = self._fp.read(32)
            if len(toc) != 32:
                raise RuntimeError(f"truncated style toc: {self.path}")
            if toc[0] == 0:
                regular_toc = toc
                break

        if regular_toc is None:
            raise RuntimeError(f"regular style missing: {self.path}")

        self.interval_count = struct.unpack_from("<I", regular_toc, 4)[0]
        self.glyph_count = struct.unpack_from("<I", regular_toc, 8)[0]
        self.advance_y = regular_toc[12]
        self.ascender = struct.unpack_from("<h", regular_toc, 13)[0]
        self.descender = struct.unpack_from("<h", regular_toc, 15)[0]
        kern_left_entries = struct.unpack_from("<H", regular_toc, 17)[0]
        kern_right_entries = struct.unpack_from("<H", regular_toc, 19)[0]
        kern_left_class_count = regular_toc[21]
        kern_right_class_count = regular_toc[22]
        ligature_count = regular_toc[23]
        data_offset = struct.unpack_from("<I", regular_toc, 24)[0]

        self._fp.seek(data_offset)
        self.intervals = []
        for _ in range(self.interval_count):
            buf = self._fp.read(12)
            if len(buf) != 12:
                raise RuntimeError(f"truncated intervals: {self.path}")
            self.intervals.append(struct.unpack("<III", buf))

        self.glyphs_file_offset = data_offset + self.interval_count * 12
        kern_left_file_offset = self.glyphs_file_offset + self.glyph_count * 16
        kern_right_file_offset = kern_left_file_offset + kern_left_entries * 3
        kern_matrix_file_offset = kern_right_file_offset + kern_right_entries * 3
        ligature_file_offset = (
            kern_matrix_file_offset + kern_left_class_count * kern_right_class_count
        )
        self.bitmap_file_offset = ligature_file_offset + ligature_count * 8

    def find_glyph_index(self, codepoint: int) -> int | None:
        left = 0
        right = len(self.intervals) - 1
        while left <= right:
            mid = left + (right - left) // 2
            first, last, offset = self.intervals[mid]
            if codepoint < first:
                right = mid - 1
            elif codepoint > last:
                left = mid + 1
            else:
                return offset + (codepoint - first)
        return None

    def read_glyph(self, glyph_index: int) -> tuple[Glyph, bytes]:
        self._fp.seek(self.glyphs_file_offset + glyph_index * 16)
        buf = self._fp.read(16)
        if len(buf) != 16:
            raise RuntimeError(f"truncated glyph {glyph_index}: {self.path}")

        glyph = Glyph(
            width=buf[0],
            height=buf[1],
            advance_x=struct.unpack_from("<H", buf, 2)[0],
            left=struct.unpack_from("<h", buf, 4)[0],
            top=struct.unpack_from("<h", buf, 6)[0],
            data_length=struct.unpack_from("<H", buf, 8)[0],
            data_offset=struct.unpack_from("<I", buf, 12)[0],
        )

        bitmap = b""
        if glyph.data_length:
            self._fp.seek(self.bitmap_file_offset + glyph.data_offset)
            bitmap = self._fp.read(glyph.data_length)
            if len(bitmap) != glyph.data_length:
                raise RuntimeError(f"truncated glyph bitmap {glyph_index}: {self.path}")

        return glyph, bitmap

    def draw_text(self, text: str, scale_divisor: int = 1) -> Image.Image:
        if scale_divisor <= 0:
            raise ValueError("scale_divisor must be >= 1")

        baseline_y = self.ascender // scale_divisor
        cursor_x = 0
        pixels: list[tuple[int, int]] = []
        max_x = 1
        max_y = max(1, baseline_y + 1)

        for char in text:
            if char in "\r\n":
                break

            glyph_index = self.find_glyph_index(ord(char))
            if glyph_index is None:
                fallback_advance = max(1, (self.advance_y // scale_divisor) // 2)
                cursor_x += fallback_advance
                max_x = max(max_x, cursor_x)
                continue

            glyph, bitmap = self.read_glyph(glyph_index)
            if bitmap:
                if scale_divisor == 1:
                    self._draw_glyph_2bit(
                        glyph=glyph,
                        bitmap=bitmap,
                        cursor_x=cursor_x,
                        baseline_y=baseline_y,
                        pixels=pixels,
                    )
                else:
                    self._draw_glyph_scaled(
                        glyph=glyph,
                        bitmap=bitmap,
                        cursor_x=cursor_x,
                        baseline_y=baseline_y,
                        scale_divisor=scale_divisor,
                        pixels=pixels,
                    )

                for px, py in pixels[-(glyph.width * glyph.height) :]:
                    max_x = max(max_x, px + 1)
                    max_y = max(max_y, py + 1)

            cursor_x += ((glyph.advance_x + 8) >> 4) // scale_divisor
            max_x = max(max_x, cursor_x)

        image = Image.new("1", (max_x + 2, max_y + 2), 1)
        for px, py in pixels:
            if 0 <= px < image.width and 0 <= py < image.height:
                image.putpixel((px, py), 0)
        return image

    @staticmethod
    def _draw_glyph_2bit(
        glyph: Glyph,
        bitmap: bytes,
        cursor_x: int,
        baseline_y: int,
        pixels: list[tuple[int, int]],
    ) -> None:
        base_x = cursor_x + glyph.left
        base_y = baseline_y - glyph.top
        pixel_count = glyph.width * glyph.height
        for pixel in range(pixel_count):
            byte = bitmap[pixel >> 2]
            shift = (3 - (pixel & 3)) * 2
            raw = (byte >> shift) & 0x3
            if raw == 0:
                continue
            gx = pixel % glyph.width
            gy = pixel // glyph.width
            pixels.append((base_x + gx, base_y + gy))

    @staticmethod
    def _draw_glyph_scaled(
        glyph: Glyph,
        bitmap: bytes,
        cursor_x: int,
        baseline_y: int,
        scale_divisor: int,
        pixels: list[tuple[int, int]],
    ) -> None:
        pixel_count = glyph.width * glyph.height
        mono = bytearray((pixel_count + 7) // 8)
        for pixel in range(pixel_count):
            byte = bitmap[pixel >> 2]
            shift = (3 - (pixel & 3)) * 2
            raw = (byte >> shift) & 0x3
            if raw > 0:
                mono[pixel >> 3] |= 0x80 >> (pixel & 7)

        scaled_left = glyph.left // scale_divisor
        scaled_top = glyph.top // scale_divisor
        base_x = cursor_x + scaled_left
        base_y = baseline_y - scaled_top

        for pixel in range(pixel_count):
            if (mono[pixel >> 3] & (0x80 >> (pixel & 7))) == 0:
                continue
            gx = pixel % glyph.width
            gy = pixel // glyph.width
            if (gx % scale_divisor) != 0 or (gy % scale_divisor) != 0:
                continue
            pixels.append((base_x + gx // scale_divisor, base_y + gy // scale_divisor))


@dataclass(frozen=True)
class PreviewFontSpec:
    label: str
    path: Path
    scale_divisor: int


def parse_font_spec(raw: str) -> PreviewFontSpec:
    path_text = raw
    scale_divisor = 1
    label = None

    tail_parts = raw.rsplit(":", 2)
    if len(tail_parts) == 3 and tail_parts[1].isdigit():
        path_text = tail_parts[0]
        scale_divisor = int(tail_parts[1])
        label = tail_parts[2] or None
    else:
        tail_parts = raw.rsplit(":", 1)
        if len(tail_parts) == 2 and tail_parts[1].isdigit():
            path_text = tail_parts[0]
            scale_divisor = int(tail_parts[1])

    path = Path(path_text)
    if label is None:
        label = path.stem
    return PreviewFontSpec(label=label, path=path, scale_divisor=scale_divisor)


def build_sheet(font_specs: list[PreviewFontSpec], texts: list[str], output: Path) -> None:
    label_font = ImageFont.load_default()
    loaded_fonts = [(spec, CpFont(spec.path)) for spec in font_specs]

    row_height = 118
    header_height = 44
    group_gap = 22
    sheet_width = 1800
    sheet_height = 24 + sum(header_height + len(texts) * row_height + group_gap for _ in loaded_fonts)

    canvas = Image.new("L", (sheet_width, sheet_height), 255)
    draw = ImageDraw.Draw(canvas)
    cursor_y = 20
    draw.text((20, cursor_y), "cpfont preview (1x and 4x nearest zoom)", fill=0, font=label_font)
    cursor_y += 26

    for spec, font in loaded_fonts:
        draw.text(
            (20, cursor_y),
            (
                f"{spec.label} | path={spec.path.name} | "
                f"advY={font.advance_y} asc={font.ascender} desc={font.descender} "
                f"scale={spec.scale_divisor}"
            ),
            fill=0,
            font=label_font,
        )
        cursor_y += 20

        for text in texts:
            image_1x = font.draw_text(text, scale_divisor=spec.scale_divisor)
            zoom_4x = image_1x.resize(
                (image_1x.width * 4, image_1x.height * 4),
                resample=Image.Resampling.NEAREST,
            )
            draw.text((20, cursor_y + 8), text, fill=0, font=label_font)
            canvas.paste(image_1x.convert("L"), (280, cursor_y + 8))
            canvas.paste(zoom_4x.convert("L"), (520, cursor_y + 8))
            cursor_y += row_height

        cursor_y += group_gap

    output.parent.mkdir(parents=True, exist_ok=True)
    canvas.save(output)


def main() -> int:
    parser = argparse.ArgumentParser(description="Preview cpfont files as PNG.")
    parser.add_argument(
        "--font",
        action="append",
        required=True,
        help="FONT spec: path[:scale_divisor[:label]]",
    )
    parser.add_argument(
        "--text",
        action="append",
        required=True,
        help="Text sample to render. Repeat for multiple lines.",
    )
    parser.add_argument("--output", required=True, help="Output PNG path.")
    args = parser.parse_args()

    font_specs = [parse_font_spec(raw) for raw in args.font]
    build_sheet(font_specs, args.text, Path(args.output))

    for spec in font_specs:
        font = CpFont(spec.path)
        print(
            f"{spec.label}: path={spec.path} advance_y={font.advance_y} "
            f"ascender={font.ascender} descender={font.descender} scale={spec.scale_divisor}"
        )
    print(f"saved preview: {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
