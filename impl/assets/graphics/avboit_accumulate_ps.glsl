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

void main(){
    const float alpha = nwbAvboitMaterialAlpha();
    if(alpha <= 0.0)
        discard;

    const float biasedDepth = clamp(
        gl_FragCoord.z - (nwbAvboitSelfOcclusionSliceBias() / max(float(nwbAvboitVirtualSliceCount()), 1.0)),
        0.0,
        1.0
    );
    const uint virtualSlice = nwbAvboitVirtualSliceFromDepth(biasedDepth);
    const uint physicalSlice = min(g_DepthWarp[virtualSlice], g_Control[0] - 1u);
    const uvec2 lowPixel = nwbAvboitLowPixelFromFragCoord(gl_FragCoord.xy);
    const uint volumeIndex = nwbAvboitVolumeIndex(lowPixel, physicalSlice);

    const float transmittance = volumeIndex < nwbAvboitVolumeVoxelCount()
        ? nwbAvboitUnpackTransmittance(g_Transmittance[volumeIndex])
        : 1.0
    ;
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

