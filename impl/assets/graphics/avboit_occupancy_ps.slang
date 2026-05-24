// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "avboit_common.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[vk::binding(0, 1)]] Texture2D<float4> g_OpaqueDepth;
[[vk::binding(1, 1)]] SamplerState g_PointSampler;
[[vk::binding(2, 1)]] RWStructuredBuffer<uint, Std430DataLayout> g_CoverageWords;

struct NwbAvboitOccupancyPixelInput{
    [[vk::location(0)]] float4 color : COLOR0;
};

static float4 inColor;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void main(NwbAvboitOccupancyPixelInput input, float4 fragCoord : SV_Position){
    inColor = input.color;

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
        InterlockedOr(g_CoverageWords[virtualSlice0 >> 5u], 1u << (virtualSlice0 & 31u));
    if(virtualSlice1 != virtualSlice0 && sliceWeight1 > NWB_AVBOIT_SPLAT_WEIGHT_EPSILON)
        InterlockedOr(g_CoverageWords[virtualSlice1 >> 5u], 1u << (virtualSlice1 & 31u));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

