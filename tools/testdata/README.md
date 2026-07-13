# LXGW WenKai UI subset

`LXGWWenKai-ui-subset.ttf` is a modified subset used only to regenerate and test
the fixed phrases in `tools/generate_ui_text_bitmaps.py`. Its primary font names
are changed to `Ink UI Test Font` / `InkUITestFont-Regular` so the modified,
installable font does not use LXGW WenKai's Reserved Font Names.

Source:

- Project: https://github.com/lxgw/LxgwWenKai
- Original file: `LXGWWenKai-Regular.ttf`
- Original version: 1.522 (March 17, 2026)
- Original SHA-256: `39ad71264b588165b469e35e6afb162a378dacd1f95348160240ba9038ac3009`
- Subsetter: fontTools 4.62.1
- Subset SHA-256: `e8efd9147921cbb2ed37cf1d8a52642891665d6c7b3ea60c52d87ead82430541`
- License: SIL Open Font License 1.1; see `OFL.txt`

The subset was produced from the original file with hinting retained, timestamp
recalculation disabled, all layout features enabled, and a text set equal to the
concatenation of every `PHRASES` entry. The subset call is made inside Python so
the phrase text does not pass through a platform shell's encoding:

```python
from fontTools import subset
from tools.generate_ui_text_bitmaps import PHRASES

text = "".join(phrase.text for phrase in PHRASES)
subset.main([
    "LXGWWenKai-Regular.ttf",
    "--output-file=tools/testdata/LXGWWenKai-ui-subset.ttf",
    f"--text={text}",
    "--layout-features=*",
    "--glyph-names",
    "--symbol-cmap",
    "--legacy-cmap",
    "--notdef-glyph",
    "--notdef-outline",
    "--recommended-glyphs",
    "--name-IDs=*",
    "--name-legacy",
    "--name-languages=*",
    "--no-recalc-timestamp",
])
```

After subsetting, fontTools was used with `recalcTimestamp=False` to replace name
IDs 1, 3, 4, 6, 16, and 21 with the non-reserved names above.
