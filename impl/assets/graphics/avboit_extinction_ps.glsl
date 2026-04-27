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

layout(location = 0) in mediump vec4 inColor;

void main(){
    float alpha;
    uint virtualSlice;
    if(!nwbAvboitAcceptLowDepthFragment(g_OpaqueDepth, g_PointSampler, alpha, virtualSlice))
        discard;

    const uint physicalSlice = min(g_DepthWarp[virtualSlice], g_Control[0] - 1u);
    const uvec2 lowPixel = nwbAvboitLowPixelFromLowFragCoord(gl_FragCoord.xy);
    const uint volumeIndex = nwbAvboitVolumeIndex(lowPixel, physicalSlice);
    if(volumeIndex >= nwbAvboitVolumeVoxelCount())
        discard;

    atomicAdd(g_Extinction[volumeIndex], nwbAvboitPackExtinction(nwbAvboitExtinctionFromAlpha(alpha)));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

