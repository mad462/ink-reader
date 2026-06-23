import sys
import fontconvert_sdcard as base

orig = base.rasterize_font_style

def wrapped(fontfile, size, intervals, style_id=0, force_autohint=False, fallback_fontfile=None):
    return orig(fontfile, size, intervals, style_id=style_id, force_autohint=force_autohint, fallback_fontfile=fallback_fontfile, prefer_embedded_bitmaps=True)

base.rasterize_font_style = wrapped
base.main()
