// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#version 460

#include "avboit_compute_common.glsli"

layout(local_size_x = 64) in;

layout(std430, binding = 0) readonly buffer NwbAvboitExtinctionBuffer{
    uint g_Extinction[];
};

layout(r16f, binding = 1) writeonly uniform image3D g_Transmittance;

layout(std430, binding = 2) readonly buffer NwbAvboitControlBuffer{
    uint g_Control[];
};

layout(std430, binding = 3) readonly buffer NwbAvboitExtinctionOverflowDepthBuffer{
    uint g_ExtinctionOverflowDepth[];
};

void main(){
    const uint lowPixelIndex = gl_GlobalInvocationID.x;
    const uint lowWidth = nwbAvboitLowWidth();
    const uint lowHeight = nwbAvboitLowHeight();
    const uint lowPixelCount = lowWidth * lowHeight;
    if(lowPixelIndex >= lowPixelCount)
        return;

    const uint physicalSliceCount = nwbAvboitPhysicalSliceCount();
    const uint activePhysicalSlices = clamp(g_Control[0], 1u, physicalSliceCount);
    const uvec2 lowPixel = uvec2(lowPixelIndex % lowWidth, lowPixelIndex / lowWidth);
    const uint overflowSlice = g_ExtinctionOverflowDepth[lowPixelIndex];
    const float saturatedExtinction = 16.0;
    float accumulatedExtinction = 0.0;
    uint cachedWordIndex = NWB_AVBOIT_OVERFLOW_INVALID;
    uint cachedPackedWord = 0u;

    for(uint physicalSlice = 0u; physicalSlice < physicalSliceCount; ++physicalSlice){
        const float transmittance = exp(-min(accumulatedExtinction, saturatedExtinction));
        imageStore(g_Transmittance, ivec3(int(lowPixel.x), int(lowPixel.y), int(physicalSlice)), vec4(transmittance, 0.0, 0.0, 0.0));

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

