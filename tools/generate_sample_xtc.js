const fs = require("fs");
const path = require("path");

const WIDTH = 480;
const HEIGHT = 800;
const ROW_BYTES = WIDTH / 8;
const BITMAP_SIZE = ROW_BYTES * HEIGHT;
const XTG_HEADER_SIZE = 22;
const XTC_HEADER_SIZE = 56;
const METADATA_SIZE = 256;
const INDEX_ENTRY_SIZE = 16;

function setPixel(bitmap, x, y, black) {
  if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT) return;
  const index = y * ROW_BYTES + (x >> 3);
  const mask = 0x80 >> (x & 7);
  if (black) {
    bitmap[index] &= ~mask;
  } else {
    bitmap[index] |= mask;
  }
}

function fillRect(bitmap, x, y, w, h, black) {
  for (let yy = y; yy < y + h; yy++) {
    for (let xx = x; xx < x + w; xx++) {
      setPixel(bitmap, xx, yy, black);
    }
  }
}

function framePage(bitmap) {
  fillRect(bitmap, 0, 0, WIDTH, 8, true);
  fillRect(bitmap, 0, HEIGHT - 8, WIDTH, 8, true);
  fillRect(bitmap, 0, 0, 8, HEIGHT, true);
  fillRect(bitmap, WIDTH - 8, 0, 8, HEIGHT, true);
}

function makeBitmap(drawFn) {
  const bitmap = Buffer.alloc(BITMAP_SIZE, 0xff);
  framePage(bitmap);
  drawFn(bitmap);
  return bitmap;
}

function encodeXtg(bitmap) {
  const out = Buffer.alloc(XTG_HEADER_SIZE + BITMAP_SIZE, 0x00);
  out.write("XTG\0", 0, "binary");
  out.writeUInt16LE(WIDTH, 4);
  out.writeUInt16LE(HEIGHT, 6);
  out.writeUInt8(0, 8);
  out.writeUInt8(0, 9);
  out.writeUInt32LE(BITMAP_SIZE, 10);
  bitmap.copy(out, XTG_HEADER_SIZE);
  return out;
}

function buildPageOne() {
  return makeBitmap((bitmap) => {
    fillRect(bitmap, 50, 90, 380, 60, true);
    fillRect(bitmap, 70, 200, 120, 420, true);
    fillRect(bitmap, 220, 200, 180, 80, true);
    fillRect(bitmap, 220, 330, 180, 80, true);
    fillRect(bitmap, 220, 460, 180, 80, true);
    fillRect(bitmap, 220, 590, 140, 80, true);
  });
}

function buildPageTwo() {
  return makeBitmap((bitmap) => {
    fillRect(bitmap, 40, 90, 400, 60, true);
    for (let i = 0; i < 6; i++) {
      fillRect(bitmap, 70 + i * 55, 220, 28, 420, true);
    }
    fillRect(bitmap, 80, 650, 300, 70, true);
  });
}

function buildPageThree() {
  return makeBitmap((bitmap) => {
    fillRect(bitmap, 60, 90, 360, 60, true);
    for (let y = 220; y < 700; y += 70) {
      fillRect(bitmap, 80, y, 320, 24, true);
    }
    for (let x = 80; x < 400; x += 64) {
      fillRect(bitmap, x, 220, 24, 420, true);
    }
  });
}

function buildXtc(pages, title, author) {
  const pageCount = pages.length;
  const indexSize = pageCount * INDEX_ENTRY_SIZE;
  const metadataOffset = XTC_HEADER_SIZE;
  const indexOffset = metadataOffset + METADATA_SIZE;
  const dataOffset = indexOffset + indexSize;
  const totalSize = dataOffset + pages.reduce((sum, page) => sum + page.length, 0);
  const out = Buffer.alloc(totalSize, 0x00);

  out.write("XTC\0", 0, "binary");
  out.writeUInt16LE(1, 4);
  out.writeUInt16LE(pageCount, 6);
  out.writeUInt8(0, 8);
  out.writeUInt8(1, 9);
  out.writeUInt8(0, 10);
  out.writeUInt8(0, 11);
  out.writeUInt32LE(1, 12);
  out.writeBigUInt64LE(BigInt(metadataOffset), 16);
  out.writeBigUInt64LE(BigInt(indexOffset), 24);
  out.writeBigUInt64LE(BigInt(dataOffset), 32);
  out.writeBigUInt64LE(0n, 40);
  out.writeBigUInt64LE(0n, 48);

  Buffer.from(title, "utf8").copy(out, metadataOffset, 0, 127);
  Buffer.from(author, "utf8").copy(out, metadataOffset + 128, 0, 63);
  out.writeUInt32LE(Math.floor(Date.now() / 1000), metadataOffset + 192);
  out.writeUInt16LE(0xffff, metadataOffset + 196);
  out.writeUInt16LE(0, metadataOffset + 198);

  let pageWriteOffset = dataOffset;
  for (let i = 0; i < pages.length; i++) {
    const indexPos = indexOffset + i * INDEX_ENTRY_SIZE;
    out.writeBigUInt64LE(BigInt(pageWriteOffset), indexPos);
    out.writeUInt32LE(pages[i].length, indexPos + 8);
    out.writeUInt16LE(WIDTH, indexPos + 12);
    out.writeUInt16LE(HEIGHT, indexPos + 14);
    pages[i].copy(out, pageWriteOffset);
    pageWriteOffset += pages[i].length;
  }

  return out;
}

function main() {
  const outputArg = process.argv[2];
  const outputPath = outputArg
    ? path.resolve(outputArg)
    : path.resolve(__dirname, "..", "TF卡内容", "sample.xtc");

  const xtgPages = [
    encodeXtg(buildPageOne()),
    encodeXtg(buildPageTwo()),
    encodeXtg(buildPageThree()),
  ];
  const xtc = buildXtc(xtgPages, "Sample XTC", "Codex");

  fs.mkdirSync(path.dirname(outputPath), { recursive: true });
  fs.writeFileSync(outputPath, xtc);
  console.log(`wrote ${outputPath} (${xtc.length} bytes)`);
}

main();
