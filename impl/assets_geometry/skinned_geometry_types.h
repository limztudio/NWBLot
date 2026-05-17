// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"
#include "geometry_class.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct SkinnedGeometryVertex{
    Float3U position;
    Half4U normal;
    Half4U tangent;
    Float2U uv0;
    Half4U color0;
};
static_assert(IsStandardLayout_V<SkinnedGeometryVertex>, "SkinnedGeometryVertex must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<SkinnedGeometryVertex>, "SkinnedGeometryVertex must stay binary-serializable");
static_assert(sizeof(SkinnedGeometryVertex) == sizeof(u32) * 11u, "SkinnedGeometryVertex GPU/source layout drifted");
static_assert(offsetof(SkinnedGeometryVertex, position) == sizeof(u32) * 0u, "SkinnedGeometryVertex position layout drifted");
static_assert(offsetof(SkinnedGeometryVertex, normal) == sizeof(u32) * 3u, "SkinnedGeometryVertex normal layout drifted");
static_assert(offsetof(SkinnedGeometryVertex, tangent) == sizeof(u32) * 5u, "SkinnedGeometryVertex tangent layout drifted");
static_assert(offsetof(SkinnedGeometryVertex, uv0) == sizeof(u32) * 7u, "SkinnedGeometryVertex uv0 layout drifted");
static_assert(offsetof(SkinnedGeometryVertex, color0) == sizeof(u32) * 9u, "SkinnedGeometryVertex color0 layout drifted");

[[nodiscard]] inline SIMDVector LoadSkinnedGeometryVertexPosition(const SkinnedGeometryVertex& vertex)noexcept{
    return VectorSetW(LoadFloat(vertex.position), 0.0f);
}

[[nodiscard]] inline SIMDVector LoadSkinnedGeometryVertexNormal(const SkinnedGeometryVertex& vertex)noexcept{
    const Float4U normal = LoadHalf4U(vertex.normal);
    return VectorSetW(LoadFloat(normal), 0.0f);
}

[[nodiscard]] inline SIMDVector LoadSkinnedGeometryVertexTangent(const SkinnedGeometryVertex& vertex)noexcept{
    return LoadFloat(LoadHalf4U(vertex.tangent));
}

[[nodiscard]] inline SIMDVector LoadSkinnedGeometryVertexUv0(const SkinnedGeometryVertex& vertex)noexcept{
    return VectorSetW(VectorSetZ(LoadFloat(vertex.uv0), 0.0f), 0.0f);
}

[[nodiscard]] inline SIMDVector LoadSkinnedGeometryVertexColor0(const SkinnedGeometryVertex& vertex)noexcept{
    return LoadFloat(LoadHalf4U(vertex.color0));
}

inline void StoreSkinnedGeometryVertexNormal(SkinnedGeometryVertex& vertex, const Float3U& normal)noexcept{
    vertex.normal = MakeHalf4U(normal.x, normal.y, normal.z, 0.0f);
}

inline void StoreSkinnedGeometryVertexTangent(SkinnedGeometryVertex& vertex, const Float4U& tangent)noexcept{
    vertex.tangent = MakeHalf4U(tangent.x, tangent.y, tangent.z, tangent.w);
}

inline void StoreSkinnedGeometryVertexColor0(SkinnedGeometryVertex& vertex, const Float4U& color0)noexcept{
    vertex.color0 = MakeHalf4U(color0.x, color0.y, color0.z, color0.w);
}

[[nodiscard]] inline SkinnedGeometryVertex MakeSkinnedGeometryVertex(
    const Float3U& position,
    const Float3U& normal,
    const Float4U& tangent,
    const Float2U& uv0,
    const Float4U& color0
)noexcept{
    SkinnedGeometryVertex vertex;
    vertex.position = position;
    StoreSkinnedGeometryVertexNormal(vertex, normal);
    StoreSkinnedGeometryVertexTangent(vertex, tangent);
    vertex.uv0 = uv0;
    StoreSkinnedGeometryVertexColor0(vertex, color0);
    return vertex;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SkinInfluence4{
    u16 joint[4] = {};
    f32 weight[4] = {};
};
static_assert(IsStandardLayout_V<SkinInfluence4>, "SkinInfluence4 must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<SkinInfluence4>, "SkinInfluence4 must stay binary-serializable");

// Stored vectors are skinned geometry columns; Float44 provides the aligned 4x4 payload layout.
using SkinnedGeometryJointMatrix = Float44;
static_assert(IsStandardLayout_V<SkinnedGeometryJointMatrix>, "SkinnedGeometryJointMatrix must stay GPU-uploadable");
static_assert(IsTriviallyCopyable_V<SkinnedGeometryJointMatrix>, "SkinnedGeometryJointMatrix must stay GPU-uploadable");
static_assert(sizeof(SkinnedGeometryJointMatrix) == sizeof(f32) * 16u, "SkinnedGeometryJointMatrix GPU layout drifted");
static_assert(alignof(SkinnedGeometryJointMatrix) >= alignof(Float4), "SkinnedGeometryJointMatrix must stay SIMD-aligned");

[[nodiscard]] inline SkinnedGeometryJointMatrix MakeIdentitySkinnedGeometryJointMatrix(){
    SkinnedGeometryJointMatrix matrix{};
    matrix.rows[0] = Float4(1.0f, 0.0f, 0.0f, 0.0f);
    matrix.rows[1] = Float4(0.0f, 1.0f, 0.0f, 0.0f);
    matrix.rows[2] = Float4(0.0f, 0.0f, 1.0f, 0.0f);
    matrix.rows[3] = Float4(0.0f, 0.0f, 0.0f, 1.0f);
    return matrix;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

