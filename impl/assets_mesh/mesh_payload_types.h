// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "skinned_mesh_types.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_MeshMissingStreamIndex = Limit<u32>::s_Max;
inline constexpr u32 s_MeshMaxMeshletVertices = 64u;
inline constexpr u32 s_MeshMaxMeshletTriangles = 96u;
inline constexpr u32 s_MeshletConeFlagEnabled = 1u << 0u;

struct MeshletDesc{
    u32 localVertexOffset = 0u;
    u32 positionOffset = 0u;
    u32 attributeOffset = 0u;
    u32 primitiveOffset = 0u;
    u32 counts = 0u;
};
static_assert(IsStandardLayout_V<MeshletDesc>, "MeshletDesc must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<MeshletDesc>, "MeshletDesc must stay binary-serializable");
static_assert(sizeof(MeshletDesc) == sizeof(u32) * 5u, "MeshletDesc layout drifted");

struct MeshletBounds{
    Float4U sphere;
    u32 conePacked = 0u;
    u32 padding0 = 0u;
};
static_assert(IsStandardLayout_V<MeshletBounds>, "MeshletBounds must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<MeshletBounds>, "MeshletBounds must stay binary-serializable");
static_assert(sizeof(MeshletBounds) == sizeof(f32) * 6u, "MeshletBounds layout drifted");

struct MeshletDeformedPositionRef{
    u32 position = s_MeshMissingStreamIndex;
    u32 skin = s_MeshMissingStreamIndex;
};
static_assert(IsStandardLayout_V<MeshletDeformedPositionRef>, "MeshletDeformedPositionRef must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<MeshletDeformedPositionRef>, "MeshletDeformedPositionRef must stay binary-serializable");
static_assert(sizeof(MeshletDeformedPositionRef) == sizeof(u32) * 2u, "MeshletDeformedPositionRef layout drifted");

struct MeshletShadingAttributeRef{
    u32 normal = s_MeshMissingStreamIndex;
    u32 tangent = s_MeshMissingStreamIndex;
    u32 uv0 = s_MeshMissingStreamIndex;
    u32 color = s_MeshMissingStreamIndex;
};
static_assert(IsStandardLayout_V<MeshletShadingAttributeRef>, "MeshletShadingAttributeRef must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<MeshletShadingAttributeRef>, "MeshletShadingAttributeRef must stay binary-serializable");
static_assert(sizeof(MeshletShadingAttributeRef) == sizeof(u32) * 4u, "MeshletShadingAttributeRef layout drifted");

struct MeshletLocalVertexRef{
    u16 localDeformedPosition = 0u;
    u16 localAttribute = 0u;
};
static_assert(IsStandardLayout_V<MeshletLocalVertexRef>, "MeshletLocalVertexRef must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<MeshletLocalVertexRef>, "MeshletLocalVertexRef must stay binary-serializable");
static_assert(sizeof(MeshletLocalVertexRef) == sizeof(u16) * 2u, "MeshletLocalVertexRef layout drifted");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

