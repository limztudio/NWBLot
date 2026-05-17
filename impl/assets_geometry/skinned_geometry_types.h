// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"
#include "geometry_class.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct alignas(16) SkinnedGeometryVertex{
    Float3U position;
    Float3U normal;
    Float4U tangent;
    Float2U uv0;
    Float4U color0;
};
static_assert(IsStandardLayout_V<SkinnedGeometryVertex>, "SkinnedGeometryVertex must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<SkinnedGeometryVertex>, "SkinnedGeometryVertex must stay binary-serializable");
static_assert(sizeof(SkinnedGeometryVertex) == sizeof(f32) * 16u, "SkinnedGeometryVertex GPU/source layout drifted");
static_assert(alignof(SkinnedGeometryVertex) >= alignof(Float4), "SkinnedGeometryVertex must stay SIMD-aligned");
static_assert(offsetof(SkinnedGeometryVertex, position) == sizeof(f32) * 0u, "SkinnedGeometryVertex position layout drifted");
static_assert(offsetof(SkinnedGeometryVertex, normal) == sizeof(f32) * 3u, "SkinnedGeometryVertex normal layout drifted");
static_assert(offsetof(SkinnedGeometryVertex, tangent) == sizeof(f32) * 6u, "SkinnedGeometryVertex tangent layout drifted");
static_assert(offsetof(SkinnedGeometryVertex, uv0) == sizeof(f32) * 10u, "SkinnedGeometryVertex uv0 layout drifted");
static_assert(offsetof(SkinnedGeometryVertex, color0) == sizeof(f32) * 12u, "SkinnedGeometryVertex color0 layout drifted");

[[nodiscard]] inline SIMDVector LoadSkinnedGeometryVertexLane(const SkinnedGeometryVertex& vertex, const usize lane)noexcept{
    NWB_ASSERT(lane < 4u);
    const f32* values = vertex.position.raw + (lane * 4u);
#if defined(NWB_HAS_SCALAR)
    return VectorSet(values[0], values[1], values[2], values[3]);
#elif defined(NWB_HAS_NEON)
#if defined(_MSC_VER) && !defined(__clang__) && !defined(_ARM64_DISTINCT_NEON_TYPES)
    return vld1q_f32_ex(values, 128);
#else
    return vld1q_f32(values);
#endif
#elif defined(NWB_HAS_SSE4)
    return _mm_load_ps(values);
#endif
}

[[nodiscard]] inline SIMDVector LoadSkinnedGeometryVertexPosition(const SkinnedGeometryVertex& vertex)noexcept{
    return VectorSetW(LoadSkinnedGeometryVertexLane(vertex, 0u), 0.0f);
}

[[nodiscard]] inline SIMDVector LoadSkinnedGeometryVertexNormal(const SkinnedGeometryVertex& vertex)noexcept{
    const SIMDVector lane0 = LoadSkinnedGeometryVertexLane(vertex, 0u);
    const SIMDVector lane1 = LoadSkinnedGeometryVertexLane(vertex, 1u);
    return VectorSetW(VectorPermute<3, 4, 5, 6>(lane0, lane1), 0.0f);
}

[[nodiscard]] inline SIMDVector LoadSkinnedGeometryVertexTangent(const SkinnedGeometryVertex& vertex)noexcept{
    const SIMDVector lane1 = LoadSkinnedGeometryVertexLane(vertex, 1u);
    const SIMDVector lane2 = LoadSkinnedGeometryVertexLane(vertex, 2u);
    return VectorPermute<2, 3, 4, 5>(lane1, lane2);
}

[[nodiscard]] inline SIMDVector LoadSkinnedGeometryVertexUv0(const SkinnedGeometryVertex& vertex)noexcept{
    const SIMDVector uv0 = VectorPermute<2, 3, 2, 3>(
        LoadSkinnedGeometryVertexLane(vertex, 2u),
        LoadSkinnedGeometryVertexLane(vertex, 2u)
    );
    return VectorSetW(VectorSetZ(uv0, 0.0f), 0.0f);
}

[[nodiscard]] inline SIMDVector LoadSkinnedGeometryVertexColor0(const SkinnedGeometryVertex& vertex)noexcept{
    return LoadSkinnedGeometryVertexLane(vertex, 3u);
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

