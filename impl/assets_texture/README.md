# Texture assets

Texture assets cook both UASTC LDR and UASTC HDR into the current `TEX1` version 3 runtime layout. `binary_payload.h` owns the packed 48-byte header, followed by 40-byte mip records and the exact payload bytes. Readers admit only `s_TextureVersion`; older cooked volumes must be recooked from their source `.nwb` and `.tex` files.

The header declares the payload format and alpha transport explicitly. `alphaInfo` packs the alpha mode in bits 0 through 7 and its UNORM8 constant in bits 8 through 15, with zero reserved high bits. LDR blocks use opaque or embedded alpha. HDR blocks use opaque alpha, a constant below 255, or a trailing LDR alpha stream matching the primary mip layout. Readers validate flags, dimensions, complete mip coverage, byte counts, and exact payload consumption.

Source metadata has distinct current contracts: `uastc_ldr_4x4` uses metadata version 1 and `uastc_hdr_4x4` uses metadata version 2. These select different payload formats; they are not alternate generations of the runtime envelope. Each format requires its exact metadata version and specification revision.

The shared payload types and mip arithmetic live directly in `global/texture_payload.h`. Callers supply every `Texture::setPayload` argument, including the dimension, depth, payload format, alpha mode, and alpha constant. The payload setter preserves those explicit values; `validatePayload()` rejects inconsistent alpha declarations. `payloadBytes()` exposes the whole transport, and `alphaUastcBlocks()` exposes the optional trailing HDR alpha stream.
