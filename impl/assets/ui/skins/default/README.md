# Default UI skin

`atlas.nwb` is the `ui_skin` asset at `engine/ui/skins/default/atlas`. It names regions in one texture asset,
`engine/ui/skins/default/texture`, described by `texture.nwb` and its UASTC `texture.tex` payload.
The generator can recreate `source.png` artwork as straight-alpha sRGB color; that image is not checked in or required at runtime.

Atlas rectangles use top-left pixel coordinates. The reference density is one artwork pixel per logical UI unit.
Panels and control backgrounds use six-pixel nine-slice borders, with separate content padding. Icons are sprites.
Each 24 x 24 region occupies a 32 x 32 tile with four transparent gutter pixels on every side. The initial UI
renderer should sample the base mip; generated smaller mips do not provide region-isolated filtering.

Regenerate the artwork and atlas from the repository root:

```powershell
python utilities/ui_skin/generate_default.py
```

Regenerate the texture with the existing converter, using the matching built executable:

```powershell
python utilities/ui_skin/generate_default.py --tex-conv __exec/windows/arm64/full/opt/tex_conv.exe
```

To make a replacement skin, author a texture with the existing `tex_conv` workflow and a `ui_skin` `.nwb` using
the same named regions. Set its typed texture reference, atlas extent, density, rectangles, draw modes, slice
insets, content padding, and minimum sizes. Include both texture and skin metadata in the cooked asset input roots.
Control behavior and font glyph atlases are independent of the skin artwork.

Fixed-height lists use `list.background`, `list.row.normal`, `list.row.hover`, `list.row.selected`,
`list.row.disabled`, `scroll.track` and `scroll.thumb`. The background, normal/disabled rows and scroll parts
are semantic aliases of existing edit/button/panel tiles, so this adds no artwork or texture payload.
Hover and selected rows keep their authored list tiles; focus uses the independent `focus.overlay`.
The generator reproduces aliases from named source regions, including when the alternate skin remaps its UV tiles.

The single-line edit box uses the nine-slice `edit.normal`, `edit.focused` and `edit.disabled` regions, plus
the optional `focus.overlay`. A replacement skin can add `edit.hover`; an absent hover region falls back
to `edit.normal`. Padding and minimum size use the maximum across normal, hover, focused and disabled regions,
combined with `Builder::editStyle()` padding, so state changes do not move the text or alter layout. The default atlas supplies eight logical units of padding on
each edge; a 40-unit field fits the default 16-unit font, while content sizing includes the full line and padding.

Selection, caret and preedit underline are clipped geometry using edit-style colors rather than additional
font or skin atlas parts. The caret is one physical pixel wide after display scaling. Read-only fields retain
selection/copy behavior and use the ordinary/focused artwork; disabled fields use their separate region.
The Testbed edit gallery uses this same skin with application-owned text models and borrowed OS services.

Font bundles remain independent of the skin artwork. Each default font uses a same-stem `.nwb` declaration,
prepared `.font` shaping payload and binary `.atlas` with lossless RGBA SDF pages and positioning tables.
Replacing control artwork does not require rebaking those font bundles.
