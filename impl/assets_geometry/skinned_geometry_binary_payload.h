// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "skinned_geometry_types.h"

#include <global/binary.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SkinnedGeometryBinaryPayload{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_SkinnedGeometryMagic = 0x44474F31u; // DGO1
inline constexpr u32 s_SkinnedGeometryVersion = 1u;
inline constexpr u32 s_SkinnedGeometryDisplacementTextureMagic = 0x44445431u; // DDT1
inline constexpr u32 s_SkinnedGeometryDisplacementTextureVersion = 1u;
inline constexpr u32 s_SkinnedGeometrySkeletonJointLimit = static_cast<u32>(Limit<u16>::s_Max) + 1u;

#pragma pack(push, 1)
struct SkinnedGeometryHeaderBinary{
    u32 magic = s_SkinnedGeometryMagic;
    u32 version = s_SkinnedGeometryVersion;
    u32 geometryClass = GeometryClass::Invalid;
    u64 restVertexCount = 0;
    u64 indexCount = 0;
    u64 skinCount = 0;
    u64 skeletonJointCount = 0;
    u64 inverseBindMatrixCount = 0;
    u64 sourceSampleCount = 0;
    u64 editMaskCount = 0;
    u64 morphCount = 0;
    u64 stringTableByteCount = 0;
};
struct SkinnedGeometryDisplacementTextureHeaderBinary{
    u32 magic = s_SkinnedGeometryDisplacementTextureMagic;
    u32 version = s_SkinnedGeometryDisplacementTextureVersion;
    u32 width = 0;
    u32 height = 0;
    u64 texelCount = 0;
};
#pragma pack(pop)
static_assert(sizeof(SkinnedGeometryHeaderBinary) == sizeof(u32) + sizeof(u32) + sizeof(u32) + (sizeof(u64) * 9u), "SkinnedGeometryHeaderBinary layout drifted");
static_assert(sizeof(SkinnedGeometryDisplacementTextureHeaderBinary) == sizeof(u32) + sizeof(u32) + sizeof(u32) + sizeof(u32) + sizeof(u64), "SkinnedGeometryDisplacementTextureHeaderBinary layout drifted");
static_assert(alignof(SkinnedGeometryHeaderBinary) == 1u, "SkinnedGeometryHeaderBinary must stay packed");
static_assert(alignof(SkinnedGeometryDisplacementTextureHeaderBinary) == 1u, "SkinnedGeometryDisplacementTextureHeaderBinary must stay packed");
static_assert(IsStandardLayout_V<SkinnedGeometryHeaderBinary>, "SkinnedGeometryHeaderBinary must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<SkinnedGeometryHeaderBinary>, "SkinnedGeometryHeaderBinary must stay binary-serializable");
static_assert(IsStandardLayout_V<SkinnedGeometryDisplacementTextureHeaderBinary>, "SkinnedGeometryDisplacementTextureHeaderBinary must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<SkinnedGeometryDisplacementTextureHeaderBinary>, "SkinnedGeometryDisplacementTextureHeaderBinary must stay binary-serializable");

struct SkinnedGeometryMorphHeaderBinary{
    u32 nameOffset = Limit<u32>::s_Max;
    u32 reserved = 0;
    u64 deltaCount = 0;
};
static_assert(IsStandardLayout_V<SkinnedGeometryMorphHeaderBinary>, "SkinnedGeometryMorphHeaderBinary must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<SkinnedGeometryMorphHeaderBinary>, "SkinnedGeometryMorphHeaderBinary must stay binary-serializable");

struct SkinnedGeometryDisplacementBinary{
    u32 texturePathOffset = Limit<u32>::s_Max;
    u32 reserved = 0;
    u32 mode = SkinnedGeometryDisplacementMode::None;
    f32 amplitude = 0.0f;
    f32 bias = 0.0f;
    Float2U uvScale = Float2U(1.0f, 1.0f);
    Float2U uvOffset = Float2U(0.0f, 0.0f);
};
static_assert(IsStandardLayout_V<SkinnedGeometryDisplacementBinary>, "SkinnedGeometryDisplacementBinary must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<SkinnedGeometryDisplacementBinary>, "SkinnedGeometryDisplacementBinary must stay binary-serializable");

[[nodiscard]] inline bool StableTextMatchesName(const CompactString& text, const Name& name){
    return !text.empty() && Name(text.view()) == name;
}

[[nodiscard]] inline SkinnedGeometryDisplacement BuildDisplacement(const SkinnedGeometryDisplacementBinary& binary){
    SkinnedGeometryDisplacement displacement;
    displacement.mode = binary.mode;
    displacement.amplitude = binary.amplitude;
    displacement.bias = binary.bias;
    displacement.uvScale = binary.uvScale;
    displacement.uvOffset = binary.uvOffset;
    return displacement;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename StringTable>
[[nodiscard]] bool BuildDisplacementBinary(
    const SkinnedGeometryDisplacement& displacement,
    const CompactString& texturePathText,
    StringTable& stringTable,
    SkinnedGeometryDisplacementBinary& outBinary){
    outBinary = SkinnedGeometryDisplacementBinary{};
    if(SkinnedGeometryDisplacementModeUsesTexture(displacement.mode)){
        if(!StableTextMatchesName(texturePathText, displacement.texture.name()))
            return false;
        if(!::AppendStringTableText(stringTable, texturePathText.view(), outBinary.texturePathOffset))
            return false;
    }

    outBinary.mode = displacement.mode;
    outBinary.amplitude = displacement.amplitude;
    outBinary.bias = displacement.bias;
    outBinary.uvScale = displacement.uvScale;
    outBinary.uvOffset = displacement.uvOffset;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

