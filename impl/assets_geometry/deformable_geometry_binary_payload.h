// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "deformable_geometry_types.h"

#include <global/binary.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace DeformableGeometryBinaryPayload{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_DeformableGeometryMagic = 0x44474F31u; // DGO1
inline constexpr u32 s_DeformableGeometryVersion = 1u;
inline constexpr u32 s_DeformableDisplacementTextureMagic = 0x44445431u; // DDT1
inline constexpr u32 s_DeformableDisplacementTextureVersion = 1u;
inline constexpr u32 s_DeformableSkeletonJointLimit = static_cast<u32>(Limit<u16>::s_Max) + 1u;

#pragma pack(push, 1)
struct DeformableGeometryHeaderBinary{
    u32 magic = s_DeformableGeometryMagic;
    u32 version = s_DeformableGeometryVersion;
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
struct DeformableDisplacementTextureHeaderBinary{
    u32 magic = s_DeformableDisplacementTextureMagic;
    u32 version = s_DeformableDisplacementTextureVersion;
    u32 width = 0;
    u32 height = 0;
    u64 texelCount = 0;
};
#pragma pack(pop)
static_assert(sizeof(DeformableGeometryHeaderBinary) == sizeof(u32) + sizeof(u32) + sizeof(u32) + (sizeof(u64) * 9u), "DeformableGeometryHeaderBinary layout drifted");
static_assert(sizeof(DeformableDisplacementTextureHeaderBinary) == sizeof(u32) + sizeof(u32) + sizeof(u32) + sizeof(u32) + sizeof(u64), "DeformableDisplacementTextureHeaderBinary layout drifted");
static_assert(alignof(DeformableGeometryHeaderBinary) == 1u, "DeformableGeometryHeaderBinary must stay packed");
static_assert(alignof(DeformableDisplacementTextureHeaderBinary) == 1u, "DeformableDisplacementTextureHeaderBinary must stay packed");
static_assert(IsStandardLayout_V<DeformableGeometryHeaderBinary>, "DeformableGeometryHeaderBinary must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<DeformableGeometryHeaderBinary>, "DeformableGeometryHeaderBinary must stay binary-serializable");
static_assert(IsStandardLayout_V<DeformableDisplacementTextureHeaderBinary>, "DeformableDisplacementTextureHeaderBinary must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<DeformableDisplacementTextureHeaderBinary>, "DeformableDisplacementTextureHeaderBinary must stay binary-serializable");

struct DeformableMorphHeaderBinary{
    u32 nameOffset = Limit<u32>::s_Max;
    u32 reserved = 0;
    u64 deltaCount = 0;
};
static_assert(IsStandardLayout_V<DeformableMorphHeaderBinary>, "DeformableMorphHeaderBinary must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<DeformableMorphHeaderBinary>, "DeformableMorphHeaderBinary must stay binary-serializable");

struct DeformableDisplacementBinary{
    u32 texturePathOffset = Limit<u32>::s_Max;
    u32 reserved = 0;
    u32 mode = DeformableDisplacementMode::None;
    f32 amplitude = 0.0f;
    f32 bias = 0.0f;
    Float2U uvScale = Float2U(1.0f, 1.0f);
    Float2U uvOffset = Float2U(0.0f, 0.0f);
};
static_assert(IsStandardLayout_V<DeformableDisplacementBinary>, "DeformableDisplacementBinary must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<DeformableDisplacementBinary>, "DeformableDisplacementBinary must stay binary-serializable");

[[nodiscard]] inline bool StableTextMatchesName(const CompactString& text, const Name& name){
    return !text.empty() && Name(text.view()) == name;
}

[[nodiscard]] inline DeformableDisplacement BuildDisplacement(const DeformableDisplacementBinary& binary){
    DeformableDisplacement displacement;
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
    const DeformableDisplacement& displacement,
    const CompactString& texturePathText,
    StringTable& stringTable,
    DeformableDisplacementBinary& outBinary){
    outBinary = DeformableDisplacementBinary{};
    if(DeformableDisplacementModeUsesTexture(displacement.mode)){
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

