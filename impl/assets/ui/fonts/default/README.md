# Default UI fonts

The engine ships a Latin primary face and a Korean fallback. Each face is a same-stem pair: `latin.nwb` and `latin.font`, and likewise for `korean`. The readable `.nwb` declares independent `font face` and `font_atlas atlas` assets collected by `asset_bunch bunch = [face, atlas];`. It contains bake settings, editable vertical layout metrics, and every glyph mapping in source order. The Font declaration has no fields; face facts, compact group dimensions/channels, and ordinal glyph IDs are derived from the prepared source and mapping order. `atlas.font = face;` declares the typed relation. Companion paths are derived from the shared stem; no source filename, schema version, or integrity hash is exposed in `.nwb`.

The `.font` is a FON2 prepared source containing the exact original SFNT, a compact image directory, and the atlas pixels. Binary lengths, version, and integrity hashes remain internal. The font cooker reads the SFNT without allocating image pixels; the atlas cooker reads the images, admits the readable mappings, and copies the original positioning tables from the SFNT. The bunch publishes `engine/ui/fonts/default/latin/face` and `engine/ui/fonts/default/latin/atlas`, and the corresponding `/korean/face` and `/korean/atlas` identities. Runtime cooking still produces independent FON1 Font and FTA1 version 2 FontAtlas payloads.

| Stem | Glyphs | Image groups | Texture data | Prepared `.font` bytes |
| --- | ---: | --- | ---: | ---: |
| `latin` | 3,748 | 1024 × 1024 RGBA8; 1024 × 1023 RG8 | 5.998 MiB | 6,858,768 |
| `korean` | 24,964 | 2 × 2048 × 2048 RGBA8; 2048 × 2047 RGBA8; 2048 × 1209 R8 | 50.354 MiB | 57,444,484 |

Each stored channel is an independent scalar signed-distance page, including alpha when present. Both atlases use the pinned FreeType 2.14.3 bitmap-SDF renderer, 32 bake pixels per em, spread 8, one guard texel, and linear UNORM bytes without mips. The Latin atlas uses six logical pages, and the Korean atlas uses thirteen. All glyph IDs, including nondrawable spaces and shaped forms, have records. The `kern`, `GPOS`, and `GDEF` tables present in each source are retained exactly. HarfBuzz shapes from `.font`; atlas table bytes are not applied to positions a second time.

Compared with the former fixed RGBA8 squares, cropping guarded bounds and dropping unused trailing channels reduces the combined texture payload from 72 MiB to 56.3515625 MiB, saving 15.6484375 MiB (21.733941%). The regenerated glyph metrics, every guarded SDF region, all retained image components, source hashes, and positioning tables match the originals exactly. These are image-byte totals; device allocation size can also include driver tiling and alignment.

The original `.ttf` and `.otf` files are not retained beside the pairs. Their exact SFNT bytes remain in `.font`, so font rendering and repeat atlas generation require no external download. The pinned source revisions are:

| Face | Pinned upstream source | SFNT bytes |
| --- | --- | ---: |
| Latin | [Noto Sans at ffebf8c1ee449e544955a7e813c54f9b73848eac](https://github.com/notofonts/noto-fonts/blob/ffebf8c1ee449e544955a7e813c54f9b73848eac/hinted/ttf/NotoSans/NotoSans-Regular.ttf) | 569,208 |
| Korean | [Noto Sans CJK KR subset at Sans2.004 / 523d033d6cb47f4a80c58a35753646f5c3608a78](https://github.com/notofonts/noto-cjk/blob/523d033d6cb47f4a80c58a35753646f5c3608a78/Sans/SubsetOTF/KR/NotoSansKR-Regular.otf) | 4,644,748 |

Both fonts use the SIL Open Font License 1.1. The source notices and terms remain in `LICENSE-NotoSans.txt` and `LICENSE-NotoSansKR.txt`. To inspect the original SFNT, use the FON2 reader or skip its 56-byte header and 48-byte entry for each image group, then read the declared SFNT length. The builder also accepts `.font` directly:

```text
python -m launcher font-builder --config opt -- --font "/absolute/path/impl/assets/ui/fonts/default/latin.font" --output "/absolute/path/output/latin.nwb" --ppem 32 --spread 8 --extent 1024
python -m launcher font-builder --skip-build --config opt -- --font "/absolute/path/impl/assets/ui/fonts/default/korean.font" --output "/absolute/path/output/korean.nwb" --ppem 32 --spread 8 --extent 2048
```

`--overwrite` is required to replace an existing complete pair. The renderer uses these atlases at 24..48 physical pixels per em, corresponding to 0.75..1.5 times their 32-ppem bake, and uses native grayscale coverage outside that qualified range. Below 24 pixels, native coverage preserves the antialiased bottom row of small descenders. See the [font-builder guide](../../../../../utilities/font_builder/README.md) for the binary contract and generation limits.
