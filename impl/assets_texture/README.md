# Texture assets

Texture assets cook both UASTC LDR and UASTC HDR into the current `TEX1` version 3 runtime layout. `binary_payload.h` owns the packed 48-byte header, followed by 40-byte mip records and the exact payload bytes. Readers admit only `s_TextureVersion`; older cooked volumes must be recooked from their source `.nwb` and `.tex` files.

The header declares the payload format and alpha transport explicitly. `alphaInfo` packs the alpha mode in bits 0 through 7 and its UNORM8 constant in bits 8 through 15, with zero reserved high bits. LDR blocks use opaque or embedded alpha. HDR blocks use opaque alpha, a constant below 255, or a trailing LDR alpha stream matching the primary mip layout. Readers validate flags, dimensions, complete mip coverage, byte counts, and exact payload consumption.

Source `.nwb` metadata describes only author choices. Every texture requires `format` (`uastc_ldr_4x4` or `uastc_hdr_4x4`), `dimension` (`2d`, `cube`, or `volume`), positive `width` and `height`, and `data`, a same-directory `.tex` filename without path components. Volumes additionally require positive `depth`; 2D and cube textures infer depth 1 and reject an authored depth. Cube faces must be square.

LDR metadata requires `color_space` (`linear` or `srgb`) and numeric `has_alpha` (0 or 1). HDR metadata requires `alpha_mode` (`opaque`, `constant_unorm8`, or `uastc_ldr_4x4`); only `constant_unorm8` permits and requires `alpha_constant_unorm8`, an integer from 0 through 254. Use `opaque` for 255. HDR color space is always linear, and alpha presence follows its alpha mode.

The cooker derives the fixed 4x4/16-byte block contract, complete mip chain, block grids, slice counts, offsets, byte counts, and optional trailing HDR alpha layout. Authors do not repeat those details, mip addressing, HDR color space/alpha flags, or version/revision fields in `.nwb`; unknown fields are rejected. The raw sidecar must exactly match the derived layout. See [the converter guide](../../utilities/tex_conv/README.md) for byte order and authoring commands. Internal runtime binary versions remain codec contracts.

The shared payload types and mip arithmetic live directly in `global/texture_payload.h`. Callers supply every `Texture::setPayload` argument, including the dimension, depth, payload format, alpha mode, and alpha constant. The payload setter preserves those explicit values; `validatePayload()` rejects inconsistent alpha declarations. `payloadBytes()` exposes the whole transport, and `alphaUastcBlocks()` exposes the optional trailing HDR alpha stream.
