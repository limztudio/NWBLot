// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#version 460

#include "avboit_common.glsli"

layout(set = 1, binding = 0) uniform texture2D g_OpaqueDepth;
layout(set = 1, binding = 1) uniform sampler g_PointSampler;
layout(std430, set = 1, binding = 2) readonly buffer NwbAvboitDepthWarpBuffer{
    uint g_DepthWarp[];
};
layout(std430, set = 1, binding = 3) readonly buffer NwbAvboitControlBuffer{
    uint g_Control[];
};
layout(std430, set = 1, binding = 4) buffer NwbAvboitExtinctionBuffer{
    uint g_Extinction[];
};
layout(std430, set = 1, binding = 5) buffer NwbAvboitExtinctionOverflowDepthBuffer{
    uint g_ExtinctionOverflowDepth[];
};

layout(location = 0) in mediump vec4 inColor;

void nwbAvboitSplatExtinction(uvec2 lowPixel, uint virtualSlice, float extinction){
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
    const uint previousWord = atomicAdd(g_Extinction[wordIndex], packedExtinction << byteShift);
    const uint previousByte = (previousWord >> byteShift) & NWB_AVBOIT_EXTINCTION_BYTE_MASK;
    const bool byteOverflow = previousByte + packedExtinction > NWB_AVBOIT_EXTINCTION_BYTE_MASK;
    const bool rawOverflow = float(previousByte) / nwbAvboitExtinctionFixedScale() + extinction > nwbAvboitExtinctionSaturation();
    if(byteOverflow || rawOverflow)
        atomicMin(g_ExtinctionOverflowDepth[nwbAvboitLowPixelIndex(lowPixel)], physicalSlice);
}

void main(){
    float alpha;
    uint virtualSlice;
    if(!nwbAvboitAcceptLowDepthFragment(g_OpaqueDepth, g_PointSampler, alpha, virtualSlice))
        discard;

    const uvec2 lowPixel = nwbAvboitLowPixelFromLowFragCoord(gl_FragCoord.xy);
    const float extinction = nwbAvboitExtinctionFromAlpha(alpha);

    uint virtualSlice0;
    uint virtualSlice1;
    float sliceWeight0;
    float sliceWeight1;
    nwbAvboitLinearVirtualSlicesFromDepth(gl_FragCoord.z, virtualSlice0, virtualSlice1, sliceWeight0, sliceWeight1);

    if(sliceWeight0 > NWB_AVBOIT_SPLAT_WEIGHT_EPSILON)
        nwbAvboitSplatExtinction(lowPixel, virtualSlice0, extinction * sliceWeight0);
    if(virtualSlice1 != virtualSlice0 && sliceWeight1 > NWB_AVBOIT_SPLAT_WEIGHT_EPSILON)
        nwbAvboitSplatExtinction(lowPixel, virtualSlice1, extinction * sliceWeight1);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

