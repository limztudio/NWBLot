// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TextureBinaryPayload{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_TextureMagic = 0x54455831u; // TEX1
inline constexpr u32 s_TextureVersion = 3u;
inline constexpr TStringView s_TextureLoadBinaryContext = GLB_TEXT("Texture::loadBinary");
inline constexpr TStringView s_TextureValidatePayloadContext = GLB_TEXT("Texture::validatePayload");
inline constexpr TStringView s_TextureAssetKindLabel = GLB_TEXT("texture");

static constexpr usize s_PackedAlignBytes = 1u;

// alphaInfo stores its mode in bits 0..7 and the UNORM8 constant in bits 8..15; the high half must remain zero.
#pragma pack(push, 1)
struct HeaderBinary{
    u32 magic = s_TextureMagic;
    u32 version = s_TextureVersion;
    u32 colorSpace = 0u;
    u32 dimension = 0u;
    u32 width = 0u;
    u32 height = 0u;
    u32 depth = 0u;
    u32 mipCount = 0u;
    u32 alphaInfo = 0u;
    u32 payloadFormat = 0u;
    u64 payloadByteCount = 0u;
};
#pragma pack(pop)
static constexpr usize s_HeaderBinaryByteSize = 48u;
static_assert(sizeof(HeaderBinary) == s_HeaderBinaryByteSize, "Texture V3 header layout drifted");
static_assert(alignof(HeaderBinary) == s_PackedAlignBytes, "Texture V3 header must stay packed");
static_assert(IsStandardLayout_V<HeaderBinary>, "Texture V3 header must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<HeaderBinary>, "Texture V3 header must stay binary-serializable");

inline constexpr u32 s_AlphaInfoModeMask = 0x000000FFu;
inline constexpr u32 s_AlphaInfoConstantShift = 8u;
inline constexpr u32 s_AlphaInfoConstantMask = 0x0000FF00u;
inline constexpr u32 s_AlphaInfoReservedMask = 0xFFFF0000u;

#pragma pack(push, 1)
struct MipLevelBinary{
    u32 width = 0u;
    u32 height = 0u;
    u32 sliceCount = 0u;
    u32 blockCountX = 0u;
    u32 blockCountY = 0u;
    u32 reserved = 0u;
    u64 offsetBytes = 0u;
    u64 sizeBytes = 0u;
};
#pragma pack(pop)
static constexpr usize s_MipLevelBinaryByteSize = 40u;
static_assert(sizeof(MipLevelBinary) == s_MipLevelBinaryByteSize, "Texture mip level layout drifted");
static_assert(alignof(MipLevelBinary) == s_PackedAlignBytes, "Texture mip level must stay packed");
static_assert(IsStandardLayout_V<MipLevelBinary>, "Texture mip level must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<MipLevelBinary>, "Texture mip level must stay binary-serializable");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

