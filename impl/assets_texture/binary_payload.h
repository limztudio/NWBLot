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
inline constexpr u32 s_TextureVersionV2 = 2u;
inline constexpr u32 s_TextureVersion = 3u;

#pragma pack(push, 1)
struct HeaderPrefix{
    u32 magic = s_TextureMagic;
    u32 version = s_TextureVersion;
};
#pragma pack(pop)
static_assert(sizeof(HeaderPrefix) == 8u, "Texture header prefix layout drifted");
static_assert(alignof(HeaderPrefix) == 1u, "Texture header prefix must stay packed");
static_assert(IsStandardLayout_V<HeaderPrefix>, "Texture header prefix must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<HeaderPrefix>, "Texture header prefix must stay binary-serializable");

#pragma pack(push, 1)
// Existing cooked LDR assets use this layout. Keep it readable so that the V3
// runtime can retain the established UASTC contract.
struct HeaderBinaryV2{
    u32 magic = s_TextureMagic;
    u32 version = s_TextureVersionV2;
    u32 colorSpace = 0u;
    u32 dimension = 0u;
    u32 width = 0u;
    u32 height = 0u;
    u32 depth = 0u;
    u32 mipCount = 0u;
    u32 hasAlpha = 0u;
    u32 reserved = 0u;
    u64 uastcByteCount = 0u;
};
#pragma pack(pop)
static_assert(sizeof(HeaderBinaryV2) == 48u, "Texture V2 header layout drifted");
static_assert(alignof(HeaderBinaryV2) == 1u, "Texture V2 header must stay packed");
static_assert(IsStandardLayout_V<HeaderBinaryV2>, "Texture V2 header must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<HeaderBinaryV2>, "Texture V2 header must stay binary-serializable");

// V3 encodes the primary payload type and HDR alpha transport without growing
// the header. alphaInfo packs its mode in bits 0..7 and the UNORM8 constant
// alpha in bits 8..15; the high half remains zero for future expansion.
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
static_assert(sizeof(HeaderBinary) == 48u, "Texture V3 header layout drifted");
static_assert(alignof(HeaderBinary) == 1u, "Texture V3 header must stay packed");
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
static_assert(sizeof(MipLevelBinary) == 40u, "Texture mip level layout drifted");
static_assert(alignof(MipLevelBinary) == 1u, "Texture mip level must stay packed");
static_assert(IsStandardLayout_V<MipLevelBinary>, "Texture mip level must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<MipLevelBinary>, "Texture mip level must stay binary-serializable");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

