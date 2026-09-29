# Default UI fonts

The engine ships an unmodified deterministic Latin primary font and a Korean fallback. The defaults are typed `Font` assets at `engine/ui/fonts/default/latin` and `engine/ui/fonts/default/korean`. Loading and fallback policy belong to the UI text service; the asset layer retains these original SFNT bytes.

| Source file | Pinned upstream source | Bytes | SHA256 |
| --- | --- | ---: | --- |
| `NotoSans-Regular.ttf` | [Noto Sans at ffebf8c1ee449e544955a7e813c54f9b73848eac](https://github.com/notofonts/noto-fonts/blob/ffebf8c1ee449e544955a7e813c54f9b73848eac/hinted/ttf/NotoSans/NotoSans-Regular.ttf) | 569208 | `b85c38ecea8a7cfb39c24e395a4007474fa5a4fc864f6ee33309eb4948d232d5` |
| `NotoSansKR-Regular.otf` | [Noto Sans CJK KR subset at Sans2.004 / 523d033d6cb47f4a80c58a35753646f5c3608a78](https://github.com/notofonts/noto-cjk/blob/523d033d6cb47f4a80c58a35753646f5c3608a78/Sans/SubsetOTF/KR/NotoSansKR-Regular.otf) | 4644748 | `69975a0ac8472717870aefeab0a4d52739308d90856b9955313b2ad5e0148d68` |

Both fonts use the SIL Open Font License 1.1. Exact upstream notices and licence terms are preserved as `LICENSE-NotoSans.txt` and `LICENSE-NotoSansKR.txt`. The Korean region subset includes Korean coverage without carrying the full pan-CJK font. Font files are unchanged, and their original upstream names are retained.

To reproduce the bundled bytes, download the files from the pinned commits in the table (replace the GitHub `blob` URL prefix with `https://raw.githubusercontent.com/notofonts/<repository>/<commit>/`), then verify byte counts and SHA256 values. Download the `LICENSE` file from the same respective commit and retain it under the licence filename above. The metadata files declare schema 1, the adjacent source file, and face index zero. Asset cooking performs no font conversion.

## Generated SDF atlases

The adjacent `latin_atlas.nwb` and `korean_atlas.nwb` are typed `FontAtlas` assets at `engine/ui/fonts/default/latin_atlas` and `engine/ui/fonts/default/korean_atlas`. Each atlas is one authoring schema 2 `.nwb` document containing its glyph metadata, every RGBA group, and exact original positioning tables. They match the exact original font hashes above and enumerate every source glyph ID, including ligatures, contextual forms and nondrawable whitespace. The native shaping fonts remain separate `Font` assets; their source bytes and license notices remain unchanged.

Both defaults use the pinned FreeType 2.14.3 bitmap-SDF renderer, 32 bake pixels per em, spread 8, one guard texel, the `freetype_sdf_u8_v1` distance encoding, and lossless linear RGBA8 with one base mip. Each R/G/B/A channel is an independent scalar page, including alpha. Generation uses all-glyph policy and a maximum of 8 RGBA groups.

| Atlas identity | Glyph records | Used logical pages | RGBA groups | Group extent | Raw pixel storage | Single `.nwb` bytes |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `engine/ui/fonts/default/latin_atlas` | 3,748 | 6 | 2 | 1024 × 1024 | 8 MiB (8,388,608 bytes) | 5,176,115 |
| `engine/ui/fonts/default/korean_atlas` | 24,964 | 13 | 4 | 2048 × 2048 | 64 MiB (67,108,864 bytes) | 51,901,890 |

Raw pixel storage includes all four channels of each allocated group, including unused pages and exterior texels; metadata and retained positioning tables are additional decoded bytes. The authoring `.nwb` files use `payload_encoding = "zstd_base64"`: each group and table contains one deterministic Zstd level 9 frame, with declared content size and checksum, stored as canonical base64 chunks of at most 4,096 characters. Each record retains its decoded byte count and SHA-256 for exact content verification. These packages contain no hash-named image or positioning sidecars. Compression changes file storage without changing texture extents, glyph records, channel assignment, or decoded runtime pixels. The runtime `FTA1` binary format remains version 1; the cooker also supports older authoring schema 1 sidecar packages.

The `opentype_tables` records retain exact original `GPOS` and `GDEF` table bytes. This preserves modern kerning/class positioning information without expanding glyph pairs. HarfBuzz shapes from the original source font; atlas positioning data must not be applied again after shaping.

To regenerate these defaults from this Windows checkout, use absolute source/output paths because the repository launcher runs the utility from its runtime output directory:

```text
python -m launcher font-atlas --config opt -- --font "C:/WorkStation/NWBLot/impl/assets/ui/fonts/default/NotoSans-Regular.ttf" --font-asset engine/ui/fonts/default/latin --output "C:/WorkStation/NWBLot/impl/assets/ui/fonts/default/latin_atlas.nwb" --ppem 32 --spread 8 --extent 1024 --max-groups 8 --renderer bitmap --overwrite
python -m launcher font-atlas --skip-build --config opt -- --font "C:/WorkStation/NWBLot/impl/assets/ui/fonts/default/NotoSansKR-Regular.otf" --font-asset engine/ui/fonts/default/korean --output "C:/WorkStation/NWBLot/impl/assets/ui/fonts/default/korean_atlas.nwb" --ppem 32 --spread 8 --extent 2048 --max-groups 8 --renderer bitmap --overwrite
```

Replace the checkout prefix with your own absolute path on Windows or Linux. `--overwrite` is explicit because these atlas files already exist. The utility stages and verifies the complete document before replacing the requested file. Its compression uses fixed settings without worker threads for deterministic generation; native Linux execution is still required to establish cross-platform byte equivalence.

The renderer currently uses these atlases at 16..48 physical pixels per em, corresponding to 0.5..1.5 times their 32-ppem bake, and uses native grayscale coverage outside this measured range. See [the utility guide](../../../../../utilities/font_atlas/README.md) for generation, transport, kerning and rendering limits.
