// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "common.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[vk::binding(0, 1)]] Texture2D<float4> g_OpaqueDepth;
[[vk::binding(1, 1)]] SamplerState g_PointSampler;
[[vk::binding(2, 1)]] StructuredBuffer<uint, Std430DataLayout> g_DepthWarp;
[[vk::binding(3, 1)]] StructuredBuffer<uint, Std430DataLayout> g_Control;
[[vk::binding(4, 1)]] RWStructuredBuffer<uint, Std430DataLayout> g_Extinction;
[[vk::binding(5, 1)]] RWStructuredBuffer<uint, Std430DataLayout> g_ExtinctionOverflowDepth;

struct NwbAvboitExtinctionPixelInput{
    [[vk::location(0)]] float4 color : COLOR0;
};

static float4 inColor;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void nwbAvboitSplatExtinction(uint2 lowPixel, uint virtualSlice, float extinction){
    if(extinction <= 0.0)
        return;

    const uint activePhysicalSlices = max(min(g_Control[0], nwbAvboitPhysicalSliceCount()), 1u);
    const uint physicalSlice = min(g_DepthWarp[virtualSlice], activePhysicalSlices - 1u);
    const uint wordIndex = nwbAvboitExtinctionWordIndex(lowPixel, physicalSlice);
    if(wordIndex >= nwbAvboitExtinctionWordCount())
        return;

    const uint packedExtinction = nwbAvboitPackExtinctionByte(extinction);
    if(packedExtinction == 0u)
        return;

    const uint byteShift = nwbAvboitExtinctionByteShift(physicalSlice);
    uint previousWord;
    InterlockedAdd(g_Extinction[wordIndex], packedExtinction << byteShift, previousWord);
    const uint previousByte = (previousWord >> byteShift) & NWB_AVBOIT_EXTINCTION_BYTE_MASK;
    const bool byteOverflow = previousByte + packedExtinction > NWB_AVBOIT_EXTINCTION_BYTE_MASK;
    const float previousExtinction = float(previousByte) / nwbAvboitExtinctionFixedScale();
    const bool rawOverflow = previousExtinction + extinction > nwbAvboitExtinctionSaturation();
    if(byteOverflow || rawOverflow)
        InterlockedMin(g_ExtinctionOverflowDepth[nwbAvboitLowPixelIndex(lowPixel)], physicalSlice);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void main(NwbAvboitExtinctionPixelInput input, float4 fragCoord : SV_Position){
    inColor = input.color;

    const float alpha = nwbAvboitResolveFragmentAlpha(input.color);
    uint virtualSlice;
    if(!nwbAvboitAcceptLowDepthFragment(g_OpaqueDepth, g_PointSampler, fragCoord, alpha, virtualSlice))
        discard;

    const uint2 lowPixel = nwbAvboitLowPixelFromLowFragCoord(fragCoord.xy);
    const float extinction = nwbAvboitExtinctionFromAlpha(alpha);

    uint virtualSlice0;
    uint virtualSlice1;
    float sliceWeight0;
    float sliceWeight1;
    nwbAvboitLinearVirtualSlicesFromDepth(fragCoord.z, virtualSlice0, virtualSlice1, sliceWeight0, sliceWeight1);

    if(sliceWeight0 > NWB_AVBOIT_SPLAT_WEIGHT_EPSILON)
        nwbAvboitSplatExtinction(lowPixel, virtualSlice0, extinction * sliceWeight0);
    if(virtualSlice1 != virtualSlice0 && sliceWeight1 > NWB_AVBOIT_SPLAT_WEIGHT_EPSILON)
        nwbAvboitSplatExtinction(lowPixel, virtualSlice1, extinction * sliceWeight1);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

