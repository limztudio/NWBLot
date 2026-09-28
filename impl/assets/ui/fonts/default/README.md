# Default UI fonts

The engine ships an unmodified deterministic Latin primary font and a Korean fallback. The defaults are typed `Font` assets at `engine/ui/fonts/default/latin` and `engine/ui/fonts/default/korean`. Loading and fallback policy belong to the UI text service; the asset layer retains these original SFNT bytes.

| Source file | Pinned upstream source | Bytes | SHA256 |
| --- | --- | ---: | --- |
| `NotoSans-Regular.ttf` | [Noto Sans at ffebf8c1ee449e544955a7e813c54f9b73848eac](https://github.com/notofonts/noto-fonts/blob/ffebf8c1ee449e544955a7e813c54f9b73848eac/hinted/ttf/NotoSans/NotoSans-Regular.ttf) | 569208 | `b85c38ecea8a7cfb39c24e395a4007474fa5a4fc864f6ee33309eb4948d232d5` |
| `NotoSansKR-Regular.otf` | [Noto Sans CJK KR subset at Sans2.004 / 523d033d6cb47f4a80c58a35753646f5c3608a78](https://github.com/notofonts/noto-cjk/blob/523d033d6cb47f4a80c58a35753646f5c3608a78/Sans/SubsetOTF/KR/NotoSansKR-Regular.otf) | 4644748 | `69975a0ac8472717870aefeab0a4d52739308d90856b9955313b2ad5e0148d68` |

Both fonts use the SIL Open Font License 1.1. Exact upstream notices and licence terms are preserved as `LICENSE-NotoSans.txt` and `LICENSE-NotoSansKR.txt`. The Korean region subset includes Korean coverage without carrying the full pan-CJK font. Font files are unchanged, and their original upstream names are retained.

To reproduce the bundled bytes, download the files from the pinned commits in the table (replace the GitHub `blob` URL prefix with `https://raw.githubusercontent.com/notofonts/<repository>/<commit>/`), then verify byte counts and SHA256 values. Download the `LICENSE` file from the same respective commit and retain it under the licence filename above. The metadata files declare schema 1, the adjacent source file, and face index zero. Asset cooking performs no font conversion.
