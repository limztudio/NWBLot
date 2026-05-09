// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct alignas(Float4) DeformerVertexMorphRangeGpu{
    u32 firstDelta = 0;
    u32 deltaCount = 0;
    u32 padding0 = 0;
    u32 padding1 = 0;
};
static_assert(
    sizeof(DeformerVertexMorphRangeGpu) == sizeof(f32) * 4u,
    "Deformer vertex morph range GPU layout drifted"
);
static_assert(
    alignof(DeformerVertexMorphRangeGpu) >= alignof(Float4),
    "Deformer vertex morph range GPU layout must stay SIMD-aligned"
);

struct alignas(Float4) DeformerBlendedMorphDeltaGpu{
    Float4 deltaPosition = Float4(0.0f, 0.0f, 0.0f, 0.0f);
    Float4 deltaNormal = Float4(0.0f, 0.0f, 0.0f, 0.0f);
    Float4 deltaTangent = Float4(0.0f, 0.0f, 0.0f, 0.0f);
};
static_assert(
    sizeof(DeformerBlendedMorphDeltaGpu) == sizeof(f32) * 12u,
    "Deformer blended morph delta GPU layout drifted"
);
static_assert(
    alignof(DeformerBlendedMorphDeltaGpu) >= alignof(Float4),
    "Deformer blended morph delta GPU layout must stay SIMD-aligned"
);

struct alignas(Float4) DeformerSkinInfluenceGpu{
    u32 joint[4] = {};
    Float4 weight = Float4(0.0f, 0.0f, 0.0f, 0.0f);
};
static_assert(
    sizeof(DeformerSkinInfluenceGpu) == sizeof(f32) * 8u,
    "Deformer skin influence GPU layout drifted"
);
static_assert(
    alignof(DeformerSkinInfluenceGpu) >= alignof(Float4),
    "Deformer skin influence GPU layout must stay SIMD-aligned"
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

