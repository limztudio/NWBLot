# Default UI skin

`atlas.nwb` is the `ui_skin` asset at `engine/ui/skins/default/atlas`. It names regions in one texture asset,
`engine/ui/skins/default/texture`, described by `texture.nwb` and its UASTC `texture.tex` payload. `source.png`
is the deterministic source artwork, authored as straight-alpha sRGB color.

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
