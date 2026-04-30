// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#version 460

#include "avboit_common.glsli"
#include "bxdf.glsli"

layout(std430, set = 1, binding = 0) readonly buffer NwbAvboitDepthWarpBuffer{
    uint g_DepthWarp[];
};
layout(std430, set = 1, binding = 1) readonly buffer NwbAvboitTransmittanceBuffer{
    uint g_Transmittance[];
};
layout(std430, set = 1, binding = 2) readonly buffer NwbAvboitControlBuffer{
    uint g_Control[];
};

layout(location = 0) in mediump vec4 inColor;
layout(location = 1) in mediump vec3 inNormal;
layout(location = 2) in vec4 inTangent;
layout(location = 4) in vec3 inWorldPosition;
layout(location = 0) out vec4 outAccumColor;
layout(location = 1) out vec4 outAccumExtinction;

uint nwbAvboitPhysicalSliceFromVirtualSlice(uint virtualSlice){
    const uint activePhysicalSlices = max(min(g_Control[0], nwbAvboitPhysicalSliceCount()), 1u);
    return min(g_DepthWarp[virtualSlice], activePhysicalSlices - 1u);
}

float nwbAvboitTransmittanceAtVirtualSlice(uvec2 lowPixel, uint virtualSlice){
    const uint physicalSlice = nwbAvboitPhysicalSliceFromVirtualSlice(virtualSlice);
    const uint volumeIndex = nwbAvboitVolumeIndex(lowPixel, physicalSlice);
    return volumeIndex < nwbAvboitVolumeVoxelCount()
        ? nwbAvboitUnpackTransmittance(g_Transmittance[volumeIndex])
        : 1.0
    ;
}

float nwbAvboitLinearBiasedTransmittance(uvec2 lowPixel, float depth){
    const uint virtualSliceCount = nwbAvboitVirtualSliceCount();
    const float virtualSliceMax = max(float(virtualSliceCount) - 1.0, 0.0);
    const float sampleSlice = clamp(
        depth * float(virtualSliceCount) - nwbAvboitSelfOcclusionSliceBias(),
        0.0,
        virtualSliceMax
    );
    const float sliceBase = floor(sampleSlice);
    const uint virtualSlice0 = uint(sliceBase);
    const uint virtualSlice1 = min(virtualSlice0 + 1u, virtualSliceCount - 1u);
    const float transmittance0 = nwbAvboitTransmittanceAtVirtualSlice(lowPixel, virtualSlice0);
    const float transmittance1 = nwbAvboitTransmittanceAtVirtualSlice(lowPixel, virtualSlice1);
    return mix(transmittance0, transmittance1, sampleSlice - sliceBase);
}

void main(){
    const float alpha = nwbAvboitMaterialAlpha();
    if(alpha <= 0.0)
        discard;

    const uvec2 lowPixel = nwbAvboitLowPixelFromFragCoord(gl_FragCoord.xy);
    const float transmittance = nwbAvboitLinearBiasedTransmittance(lowPixel, gl_FragCoord.z);
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

