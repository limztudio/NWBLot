// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#version 460

#include "avboit_common.glsli"
#include "bxdf.glsli"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


layout(set = 1, binding = 1) uniform texture3D g_Transmittance;
layout(set = 1, binding = 3) uniform sampler g_LinearSampler;

layout(location = 0) in mediump vec4 inColor;
layout(location = 1) in mediump vec3 inNormal;
layout(location = 2) in vec4 inTangent;
layout(location = 4) in vec3 inWorldPosition;
layout(location = 0) out vec4 outAccumColor;
layout(location = 1) out vec4 outAccumExtinction;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


vec2 nwbAvboitTransmittanceUvFromFragCoord(vec2 fragCoord){
    const vec2 fullSize = max(vec2(float(nwbAvboitFullWidth()), float(nwbAvboitFullHeight())), vec2(1.0));
    return clamp(fragCoord / fullSize, vec2(0.0), vec2(1.0));
}

float nwbAvboitPhysicalSliceFromVirtualSampleSlice(float sampleSlice){
    const uint virtualSliceCount = nwbAvboitVirtualSliceCount();
    const float sliceBase = floor(sampleSlice);
    const uint virtualSlice0 = uint(sliceBase);
    const uint virtualSlice1 = min(virtualSlice0 + 1u, virtualSliceCount - 1u);
    const float physicalSlice0 = float(nwbAvboitPhysicalSliceFromVirtualSlice(virtualSlice0));
    const float physicalSlice1 = float(nwbAvboitPhysicalSliceFromVirtualSlice(virtualSlice1));
    return mix(physicalSlice0, physicalSlice1, sampleSlice - sliceBase);
}

float nwbAvboitLinearBiasedTransmittance(vec2 fragCoord, float depth){
    const uint virtualSliceCount = nwbAvboitVirtualSliceCount();
    const float virtualSliceMax = max(float(virtualSliceCount) - 1.0, 0.0);
    const float sampleSlice = clamp(
        depth * float(virtualSliceCount) - nwbAvboitSelfOcclusionSliceBias(),
        0.0,
        virtualSliceMax
    );
    const float physicalSlice = nwbAvboitPhysicalSliceFromVirtualSampleSlice(sampleSlice);
    const float physicalSliceUv = (physicalSlice + 0.5) / max(float(nwbAvboitPhysicalSliceCount()), 1.0);
    return texture(
        sampler3D(g_Transmittance, g_LinearSampler),
        vec3(nwbAvboitTransmittanceUvFromFragCoord(fragCoord), clamp(physicalSliceUv, 0.0, 1.0))
    ).r;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void main(){
    const float alpha = nwbAvboitMaterialAlpha();
    if(alpha <= 0.0)
        discard;

    const float transmittance = nwbAvboitLinearBiasedTransmittance(gl_FragCoord.xy, gl_FragCoord.z);
    const float weightedAlpha = alpha * transmittance;

    const mediump vec3 baseColor = clamp(nwbProjectBxdfPixel(inColor.rgb), vec3(0.0), vec3(1.0));
    const mediump vec3 shadedColor = clamp(
        nwbProjectApplyDirectionalShading(baseColor, inNormal, inTangent, inWorldPosition),
        vec3(0.0),
        vec3(1.0)
    );

    outAccumColor = vec4(shadedColor * weightedAlpha, weightedAlpha);
    outAccumExtinction = vec4(nwbAvboitExtinctionFromAlpha(alpha), 0.0, 0.0, 0.0);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

