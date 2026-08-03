// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "format.h"

#include <core/assets/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TextureColorSpace{
    enum Enum : u8{
        Linear = 0u,
        Srgb,
    };
};

[[nodiscard]] inline bool IsValidTextureColorSpace(const TextureColorSpace::Enum colorSpace){
    return colorSpace == TextureColorSpace::Linear || colorSpace == TextureColorSpace::Srgb;
}

struct TextureMipLevel{
    u32 width = 0u;
    u32 height = 0u;
    u32 blockCountX = 0u;
    u32 blockCountY = 0u;
    u64 offsetBytes = 0u;
    u64 sizeBytes = 0u;
    // The number of independently-addressed 2D UASTC block planes in this mip: one for Texture2D, six for a
    // cubemap, and the mip-reduced Z extent for Texture3D.
    u32 sliceCount = 1u;
};
static_assert(IsStandardLayout_V<TextureMipLevel>, "Texture mip level must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<TextureMipLevel>, "Texture mip level must stay binary-serializable");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Texture final : public Core::Assets::TypedAsset<Texture>{
public:
    NWB_DEFINE_ASSET_TYPE("texture")


public:
    using MipLevelVector = Core::Assets::AssetVector<TextureMipLevel>;


public:
    explicit Texture(Core::Assets::AssetArena& arena)
        : m_mipLevels(arena)
        , m_uastcBlocks(arena)
    {}
    Texture(Core::Assets::AssetArena& arena, const Name& virtualPath)
        : Core::Assets::TypedAsset<Texture>(virtualPath)
        , m_mipLevels(arena)
        , m_uastcBlocks(arena)
    {}


public:
    bool loadBinary(const Core::Assets::AssetBytes& binary);
    [[nodiscard]] bool validatePayload()const;

public:
    void setPayload(
        const TextureColorSpace::Enum colorSpace,
        const bool hasAlpha,
        const u32 width,
        const u32 height,
        MipLevelVector&& mipLevels,
        Core::Assets::AssetBytes&& uastcBlocks,
        const TextureDimension::Enum dimension = TextureDimension::Texture2D,
        const u32 depth = 1u
    ){
        m_colorSpace = colorSpace;
        m_hasAlpha = hasAlpha;
        m_width = width;
        m_height = height;
        m_dimension = dimension;
        m_depth = depth;
        m_mipLevels = Move(mipLevels);
        m_uastcBlocks = Move(uastcBlocks);
    }

    [[nodiscard]] TextureColorSpace::Enum colorSpace()const{ return m_colorSpace; }
    [[nodiscard]] bool hasAlpha()const{ return m_hasAlpha; }
    [[nodiscard]] u32 width()const{ return m_width; }
    [[nodiscard]] u32 height()const{ return m_height; }
    [[nodiscard]] TextureDimension::Enum dimension()const{ return m_dimension; }
    [[nodiscard]] u32 depth()const{ return m_depth; }
    [[nodiscard]] const MipLevelVector& mipLevels()const{ return m_mipLevels; }
    [[nodiscard]] const Core::Assets::AssetBytes& uastcBlocks()const{ return m_uastcBlocks; }


private:
    u32 m_width = 0u;
    u32 m_height = 0u;
    u32 m_depth = 1u;

    TextureColorSpace::Enum m_colorSpace = TextureColorSpace::Linear;
    TextureDimension::Enum m_dimension = TextureDimension::Texture2D;
    bool m_hasAlpha = false;

    MipLevelVector m_mipLevels;
    Core::Assets::AssetBytes m_uastcBlocks;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class TextureAssetCodec final : public Core::Assets::AssetCodec<Texture>{
public:
    TextureAssetCodec() = default;


#if defined(NWB_COOK)
public:
    virtual bool serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const override;
#endif
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
