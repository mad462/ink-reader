import freetype
face = freetype.Face(r"D:\Desktop\Small SimSun.ttf")
face.select_size(10)
face.load_char('章', freetype.FT_LOAD_RENDER)
bitmap = face.glyph.bitmap
print('pixel_mode', bitmap.pixel_mode)
print('num_grays', bitmap.num_grays)
print('width', bitmap.width, 'rows', bitmap.rows, 'pitch', bitmap.pitch)
print('buffer_len', len(bitmap.buffer))
print('first16', list(bitmap.buffer[:16]))
