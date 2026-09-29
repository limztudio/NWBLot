# Default UI font bundles

The engine ships a Latin primary face and a Korean fallback. Each face is a paired, same-stem trio: `latin.nwb`, `latin.font`, `latin.atlas`, and likewise for `korean`. The small `.nwb` declares `font_bundle` schema 1. The `.font` is a FON1 envelope around the exact original SFNT bytes needed by FreeType and HarfBuzz. The `.atlas` is a binary FTA1 package containing SDF glyph records, RGBA groups, and exact original positioning tables. The cooker derives the font identity from the stem and the atlas identity by appending `_atlas`; the metadata contains no source filename or font reference.

| Stem | Cooked `Font` identity | Cooked `FontAtlas` identity | Glyphs | RGBA groups | Atlas bytes |
| --- | --- | --- | ---: | ---: | ---: |
| `latin` | `engine/ui/fonts/default/latin` | `engine/ui/fonts/default/latin_atlas` | 3,748 | 2 × 1024² | 8,657,014 |
| `korean` | `engine/ui/fonts/default/korean` | `engine/ui/fonts/default/korean_atlas` | 24,964 | 4 × 2048² | 68,451,728 |

Each RGBA channel is an independent scalar signed-distance page, including alpha. Both atlases use the pinned FreeType 2.14.3 bitmap-SDF renderer, 32 bake pixels per em, spread 8, one guard texel, and linear RGBA8 without mips. The Latin atlas uses six logical pages, and the Korean atlas uses thirteen. All glyph IDs, including nondrawable spaces and shaped forms, have records. The `kern`, `GPOS`, and `GDEF` tables present in each source are retained exactly. HarfBuzz shapes from `.font`; atlas table bytes are not applied to positions a second time.

The original `.ttf` and `.otf` files are not retained beside the bundles. Their exact SFNT bytes remain in `.font`, so font rendering and repeat atlas generation require no external download. The source revisions and hashes are:

| Face | Pinned upstream source | SFNT bytes | SFNT SHA-256 |
| --- | --- | ---: | --- |
| Latin | [Noto Sans at ffebf8c1ee449e544955a7e813c54f9b73848eac](https://github.com/notofonts/noto-fonts/blob/ffebf8c1ee449e544955a7e813c54f9b73848eac/hinted/ttf/NotoSans/NotoSans-Regular.ttf) | 569,208 | `b85c38ecea8a7cfb39c24e395a4007474fa5a4fc864f6ee33309eb4948d232d5` |
| Korean | [Noto Sans CJK KR subset at Sans2.004 / 523d033d6cb47f4a80c58a35753646f5c3608a78](https://github.com/notofonts/noto-cjk/blob/523d033d6cb47f4a80c58a35753646f5c3608a78/Sans/SubsetOTF/KR/NotoSansKR-Regular.otf) | 4,644,748 | `69975a0ac8472717870aefeab0a4d52739308d90856b9955313b2ad5e0148d68` |

Both fonts use the SIL Open Font License 1.1. The source notices and terms remain in `LICENSE-NotoSans.txt` and `LICENSE-NotoSansKR.txt`. To inspect the original SFNT, read the 24-byte FON1 header and then the remaining `.font` bytes. The builder also accepts `.font` directly:

```text
python -m launcher font-builder --config opt -- --font "/absolute/path/impl/assets/ui/fonts/default/latin.font" --output "/absolute/path/output/latin.nwb" --ppem 32 --spread 8 --extent 1024
python -m launcher font-builder --skip-build --config opt -- --font "/absolute/path/impl/assets/ui/fonts/default/korean.font" --output "/absolute/path/output/korean.nwb" --ppem 32 --spread 8 --extent 2048
```

`--overwrite` is required to replace an existing complete trio. The renderer uses these atlases at 24..48 physical pixels per em, corresponding to 0.75..1.5 times their 32-ppem bake, and uses native grayscale coverage outside that qualified range. Below 24 pixels, native coverage preserves the antialiased bottom row of small descenders. See the [font-builder guide](../../../../../utilities/font_builder/README.md) for the binary contract and generation limits.
