// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#version 460

#include "avboit_common.glsli"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


layout(set = 1, binding = 0) uniform texture2D g_OpaqueDepth;
layout(set = 1, binding = 1) uniform sampler g_PointSampler;
layout(std430, set = 1, binding = 2) buffer NwbAvboitCoverageBuffer{
    uint g_CoverageWords[];
};

layout(location = 0) in mediump vec4 inColor;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void main(){
    float alpha;
    uint virtualSlice;
    if(!nwbAvboitAcceptLowDepthFragment(g_OpaqueDepth, g_PointSampler, alpha, virtualSlice))
        discard;

    uint virtualSlice0;
    uint virtualSlice1;
    float sliceWeight0;
    float sliceWeight1;
    nwbAvboitLinearVirtualSlicesFromDepth(gl_FragCoord.z, virtualSlice0, virtualSlice1, sliceWeight0, sliceWeight1);

    if(sliceWeight0 > NWB_AVBOIT_SPLAT_WEIGHT_EPSILON)
        atomicOr(g_CoverageWords[virtualSlice0 >> 5u], 1u << (virtualSlice0 & 31u));
    if(virtualSlice1 != virtualSlice0 && sliceWeight1 > NWB_AVBOIT_SPLAT_WEIGHT_EPSILON)
        atomicOr(g_CoverageWords[virtualSlice1 >> 5u], 1u << (virtualSlice1 & 31u));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

