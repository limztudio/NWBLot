// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_CSG_ENABLED 1
#define NWB_CSG_CLIP_SET 2

#include "../avboit/common.slangi"
#include "../avboit/binding_slots.h"
#include "transparent_cap_avboit_common.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[vk::binding(NWB_AVBOIT_BINDING_OPAQUE_DEPTH, NWB_AVBOIT_TRANSPARENT_SET)]] Texture2D<float4> g_OpaqueDepth;
[[vk::binding(NWB_AVBOIT_BINDING_POINT_SAMPLER, NWB_AVBOIT_TRANSPARENT_SET)]] SamplerState g_PointSampler;
[[vk::binding(NWB_AVBOIT_OCCUPANCY_BINDING_COVERAGE_WORDS, NWB_AVBOIT_TRANSPARENT_SET)]] RWStructuredBuffer<uint, Std430DataLayout> g_CoverageWords;

static float4 inColor;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void main(NwbCsgTransparentCapAvboitPixelInput input, float4 fragCoord : SV_Position){
    inColor = input.color;
    if(nwbCsgTransparentCapPixelDiscard(input))
        discard;

    const float alpha = nwbAvboitResolveFragmentAlpha(input.color);
    uint virtualSlice;
    if(!nwbAvboitAcceptLowDepthFragment(g_OpaqueDepth, g_PointSampler, fragCoord, alpha, virtualSlice))
        discard;

    uint virtualSlice0;
    uint virtualSlice1;
    float sliceWeight0;
    float sliceWeight1;
    nwbAvboitLinearVirtualSlicesFromDepth(fragCoord.z, virtualSlice0, virtualSlice1, sliceWeight0, sliceWeight1);

    if(sliceWeight0 > NWB_AVBOIT_SPLAT_WEIGHT_EPSILON)
        InterlockedOr(g_CoverageWords[virtualSlice0 >> NWB_AVBOIT_COVERAGE_WORD_SHIFT], 1u << (virtualSlice0 & NWB_AVBOIT_COVERAGE_BIT_MASK));
    if(virtualSlice1 != virtualSlice0 && sliceWeight1 > NWB_AVBOIT_SPLAT_WEIGHT_EPSILON)
        InterlockedOr(g_CoverageWords[virtualSlice1 >> NWB_AVBOIT_COVERAGE_WORD_SHIFT], 1u << (virtualSlice1 & NWB_AVBOIT_COVERAGE_BIT_MASK));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

