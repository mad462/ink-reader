import freetype
face = freetype.Face(r"D:\Desktop\Small SimSun.ttf")
face.set_char_size(8 << 6, 8 << 6, 150, 150)
face.load_char('章', freetype.FT_LOAD_RENDER)
bitmap = face.glyph.bitmap
print('outline pixel_mode', bitmap.pixel_mode)
print('outline num_grays', bitmap.num_grays)
print('outline width', bitmap.width, 'rows', bitmap.rows, 'pitch', bitmap.pitch)
print('outline buffer_len', len(bitmap.buffer))
print('outline first16', list(bitmap.buffer[:16]))
