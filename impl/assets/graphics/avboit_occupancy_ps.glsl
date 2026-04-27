// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#version 460

#include "avboit_common.glsli"

layout(set = 1, binding = 0) uniform texture2D g_OpaqueDepth;
layout(set = 1, binding = 1) uniform sampler g_PointSampler;
layout(std430, set = 1, binding = 2) buffer NwbAvboitCoverageBuffer{
    uint g_CoverageWords[];
};

layout(location = 0) in mediump vec4 inColor;

void main(){
    float alpha;
    uint virtualSlice;
    if(!nwbAvboitAcceptLowDepthFragment(g_OpaqueDepth, g_PointSampler, alpha, virtualSlice))
        discard;

    atomicOr(g_CoverageWords[virtualSlice >> 5u], 1u << (virtualSlice & 31u));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

