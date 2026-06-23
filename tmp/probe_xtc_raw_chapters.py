import struct
from pathlib import Path
p = Path(r"D:\Desktop\电子书相关\海底两万里.xtc")
data = p.read_bytes()
idx_off = struct.unpack_from('<Q', data, 24)[0]
chapter_off = struct.unpack_from('<Q', data, 48)[0]
count = (idx_off - chapter_off) // 96
for i in [0,1,2,20,21]:
    off = chapter_off + i*96
    raw = data[off:off+80]
    zero = raw.find(b'\0')
    start = struct.unpack_from('<H', data, off+80)[0]
    end = struct.unpack_from('<H', data, off+82)[0]
    print('chapter', i+1, 'zero', zero, 'start', start, 'end', end, 'lenraw', len(raw))
    print('hex', raw[:48].hex())
    try:
        print('utf8', raw[:zero if zero!=-1 else 80].decode('utf-8'))
    except Exception as e:
        print('utf8_err', e)
    try:
        print('gbk', raw[:zero if zero!=-1 else 80].decode('gbk'))
    except Exception as e:
        print('gbk_err', e)
    print('---')
