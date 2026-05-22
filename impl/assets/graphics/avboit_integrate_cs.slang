// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "avboit_compute_common.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[vk::binding(0, 0)]] StructuredBuffer<uint, Std430DataLayout> g_Extinction;
[[vk::binding(1, 0), vk::image_format("r16f")]] RWTexture3D<float> g_Transmittance;
[[vk::binding(2, 0)]] StructuredBuffer<uint, Std430DataLayout> g_Control;
[[vk::binding(3, 0)]] StructuredBuffer<uint, Std430DataLayout> g_ExtinctionOverflowDepth;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void nwbAvboitIntegrateMain(uint lowPixelIndex){
    const uint lowWidth = nwbAvboitLowWidth();
    const uint lowHeight = nwbAvboitLowHeight();
    const uint lowPixelCount = lowWidth * lowHeight;
    if(lowPixelIndex >= lowPixelCount)
        return;

    const uint physicalSliceCount = nwbAvboitPhysicalSliceCount();
    const uint activePhysicalSlices = clamp(g_Control[0], 1u, physicalSliceCount);
    const uint2 lowPixel = uint2(lowPixelIndex % lowWidth, lowPixelIndex / lowWidth);
    const uint overflowSlice = g_ExtinctionOverflowDepth[lowPixelIndex];
    const float saturatedExtinction = 16.0;
    float accumulatedExtinction = 0.0;
    uint cachedWordIndex = NWB_AVBOIT_OVERFLOW_INVALID;
    uint cachedPackedWord = 0u;

    for(uint physicalSlice = 0u; physicalSlice < physicalSliceCount; ++physicalSlice){
        const float transmittance = exp(-min(accumulatedExtinction, saturatedExtinction));
        g_Transmittance[uint3(lowPixel.x, lowPixel.y, physicalSlice)] = transmittance;

        if(physicalSlice >= activePhysicalSlices || accumulatedExtinction >= saturatedExtinction)
            continue;

        if(overflowSlice != NWB_AVBOIT_OVERFLOW_INVALID && physicalSlice >= overflowSlice){
            accumulatedExtinction = saturatedExtinction;
            continue;
        }

        const uint wordIndex = nwbAvboitExtinctionWordIndex(lowPixel, physicalSlice);
        if(wordIndex >= nwbAvboitExtinctionWordCount())
            continue;

        if(wordIndex != cachedWordIndex){
            cachedPackedWord = g_Extinction[wordIndex];
            cachedWordIndex = wordIndex;
        }
        accumulatedExtinction = min(
            accumulatedExtinction + nwbAvboitUnpackExtinctionByte(cachedPackedWord, physicalSlice),
            saturatedExtinction
        );
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(64, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    nwbAvboitIntegrateMain(dispatchThreadID.x);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

