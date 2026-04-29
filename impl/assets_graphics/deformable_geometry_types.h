// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"

#include <core/assets/asset_ref.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class DeformableDisplacementTexture;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct alignas(16) DeformableVertexRest{
    Float3U position;
    Float3U normal;
    Float4U tangent;
    Float2U uv0;
    Float4U color0;
};
static_assert(IsStandardLayout_V<DeformableVertexRest>, "DeformableVertexRest must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<DeformableVertexRest>, "DeformableVertexRest must stay binary-serializable");
static_assert(sizeof(DeformableVertexRest) == sizeof(f32) * 16u, "DeformableVertexRest GPU/source layout drifted");
static_assert(alignof(DeformableVertexRest) >= alignof(Float4), "DeformableVertexRest must stay SIMD-aligned");
static_assert(offsetof(DeformableVertexRest, position) == sizeof(f32) * 0u, "DeformableVertexRest position layout drifted");
static_assert(offsetof(DeformableVertexRest, normal) == sizeof(f32) * 3u, "DeformableVertexRest normal layout drifted");
static_assert(offsetof(DeformableVertexRest, tangent) == sizeof(f32) * 6u, "DeformableVertexRest tangent layout drifted");
static_assert(offsetof(DeformableVertexRest, uv0) == sizeof(f32) * 10u, "DeformableVertexRest uv0 layout drifted");
static_assert(offsetof(DeformableVertexRest, color0) == sizeof(f32) * 12u, "DeformableVertexRest color0 layout drifted");

[[nodiscard]] inline SIMDVector LoadRestVertexLane(const DeformableVertexRest& vertex, const usize lane)noexcept{
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

[[nodiscard]] inline SIMDVector LoadRestVertexPosition(const DeformableVertexRest& vertex)noexcept{
    return VectorSetW(LoadRestVertexLane(vertex, 0u), 0.0f);
}

[[nodiscard]] inline SIMDVector LoadRestVertexNormal(const DeformableVertexRest& vertex)noexcept{
    const SIMDVector lane0 = LoadRestVertexLane(vertex, 0u);
    const SIMDVector lane1 = LoadRestVertexLane(vertex, 1u);
    return VectorSetW(VectorPermute<3, 4, 5, 6>(lane0, lane1), 0.0f);
}

[[nodiscard]] inline SIMDVector LoadRestVertexTangent(const DeformableVertexRest& vertex)noexcept{
    const SIMDVector lane1 = LoadRestVertexLane(vertex, 1u);
    const SIMDVector lane2 = LoadRestVertexLane(vertex, 2u);
    return VectorPermute<2, 3, 4, 5>(lane1, lane2);
}

[[nodiscard]] inline SIMDVector LoadRestVertexUv0(const DeformableVertexRest& vertex)noexcept{
    const SIMDVector uv0 = VectorPermute<2, 3, 2, 3>(
        LoadRestVertexLane(vertex, 2u),
        LoadRestVertexLane(vertex, 2u)
    );
    return VectorSetW(VectorSetZ(uv0, 0.0f), 0.0f);
}

[[nodiscard]] inline SIMDVector LoadRestVertexColor0(const DeformableVertexRest& vertex)noexcept{
    return LoadRestVertexLane(vertex, 3u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SkinInfluence4{
    u16 joint[4] = {};
    f32 weight[4] = {};
};
static_assert(IsStandardLayout_V<SkinInfluence4>, "SkinInfluence4 must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<SkinInfluence4>, "SkinInfluence4 must stay binary-serializable");

// Stored vectors are deformer columns; Float44 provides the aligned 4x4 payload layout.
using DeformableJointMatrix = Float44;
static_assert(IsStandardLayout_V<DeformableJointMatrix>, "DeformableJointMatrix must stay GPU-uploadable");
static_assert(IsTriviallyCopyable_V<DeformableJointMatrix>, "DeformableJointMatrix must stay GPU-uploadable");
static_assert(sizeof(DeformableJointMatrix) == sizeof(f32) * 16u, "DeformableJointMatrix GPU layout drifted");
static_assert(alignof(DeformableJointMatrix) >= alignof(Float4), "DeformableJointMatrix must stay SIMD-aligned");

[[nodiscard]] inline DeformableJointMatrix MakeIdentityDeformableJointMatrix(){
    DeformableJointMatrix matrix{};
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


namespace DeformableEditMaskFlag{
    enum Enum : u8{
        Editable = 1u << 0u,
        Restricted = 1u << 1u,
        Forbidden = 1u << 2u,
        RequiresRepair = 1u << 3u,
    };
};
using DeformableEditMaskFlags = u8;

static constexpr DeformableEditMaskFlags s_DeformableEditMaskDefault = DeformableEditMaskFlag::Editable;
static constexpr DeformableEditMaskFlags s_DeformableEditMaskKnownFlags =
    DeformableEditMaskFlag::Editable
    | DeformableEditMaskFlag::Restricted
    | DeformableEditMaskFlag::Forbidden
    | DeformableEditMaskFlag::RequiresRepair
;

[[nodiscard]] inline bool ValidDeformableEditMaskFlags(const DeformableEditMaskFlags flags){
    if(flags == 0u || (flags & ~s_DeformableEditMaskKnownFlags) != 0u)
        return false;

    return (flags & (DeformableEditMaskFlag::Editable | DeformableEditMaskFlag::Forbidden))
        != (DeformableEditMaskFlag::Editable | DeformableEditMaskFlag::Forbidden)
    ;
}

[[nodiscard]] inline bool DeformableEditMaskAllowsCommit(const DeformableEditMaskFlags flags){
    return
        ValidDeformableEditMaskFlags(flags)
        && (flags & DeformableEditMaskFlag::Forbidden) == 0u
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace DeformableDisplacementMode{
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

[[nodiscard]] inline bool DeformableDisplacementModeUsesTexture(const u32 mode){
    return
        mode == DeformableDisplacementMode::ScalarTexture
        || mode == DeformableDisplacementMode::VectorTangentTexture
        || mode == DeformableDisplacementMode::VectorObjectTexture
    ;
}

struct DeformableDisplacement{
    Core::Assets::AssetRef<DeformableDisplacementTexture> texture;
    u32 mode = DeformableDisplacementMode::None;
    f32 amplitude = 0.0f;
    f32 bias = 0.0f;
    Float2U uvScale = Float2U(1.0f, 1.0f);
    Float2U uvOffset = Float2U(0.0f, 0.0f);
};
static_assert(IsStandardLayout_V<DeformableDisplacement>, "DeformableDisplacement must stay layout-stable");
static_assert(IsTriviallyCopyable_V<DeformableDisplacement>, "DeformableDisplacement must stay cheap to copy");

[[nodiscard]] inline bool ValidDeformableDisplacementDescriptor(const DeformableDisplacement& displacement){
    if(
        !IsFinite(displacement.amplitude)
        || !IsFinite(displacement.bias)
        || !IsFinite(displacement.uvScale.x)
        || !IsFinite(displacement.uvScale.y)
        || !IsFinite(displacement.uvOffset.x)
        || !IsFinite(displacement.uvOffset.y)
    )
        return false;

    if(displacement.mode == DeformableDisplacementMode::None){
        return
            displacement.amplitude == 0.0f
            && displacement.bias == 0.0f
            && !displacement.texture.valid()
        ;
    }
    if(displacement.mode == DeformableDisplacementMode::ScalarUvRamp)
        return !displacement.texture.valid() && displacement.bias == 0.0f;
    if(DeformableDisplacementModeUsesTexture(displacement.mode))
        return displacement.texture.valid();

    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct DeformableMorphDelta{
    u32 vertexId = 0;
    Float3U deltaPosition;
    Float3U deltaNormal;
    Float4U deltaTangent;
};
static_assert(IsStandardLayout_V<DeformableMorphDelta>, "DeformableMorphDelta must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<DeformableMorphDelta>, "DeformableMorphDelta must stay binary-serializable");

struct DeformableMorph{
    Name name = NAME_NONE;
    CompactString nameText;
    Vector<DeformableMorphDelta> deltas;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

