// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"
#include "geometry_class.h"

#include <core/assets/asset_ref.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class SkinnedGeometryDisplacementTexture;


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

struct SourceSample{
    u32 sourceTri = 0;
    f32 bary[3] = {};
};
static_assert(IsStandardLayout_V<SourceSample>, "SourceSample must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<SourceSample>, "SourceSample must stay binary-serializable");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SkinnedGeometryEditMaskFlag{
    enum Enum : u8{
        Editable = 1u << 0u,
        Restricted = 1u << 1u,
        Forbidden = 1u << 2u,
        RequiresRepair = 1u << 3u,
    };
};
using SkinnedGeometryEditMaskFlags = u8;

static constexpr SkinnedGeometryEditMaskFlags s_SkinnedGeometryEditMaskDefault = SkinnedGeometryEditMaskFlag::Editable;
static constexpr SkinnedGeometryEditMaskFlags s_SkinnedGeometryEditMaskKnownFlags =
    SkinnedGeometryEditMaskFlag::Editable
    | SkinnedGeometryEditMaskFlag::Restricted
    | SkinnedGeometryEditMaskFlag::Forbidden
    | SkinnedGeometryEditMaskFlag::RequiresRepair
;

[[nodiscard]] inline bool ValidSkinnedGeometryEditMaskFlags(const SkinnedGeometryEditMaskFlags flags){
    if(flags == 0u || (flags & ~s_SkinnedGeometryEditMaskKnownFlags) != 0u)
        return false;

    return (flags & (SkinnedGeometryEditMaskFlag::Editable | SkinnedGeometryEditMaskFlag::Forbidden))
        != (SkinnedGeometryEditMaskFlag::Editable | SkinnedGeometryEditMaskFlag::Forbidden)
    ;
}

[[nodiscard]] inline bool SkinnedGeometryEditMaskAllowsCommit(const SkinnedGeometryEditMaskFlags flags){
    return
        ValidSkinnedGeometryEditMaskFlags(flags)
        && (flags & SkinnedGeometryEditMaskFlag::Forbidden) == 0u
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SkinnedGeometryDisplacementMode{
    enum Enum : u32{
        None = 0,
        ScalarUvRamp = 1,
        ScalarTexture = 2,
        VectorTangentTexture = 3,
        VectorObjectTexture = 4,
        PoseDrivenScalar = 5,
        PoseDrivenVector = 6,
    };
};

[[nodiscard]] inline bool SkinnedGeometryDisplacementModeUsesTexture(const u32 mode){
    return
        mode == SkinnedGeometryDisplacementMode::ScalarTexture
        || mode == SkinnedGeometryDisplacementMode::VectorTangentTexture
        || mode == SkinnedGeometryDisplacementMode::VectorObjectTexture
    ;
}

struct SkinnedGeometryDisplacement{
    Core::Assets::AssetRef<SkinnedGeometryDisplacementTexture> texture;
    u32 mode = SkinnedGeometryDisplacementMode::None;
    f32 amplitude = 0.0f;
    f32 bias = 0.0f;
    Float2U uvScale = Float2U(1.0f, 1.0f);
    Float2U uvOffset = Float2U(0.0f, 0.0f);
};
static_assert(IsStandardLayout_V<SkinnedGeometryDisplacement>, "SkinnedGeometryDisplacement must stay layout-stable");
static_assert(IsTriviallyCopyable_V<SkinnedGeometryDisplacement>, "SkinnedGeometryDisplacement must stay cheap to copy");

[[nodiscard]] inline bool ValidSkinnedGeometryDisplacementDescriptor(const SkinnedGeometryDisplacement& displacement){
    if(
        !IsFinite(displacement.amplitude)
        || !IsFinite(displacement.bias)
        || !IsFinite(displacement.uvScale.x)
        || !IsFinite(displacement.uvScale.y)
        || !IsFinite(displacement.uvOffset.x)
        || !IsFinite(displacement.uvOffset.y)
    )
        return false;

    if(displacement.mode == SkinnedGeometryDisplacementMode::None){
        return
            displacement.amplitude == 0.0f
            && displacement.bias == 0.0f
            && !displacement.texture.valid()
        ;
    }
    if(displacement.mode == SkinnedGeometryDisplacementMode::ScalarUvRamp)
        return !displacement.texture.valid() && displacement.bias == 0.0f;
    if(SkinnedGeometryDisplacementModeUsesTexture(displacement.mode))
        return displacement.texture.valid();

    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SkinnedGeometryMorphDelta{
    u32 vertexId = 0;
    Float3U deltaPosition;
    Float3U deltaNormal;
    Float4U deltaTangent;
};
static_assert(IsStandardLayout_V<SkinnedGeometryMorphDelta>, "SkinnedGeometryMorphDelta must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<SkinnedGeometryMorphDelta>, "SkinnedGeometryMorphDelta must stay binary-serializable");

struct SkinnedGeometryMorph{
    Name name = NAME_NONE;
    CompactString nameText;
    Vector<SkinnedGeometryMorphDelta> deltas;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

