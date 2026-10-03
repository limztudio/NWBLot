# Default UI font bundles

The engine ships a Latin primary face and a Korean fallback. Each face is a same-stem trio: `latin.nwb`, `latin.font`, `latin.atlas`, and likewise for `korean`. The readable `.nwb` declares independent schema 1 `font face` and `font_atlas atlas` assets collected by `asset_bunch bunch = [face, atlas];`. Each asset has its own visible metadata, and `atlas.font = face;` declares the typed relation. The cooker checks the declared metrics, hashes, bake settings, and group layouts against the paired binaries; companion paths are derived from the shared stem.

The `.font` is a FON1 envelope around the exact original SFNT bytes needed by FreeType and HarfBuzz. The `.atlas` is a current-version 2 FTA1 package containing SDF glyph records, compact image groups, and exact original positioning tables. The bunch publishes `engine/ui/fonts/default/latin/face` and `engine/ui/fonts/default/latin/atlas`, and the corresponding `/korean/face` and `/korean/atlas` identities.

| Stem | Glyphs | Image groups | Texture data | Atlas file bytes |
| --- | ---: | --- | ---: | ---: |
| `latin` | 3,748 | 1024 × 1024 RGBA8; 1024 × 1023 RG8 | 5.998 MiB | 6,557,822 |
| `korean` | 24,964 | 2 × 2048 × 2048 RGBA8; 2048 × 2047 RGBA8; 2048 × 1209 R8 | 50.354 MiB | 54,142,368 |

Each stored channel is an independent scalar signed-distance page, including alpha when present. Both atlases use the pinned FreeType 2.14.3 bitmap-SDF renderer, 32 bake pixels per em, spread 8, one guard texel, and linear UNORM bytes without mips. The Latin atlas uses six logical pages, and the Korean atlas uses thirteen. All glyph IDs, including nondrawable spaces and shaped forms, have records. The `kern`, `GPOS`, and `GDEF` tables present in each source are retained exactly. HarfBuzz shapes from `.font`; atlas table bytes are not applied to positions a second time.

Compared with the former fixed RGBA8 squares, cropping guarded bounds and dropping unused trailing channels reduces the combined texture payload from 72 MiB to 56.3515625 MiB, saving 15.6484375 MiB (21.733941%). The regenerated glyph metrics, every guarded SDF region, all retained image components, source hashes, and positioning tables match the originals exactly. These are image-byte totals; device allocation size can also include driver tiling and alignment.

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
