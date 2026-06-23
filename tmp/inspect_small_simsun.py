from fontTools.ttLib import TTFont
font = TTFont(r"D:\Desktop\Small SimSun.ttf")
print('tables', sorted(font.keys()))
print('bitmap tables', [t for t in ('EBDT','EBLC','CBDT','CBLC','sbix','bdat','bloc') if t in font])
if 'EBLC' in font:
    print('EBLC strikes', len(font['EBLC'].strikes))
    for i,s in enumerate(font['EBLC'].strikes[:20]):
        bst = s.bitmapSizeTable
        print('strike', i, 'ppemX', bst.ppemX, 'ppemY', bst.ppemY, 'bitDepth', bst.bitDepth)
