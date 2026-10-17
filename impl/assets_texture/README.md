# Texture assets

Textures use a basename pair: `image.nwb` describes the texture and `image.tex` contains its raw linear UASTC blocks. [tex_conv](../../utilities/tex_conv/README.md) converts input RGB to linear light, generates the complete mip chain, and writes both files. The cooker derives checked mip records from the metadata, validates the raw byte layout, and packages it into the current runtime texture binary. It performs no image conversion or texture encoding.

A 7 by 5 LDR texture with embedded alpha uses:

```
texture asset;
asset.format = "uastc_ldr_4x4";
asset.dimension = "2d";
asset.resolution = [7, 5];
asset.alpha_mode = "embedded_ldr";
```

The metadata file implicitly selects the same-directory `.tex` file with the same basename. Every texture requires these fields:

| Field | Contract |
| --- | --- |
| `format` | `uastc_ldr_4x4` or `uastc_hdr_4x4` |
| `dimension` | `2d`, `cube`, or `volume` |
| `resolution` | Positive integer `[width, height]` for 2D/cube or `[width, height, depth]` for volumes; cube faces must be square |
| `alpha_mode` | `opaque`, `embedded_ldr`, `constant_unorm8`, or `separate_uastc_ldr_4x4`, subject to the format rules below |

LDR blocks use `opaque` or `embedded_ldr`. HDR color blocks use `opaque`, `constant_unorm8`, or `separate_uastc_ldr_4x4`. Only `constant_unorm8` requires and permits `alpha_constant_unorm8`, an integer from 0 through 254; opaque alpha uses 255 internally. The cooker derives the complete mip count from the resolution. Metadata does not contain file references, source color spaces, individual width/height/depth fields, block geometry, mip counts or offsets, or version fields. Unknown fields are rejected.

The raw `.tex` file contains no header or mip table. Each primary mip stores its independently addressed planes followed by their row-major 4 by 4, 16-byte UASTC blocks. Plane count is one for 2D, six in `+X, -X, +Y, -Y, +Z, -Z` order for a cube, or the reduced depth for a volume. Mips run from the base resolution through the complete chain. Separate HDR alpha is a second LDR UASTC stream after the entire primary stream, with identical mip and plane layout. Opaque, embedded, and constant modes have only the primary stream.

The cooker derives the complete mip chain through 1 by 1, and depth 1 for volumes, from the semantic dimensions. It derives block grids, slice counts, contiguous offsets, and total byte count using checked arithmetic. It rejects incomplete mip chains, invalid dimensions or alpha combinations, overflow, truncated payloads, and extra raw bytes before publishing the asset. It then applies the normal `Texture::validatePayload` admission rules and serializes the internal runtime header and mip table.

[global/texture_payload.h](../../global/texture_payload.h) owns the shared format, dimension, alpha, block arithmetic, and cooked `TEX1` version 4 layout. Cooked binaries contain a packed 44-byte header, 40-byte mip records, and the exact validated raw block bytes. The header records dimensions, mip count, payload format, alpha transport, and payload byte count. `alphaInfo` packs the mode in bits 0 through 7 and its UNORM8 constant in bits 8 through 15, with zero reserved high bits. The raw authoring sidecar and the cooked runtime binary have distinct layouts.

Runtime readers admit only the current binary version and validate the complete mip layout, alpha consistency, reserved fields, and exact payload consumption. Every runtime texture uses linear RGB and linear alpha coverage. GPU upload selects linear ASTC, BC7, or RGBA8 for LDR and float ASTC, BC6H, or RGBA16F for HDR, according to device support.

Runtime mip sampling belongs to the independently bound sampler asset. Its `mip_filter` field selects `nearest` or `linear`; texture metadata does not accept that field. `Texture::setPayload` receives explicit dimensions, mip records, payload format, alpha mode, and alpha constant values. `payloadBytes()` exposes the complete block transport, and `alphaUastcBlocks()` exposes the optional trailing HDR alpha stream.
