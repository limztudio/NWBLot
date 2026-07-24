// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "skin_types.h"

#include "meshlet_constants.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_MeshMissingStreamIndex = NWB_MESH_MISSING_STREAM_INDEX;
inline constexpr u32 s_MeshMaxMeshletVertices = NWB_MESH_SHADER_MAX_VERTICES;
inline constexpr u32 s_MeshMaxMeshletTriangles = NWB_MESH_SHADER_MAX_TRIANGLES;
inline constexpr u32 s_MeshletTriangleIndexCount = NWB_MESHLET_TRIANGLE_INDEX_COUNT;
inline constexpr u32 s_MeshletConeFlagEnabled = NWB_MESHLET_CONE_FLAG_ENABLED;

struct MeshletDesc{
    u32 localVertexOffset = 0u;
    u32 primitiveOffset = 0u;
    u32 positionRefOffset = 0u;
    u32 attributeRefOffset = 0u;
    u32 counts = 0u;
    u32 positionBase = 0u;
    u32 skinBase = s_MeshMissingStreamIndex;
    u32 normalBase = 0u;
    u32 tangentBase = 0u;
    u32 uv0Base = 0u;
    u32 colorBase = 0u;
    u32 encoding = 0u;
};
static_assert(IsStandardLayout_V<MeshletDesc>, "MeshletDesc must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<MeshletDesc>, "MeshletDesc must stay binary-serializable");
static_assert(sizeof(MeshletDesc) == sizeof(u32) * 12u, "MeshletDesc layout drifted");

struct MeshletBounds{
    Float4U sphere;
    u32 conePacked = 0u;
    u32 padding0 = 0u;
};
static_assert(IsStandardLayout_V<MeshletBounds>, "MeshletBounds must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<MeshletBounds>, "MeshletBounds must stay binary-serializable");
static_assert(sizeof(MeshletBounds) == NWB_MESHLET_BOUNDS_STRIDE, "MeshletBounds layout drifted");
static_assert(offsetof(MeshletBounds, sphere) == NWB_MESHLET_BOUNDS_SPHERE_BYTE_OFFSET, "MeshletBounds sphere offset drifted");
static_assert(offsetof(MeshletBounds, conePacked) == NWB_MESHLET_BOUNDS_CONE_BYTE_OFFSET, "MeshletBounds cone offset drifted");
static_assert(offsetof(MeshletBounds, padding0) == NWB_MESHLET_BOUNDS_PADDING_BYTE_OFFSET, "MeshletBounds padding offset drifted");

struct MeshletPositionStreamRef{
    u32 position = s_MeshMissingStreamIndex;
    u32 skin = s_MeshMissingStreamIndex;
};
static_assert(IsStandardLayout_V<MeshletPositionStreamRef>, "MeshletPositionStreamRef must stay standard-layout");
static_assert(IsTriviallyCopyable_V<MeshletPositionStreamRef>, "MeshletPositionStreamRef must stay trivially copyable");
static_assert(sizeof(MeshletPositionStreamRef) == sizeof(u32) * 2u, "MeshletPositionStreamRef layout drifted");

struct MeshletAttributeStreamRef{
    u32 normal = s_MeshMissingStreamIndex;
    u32 tangent = s_MeshMissingStreamIndex;
    u32 uv0 = s_MeshMissingStreamIndex;
    u32 color = s_MeshMissingStreamIndex;
};
static_assert(IsStandardLayout_V<MeshletAttributeStreamRef>, "MeshletAttributeStreamRef must stay standard-layout");
static_assert(IsTriviallyCopyable_V<MeshletAttributeStreamRef>, "MeshletAttributeStreamRef must stay trivially copyable");
static_assert(sizeof(MeshletAttributeStreamRef) == sizeof(u32) * 4u, "MeshletAttributeStreamRef layout drifted");

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

