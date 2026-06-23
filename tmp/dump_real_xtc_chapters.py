import struct
from pathlib import Path
p = Path(r"D:\Desktop\电子书相关\海底两万里.xtc")
data = p.read_bytes()
magic = data[0:4]
version = struct.unpack_from('<H', data, 4)[0]
page_count = struct.unpack_from('<H', data, 6)[0]
has_meta = data[9]
has_chapters = data[11]
meta_off = struct.unpack_from('<Q', data, 16)[0]
idx_off = struct.unpack_from('<Q', data, 24)[0]
data_off = struct.unpack_from('<Q', data, 32)[0]
chapter_off = struct.unpack_from('<Q', data, 48)[0]
print('magic', magic, 'version', version, 'pages', page_count, 'meta', has_meta, 'chap', has_chapters)
print('offsets', meta_off, idx_off, data_off, chapter_off)
span = idx_off - chapter_off
print('chapter span', span, 'count', span // 96 if span % 96 == 0 else 'nonint')
for i in range(min(30, span // 96)):
    off = chapter_off + i*96
    raw_name = data[off:off+80]
    start = struct.unpack_from('<H', data, off+80)[0]
    end = struct.unpack_from('<H', data, off+82)[0]
    name = raw_name.split(b'\0',1)[0].decode('utf-8', errors='replace')
    print(i+1, start, end, repr(name))
