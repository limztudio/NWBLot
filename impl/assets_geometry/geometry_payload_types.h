// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "skinned_geometry_types.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_GeometryMissingStreamIndex = Limit<u32>::s_Max;
inline constexpr u32 s_GeometryMaxMeshletVertices = 64u;
inline constexpr u32 s_GeometryMaxMeshletTriangles = 96u;

struct GeometryVertexRef{
    u32 position = s_GeometryMissingStreamIndex;
    u32 normal = s_GeometryMissingStreamIndex;
    u32 tangent = s_GeometryMissingStreamIndex;
    u32 uv0 = s_GeometryMissingStreamIndex;
    u32 color = s_GeometryMissingStreamIndex;
    u32 skin = s_GeometryMissingStreamIndex;
};
static_assert(IsStandardLayout_V<GeometryVertexRef>, "GeometryVertexRef must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<GeometryVertexRef>, "GeometryVertexRef must stay binary-serializable");
static_assert(sizeof(GeometryVertexRef) == sizeof(u32) * 6u, "GeometryVertexRef layout drifted");

struct GeometryMeshletDesc{
    u32 vertexOffset = 0u;
    u32 primitiveOffset = 0u;
    u32 vertexCount = 0u;
    u32 primitiveCount = 0u;
};
static_assert(IsStandardLayout_V<GeometryMeshletDesc>, "GeometryMeshletDesc must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<GeometryMeshletDesc>, "GeometryMeshletDesc must stay binary-serializable");
static_assert(sizeof(GeometryMeshletDesc) == sizeof(u32) * 4u, "GeometryMeshletDesc layout drifted");

struct GeometryMeshletBounds{
    Float4U sphere;
    Float4U cone;
};
static_assert(IsStandardLayout_V<GeometryMeshletBounds>, "GeometryMeshletBounds must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<GeometryMeshletBounds>, "GeometryMeshletBounds must stay binary-serializable");
static_assert(sizeof(GeometryMeshletBounds) == sizeof(f32) * 8u, "GeometryMeshletBounds layout drifted");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

