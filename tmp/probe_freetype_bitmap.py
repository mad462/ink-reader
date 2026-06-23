import freetype
face = freetype.Face(r"D:\Desktop\Small SimSun.ttf")
print('num_fixed_sizes', len(face.available_sizes))
for i,s in enumerate(face.available_sizes[:20]):
    print(i, 'height', s.height, 'width', s.width, 'x_ppem', s.x_ppem, 'y_ppem', s.y_ppem)
print('has select_size', hasattr(face, 'select_size'))
if hasattr(face, 'select_size'):
    face.select_size(10)
    face.load_char('电', freetype.FT_LOAD_RENDER)
    print('glyph bitmap', face.glyph.bitmap.width, face.glyph.bitmap.rows, 'pitch', face.glyph.bitmap.pitch, 'left', face.glyph.bitmap_left, 'top', face.glyph.bitmap_top)
