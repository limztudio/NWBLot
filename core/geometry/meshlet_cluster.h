// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_GEOMETRY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct MeshletBuildConfig{
    u32 maxVertices = 64;
    u32 maxTriangles = 126;
};
static_assert(IsStandardLayout_V<MeshletBuildConfig>, "MeshletBuildConfig must stay layout-stable");
static_assert(IsTriviallyCopyable_V<MeshletBuildConfig>, "MeshletBuildConfig must stay cheap to copy");

struct MeshletBounds{
    Float3U minimum = Float3U(0.0f, 0.0f, 0.0f);
    Float3U maximum = Float3U(0.0f, 0.0f, 0.0f);
    Float3U center = Float3U(0.0f, 0.0f, 0.0f);
    f32 radius = 0.0f;
};
static_assert(IsStandardLayout_V<MeshletBounds>, "MeshletBounds must stay layout-stable");
static_assert(IsTriviallyCopyable_V<MeshletBounds>, "MeshletBounds must stay cheap to copy");

struct MeshletCluster{
    u32 firstVertex = 0;
    u32 vertexCount = 0;
    u32 firstIndex = 0;
    u32 indexCount = 0;
    MeshletBounds bounds;
};
static_assert(IsStandardLayout_V<MeshletCluster>, "MeshletCluster must stay layout-stable");
static_assert(IsTriviallyCopyable_V<MeshletCluster>, "MeshletCluster must stay cheap to copy");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool BuildMeshlets(
    const Vector<Float3U>& positions,
    const Vector<u32>& indices,
    const MeshletBuildConfig& config,
    Vector<MeshletCluster>& outMeshlets,
    Vector<u32>& outVertexIndices,
    Vector<u32>& outLocalIndices
);

[[nodiscard]] bool ComputeMeshletDeformationBounds(
    const Vector<Float3U>& positions,
    const Vector<u32>& meshletVertexIndices,
    const MeshletCluster& meshlet,
    const Vector<f32>& vertexExpansionRadii,
    f32 uniformExpansionRadius,
    MeshletBounds& outBounds
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_GEOMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

