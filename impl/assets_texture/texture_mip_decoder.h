// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "asset.h"

#include <core/alloc/scratch.h>
#include <core/graphics/backend_selection.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Owns UASTC slice layout plus LDR/HDR mip decode feeding texture uploads.
struct TextureDecodedMipUpload{
    Vector<u8, Core::Alloc::ScratchArena> bytes;
    usize rowPitch = 0u;
    usize sliceByteCount = 0u;

    explicit TextureDecodedMipUpload(Core::Alloc::ScratchArena& arena)
        : bytes(arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class TextureMipDecoder final : NoCopy{
public:
    TextureMipDecoder();


public:
    [[nodiscard]] bool decode(
        const Texture& textureAsset,
        const TextureMipLevel& mip,
        u32 mipLevel,
        Core::Format::Enum format,
        TextureDecodedMipUpload& outUpload
    );


private:
    [[nodiscard]] bool decodeLdr(
        const Texture& textureAsset,
        const TextureMipLevel& mip,
        u32 mipLevel,
        Core::Format::Enum format,
        TextureDecodedMipUpload& outUpload
    );

    [[nodiscard]] bool decodeHdr(
        const Texture& textureAsset,
        const TextureMipLevel& mip,
        u32 mipLevel,
        Core::Format::Enum format,
        TextureDecodedMipUpload& outUpload
    );
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

