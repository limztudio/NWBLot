// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#version 460

#include "avboit_common.glsli"

layout(set = 1, binding = 0) uniform texture2D g_OpaqueDepth;
layout(set = 1, binding = 1) uniform sampler g_PointSampler;
layout(std430, set = 1, binding = 2) buffer NwbAvboitCoverageBuffer{
    uint g_CoverageWords[];
};

layout(location = 0) in vec3 inColor;

void main(){
    const float alpha = nwbAvboitMaterialAlpha();
    if(alpha <= 0.0)
        discard;

    const float opaqueDepth = nwbAvboitOpaqueDepthFromLowFragCoord(g_OpaqueDepth, g_PointSampler, gl_FragCoord.xy);
    if(gl_FragCoord.z > opaqueDepth)
        discard;

    const uint virtualSlice = nwbAvboitVirtualSliceFromDepth(gl_FragCoord.z);
    atomicOr(g_CoverageWords[virtualSlice >> 5u], 1u << (virtualSlice & 31u));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
