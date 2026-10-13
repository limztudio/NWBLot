// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "image_fixture_support.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiFixtureSupport{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Impl;
using namespace Impl::Ui;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SharedImageSource MakeSeededImage(Core::Alloc::GlobalArena& arena, const StringView path, const u8 seed, const u32 width, const u32 height){
    const auto mipCount = TextureFormat::ComputeCompleteMipCount(TextureDimension::Texture2D, width, height, 1u);
    if(!mipCount)
        return {};
    Texture::MipLevelVector mips(arena);
    mips.reserve(*mipCount);
    u32 mipWidth = width;
    u32 mipHeight = height;
    u64 offset = 0u;
    for(u32 index = 0u; index < *mipCount; ++index){
        const auto plane = TextureFormat::ComputeMipPlaneBlockLayout(TexturePayloadFormat::UastcLdr4x4, mipWidth, mipHeight);
        if(!plane)
            return {};
        mips.push_back({ mipWidth, mipHeight, plane->blocksX, plane->blocksY, offset, plane->planeByteCount, 1u });
        offset += plane->planeByteCount;
        mipWidth = mipWidth > 1u ? mipWidth >> 1u : 1u;
        mipHeight = mipHeight > 1u ? mipHeight >> 1u : 1u;
    }
    if(offset > Limit<usize>::s_Max)
        return {};
    Core::Assets::AssetBytes bytes(arena);
    bytes.resize(static_cast<usize>(offset));
    for(usize index = 0u; index < bytes.size(); ++index)
        bytes[index] = static_cast<u8>(static_cast<u32>(seed) + index * 17u);
    Texture texture(arena, Name(path));
    texture.setPayload(TextureColorSpace::Srgb, true, width, height, Move(mips), Move(bytes), TextureDimension::Texture2D, 1u, TexturePayloadFormat::UastcLdr4x4, TextureAlphaMode::EmbeddedLdr, TextureFormat::s_OpaqueAlphaUnorm8);
    return MakeImageSource(arena, texture);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

