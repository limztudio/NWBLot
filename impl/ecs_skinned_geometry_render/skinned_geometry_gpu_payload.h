// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct alignas(Float4) SkinnedGeometryVertexMorphRangeGpu{
    u32 firstDelta = 0;
    u32 deltaCount = 0;
    u32 padding0 = 0;
    u32 padding1 = 0;
};
static_assert(
    sizeof(SkinnedGeometryVertexMorphRangeGpu) == sizeof(f32) * 4u,
    "SkinnedGeometry vertex morph range GPU layout drifted"
);
static_assert(
    alignof(SkinnedGeometryVertexMorphRangeGpu) >= alignof(Float4),
    "SkinnedGeometry vertex morph range GPU layout must stay SIMD-aligned"
);

struct alignas(Float4) SkinnedGeometryBlendedMorphDeltaGpu{
    Float4 deltaPosition = Float4(0.0f, 0.0f, 0.0f, 0.0f);
    Float4 deltaNormal = Float4(0.0f, 0.0f, 0.0f, 0.0f);
    Float4 deltaTangent = Float4(0.0f, 0.0f, 0.0f, 0.0f);
};
static_assert(
    sizeof(SkinnedGeometryBlendedMorphDeltaGpu) == sizeof(f32) * 12u,
    "SkinnedGeometry blended morph delta GPU layout drifted"
);
static_assert(
    alignof(SkinnedGeometryBlendedMorphDeltaGpu) >= alignof(Float4),
    "SkinnedGeometry blended morph delta GPU layout must stay SIMD-aligned"
);

struct alignas(Float4) SkinnedGeometrySkinInfluenceGpu{
    u32 joint[4] = {};
    Float4 weight = Float4(0.0f, 0.0f, 0.0f, 0.0f);
};
static_assert(
    sizeof(SkinnedGeometrySkinInfluenceGpu) == sizeof(f32) * 8u,
    "SkinnedGeometry skin influence GPU layout drifted"
);
static_assert(
    alignof(SkinnedGeometrySkinInfluenceGpu) >= alignof(Float4),
    "SkinnedGeometry skin influence GPU layout must stay SIMD-aligned"
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

