#!/usr/bin/env node

import fs from "fs";
import path from "path";
import { fileURLToPath, pathToFileURL } from "url";

function readEpdFont(epdPath) {
  const data = fs.readFileSync(epdPath);
  const magic = data.readUInt32LE(0);
  const version = data.readUInt16LE(4);
  const is2Bit = data.readUInt8(6);
  const advanceY = data.readUInt8(8);
  const ascender = data.readInt8(9);
  const descender = data.readInt8(10);
  const intervalCount = data.readUInt32LE(12);
  const glyphCount = data.readUInt32LE(16);
  const intervalsOffset = data.readUInt32LE(20);
  const glyphsOffset = data.readUInt32LE(24);
  const bitmapOffset = data.readUInt32LE(28);

  const intervals = [];
  for (let i = 0; i < intervalCount; i++) {
    const off = intervalsOffset + i * 12;
    intervals.push({
      start: data.readUInt32LE(off),
      end: data.readUInt32LE(off + 4),
      offset: data.readUInt32LE(off + 8),
    });
  }

  return {
    data,
    magic,
    version,
    is2Bit,
    advanceY,
    ascender,
    descender,
    intervalCount,
    glyphCount,
    intervalsOffset,
    glyphsOffset,
    bitmapOffset,
    intervals,
  };
}

function findGlyphIndex(intervals, codePoint) {
  for (const interval of intervals) {
    if (interval.start <= codePoint && codePoint <= interval.end) {
      return interval.offset + (codePoint - interval.start);
    }
  }
  return null;
}

function readEpdGlyph(epd, glyphIndex) {
  const off = epd.glyphsOffset + glyphIndex * 16;
  const width = epd.data.readUInt8(off);
  const height = epd.data.readUInt8(off + 1);
  const advanceX = epd.data.readUInt8(off + 2);
  const left = epd.data.readInt16LE(off + 4);
  const top = epd.data.readInt16LE(off + 6);
  const dataLength = epd.data.readUInt32LE(off + 8);
  const dataOffset = epd.data.readUInt32LE(off + 12);
  const bitmap = epd.data.subarray(
    epd.bitmapOffset + dataOffset,
    epd.bitmapOffset + dataOffset + dataLength
  );
  return { width, height, advanceX, left, top, dataLength, dataOffset, bitmap };
}

function renderPacked1Bit(bitmap, width, height) {
  const lines = [];
  const rowBytes = Math.ceil(width / 8);
  for (let y = 0; y < height; y++) {
    let row = "";
    for (let x = 0; x < width; x++) {
      const byte = bitmap[y * rowBytes + (x >> 3)];
      const bit = (byte >> (7 - (x & 7))) & 0x1;
      row += bit ? "#" : ".";
    }
    lines.push(row);
  }
  return lines;
}

function renderPacked2Bit(bitmap, width, height) {
  const lines = [];
  for (let y = 0; y < height; y++) {
    let row = "";
    for (let x = 0; x < width; x++) {
      const pixel = y * width + x;
      const byte = bitmap[pixel >> 2];
      const shift = (3 - (pixel & 3)) * 2;
      const raw = (byte >> shift) & 0x3;
      row += raw ? "#" : ".";
    }
    lines.push(row);
  }
  return lines;
}

function renderGrayBuffer(buffer, width, height, threshold = 128) {
  const lines = [];
  for (let y = 0; y < height; y++) {
    let row = "";
    for (let x = 0; x < width; x++) {
      const value = buffer[y * width + x];
      row += value >= threshold ? "#" : ".";
    }
    lines.push(row);
  }
  return lines;
}

function printLines(title, lines) {
  console.log(title);
  for (const line of lines) {
    console.log(line);
  }
  console.log("");
}

async function loadFreeTypeModule(repoRoot) {
  const jsPath = path.join(
    repoRoot,
    "tools",
    "x4-epdfont-converter",
    "public",
    "wasm",
    "freetype.js"
  );
  const wasmDir = path.dirname(jsPath);
  const tempMjsPath = path.join(wasmDir, "freetype.node-temp.mjs");
  fs.copyFileSync(jsPath, tempMjsPath);
  const mod = await import(pathToFileURL(tempMjsPath).href + `?t=${Date.now()}`);
  return mod.default({
    locateFile: (p) => path.join(wasmDir, p),
  });
}

async function main() {
  const __filename = fileURLToPath(import.meta.url);
  const __dirname = path.dirname(__filename);
  const repoRoot = path.resolve(__dirname, "..");
  const epdPath = process.argv[2];
  const fontPath = process.argv[3];
  const sampleText = process.argv[4] || "飞";

  if (!epdPath || !fontPath) {
    console.error(
      "usage: node tools/diagnose_epdfont_glyph.js <epdfont> <ttf/otf> [text]"
    );
    process.exit(1);
  }

  const epd = readEpdFont(epdPath);
  const ft = await loadFreeTypeModule(repoRoot);
  const fontData = new Uint8Array(fs.readFileSync(fontPath));
  const faces = ft.LoadFontFromBytes(fontData);
  const face = faces[0];
  ft.SetFont(face.family_name, face.style_name);
  ft.SetPixelSize(0, epd.advanceY);

  for (const ch of sampleText) {
    const codePoint = ch.codePointAt(0);
    const glyphIndex = findGlyphIndex(epd.intervals, codePoint);
    if (glyphIndex == null) {
      console.log(`missing in epdfont: ${ch} U+${codePoint.toString(16)}`);
      continue;
    }

    const epdGlyph = readEpdGlyph(epd, glyphIndex);
    const glyphMap = ft.LoadGlyphs(
      [codePoint],
      ft.FT_LOAD_RENDER | ft.FT_LOAD_TARGET_NORMAL
    );
    const liveGlyph = glyphMap.get(codePoint);

    console.log(
      `char=${ch} U+${codePoint.toString(16)} epd={w:${epdGlyph.width},h:${epdGlyph.height},adv:${epdGlyph.advanceX},left:${epdGlyph.left},top:${epdGlyph.top},len:${epdGlyph.dataLength}}`
    );
    console.log(
      `live={w:${liveGlyph.bitmap.width},h:${liveGlyph.bitmap.rows},adv:${Math.round(liveGlyph.advance.x / 64)},left:${liveGlyph.bitmap_left},top:${liveGlyph.bitmap_top},buf:${liveGlyph.bitmap.buffer?.length || 0}}`
    );
    console.log("");

    if (epd.is2Bit) {
      printLines("epdfont packed 2bit", renderPacked2Bit(epdGlyph.bitmap, epdGlyph.width, epdGlyph.height));
    } else {
      printLines("epdfont packed 1bit", renderPacked1Bit(epdGlyph.bitmap, epdGlyph.width, epdGlyph.height));
    }

    if (liveGlyph.bitmap?.buffer) {
      printLines(
        "live glyph buffer threshold128",
        renderGrayBuffer(
          Array.from(liveGlyph.bitmap.buffer),
          liveGlyph.bitmap.width,
          liveGlyph.bitmap.rows,
          128
        )
      );
      printLines(
        "live glyph buffer threshold64",
        renderGrayBuffer(
          Array.from(liveGlyph.bitmap.buffer),
          liveGlyph.bitmap.width,
          liveGlyph.bitmap.rows,
          64
        )
      );
    }
  }
}

main().catch((err) => {
  console.error(err);
  process.exit(1);
});
