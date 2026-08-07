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
    // The number of independently-addressed 2D payload planes in this mip: one for Texture2D, six for a cubemap,
    // and the mip-reduced Z extent for Texture3D.
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
        , m_payloadBytes(arena)
    {}
    Texture(Core::Assets::AssetArena& arena, const Name& virtualPath)
        : Core::Assets::TypedAsset<Texture>(virtualPath)
        , m_mipLevels(arena)
        , m_payloadBytes(arena)
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
        Core::Assets::AssetBytes&& payloadBytes,
        const TextureDimension::Enum dimension = TextureDimension::Texture2D,
        const u32 depth = 1u,
        const TexturePayloadFormat::Enum payloadFormat = TexturePayloadFormat::UastcLdr4x4,
        const TextureAlphaMode::Enum alphaMode = TextureAlphaMode::Opaque,
        const u8 alphaConstantUnorm8 = TextureFormat::s_OpaqueAlphaUnorm8
    ){
        m_colorSpace = colorSpace;
        m_hasAlpha = hasAlpha;
        m_width = width;
        m_height = height;
        m_dimension = dimension;
        m_depth = depth;
        m_payloadFormat = payloadFormat;
        // Preserve the pre-HDR call shape: callers that pass only hasAlpha for
        // a regular LDR UASTC texture get its alpha stored in the primary blocks.
        m_alphaMode = payloadFormat == TexturePayloadFormat::UastcLdr4x4
            && hasAlpha
            && alphaMode == TextureAlphaMode::Opaque
            ? TextureAlphaMode::EmbeddedLdr
            : alphaMode
        ;
        m_alphaConstantUnorm8 = alphaConstantUnorm8;
        m_mipLevels = Move(mipLevels);
        m_payloadBytes = Move(payloadBytes);
    }

    [[nodiscard]] TextureColorSpace::Enum colorSpace()const{ return m_colorSpace; }
    [[nodiscard]] bool hasAlpha()const{ return m_hasAlpha; }
    [[nodiscard]] u32 width()const{ return m_width; }
    [[nodiscard]] u32 height()const{ return m_height; }
    [[nodiscard]] TextureDimension::Enum dimension()const{ return m_dimension; }
    [[nodiscard]] u32 depth()const{ return m_depth; }
    [[nodiscard]] TexturePayloadFormat::Enum payloadFormat()const{ return m_payloadFormat; }
    [[nodiscard]] TextureAlphaMode::Enum alphaMode()const{ return m_alphaMode; }
    [[nodiscard]] u8 alphaConstantUnorm8()const{ return m_alphaConstantUnorm8; }
    [[nodiscard]] const MipLevelVector& mipLevels()const{ return m_mipLevels; }
    [[nodiscard]] const Core::Assets::AssetBytes& payloadBytes()const{ return m_payloadBytes; }
    // Preserved for UASTC callers. HDR alpha data, when present, follows the
    // RGB UASTC HDR stream in payloadBytes().
    [[nodiscard]] const Core::Assets::AssetBytes& uastcBlocks()const{ return m_payloadBytes; }
    [[nodiscard]] u64 primaryPayloadByteCount()const{
        if(m_mipLevels.empty())
            return 0u;
        const TextureMipLevel& lastMip = m_mipLevels.back();
        return lastMip.offsetBytes + lastMip.sizeBytes;
    }
    [[nodiscard]] const u8* alphaUastcBlocks()const{
        if(m_alphaMode != TextureAlphaMode::SeparateUastcLdr4x4)
            return nullptr;
        return m_payloadBytes.data() + static_cast<usize>(primaryPayloadByteCount());
    }


private:
    u32 m_width = 0u;
    u32 m_height = 0u;
    u32 m_depth = 1u;

    TextureColorSpace::Enum m_colorSpace = TextureColorSpace::Linear;
    TextureDimension::Enum m_dimension = TextureDimension::Texture2D;
    TexturePayloadFormat::Enum m_payloadFormat = TexturePayloadFormat::UastcLdr4x4;
    TextureAlphaMode::Enum m_alphaMode = TextureAlphaMode::Opaque;
    bool m_hasAlpha = false;
    u8 m_alphaConstantUnorm8 = TextureFormat::s_OpaqueAlphaUnorm8;

    MipLevelVector m_mipLevels;
    Core::Assets::AssetBytes m_payloadBytes;
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

