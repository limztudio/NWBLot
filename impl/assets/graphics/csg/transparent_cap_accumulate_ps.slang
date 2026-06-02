// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_CSG_ENABLED 1
#define NWB_CSG_CLIP_SET 2

#include "../avboit/binding_slots.h"
#include "../scene/binding_slots.h"
#include "../mesh/runtime_constants.h"

#define NWB_SCENE_SHADING_SET NWB_SCENE_SHADING_AVBOIT_ACCUMULATE_SET
#define NWB_SCENE_SHADING_BINDING NWB_SCENE_SHADING_AVBOIT_ACCUMULATE_BINDING
#include "../avboit/common.slangi"
#include "../scene/lighting.slangi"
#include "transparent_cap_avboit_common.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[vk::binding(NWB_AVBOIT_ACCUMULATE_BINDING_TRANSMITTANCE, NWB_AVBOIT_TRANSPARENT_SET)]] Texture3D<float4> g_Transmittance;
[[vk::binding(NWB_AVBOIT_ACCUMULATE_BINDING_LINEAR_SAMPLER, NWB_AVBOIT_TRANSPARENT_SET)]] SamplerState g_LinearSampler;
[[vk::binding(NWB_AVBOIT_ACCUMULATE_BINDING_DEPTH_WARP, NWB_AVBOIT_TRANSPARENT_SET)]] StructuredBuffer<uint, Std430DataLayout> g_DepthWarp;
[[vk::binding(NWB_AVBOIT_ACCUMULATE_BINDING_CONTROL, NWB_AVBOIT_TRANSPARENT_SET)]] StructuredBuffer<uint, Std430DataLayout> g_Control;

struct NwbCsgTransparentCapAccumulatePixelOutput{
    [[vk::location(NWB_AVBOIT_ACCUM_COLOR_LOCATION)]] float4 accumColor : SV_Target0;
    [[vk::location(NWB_AVBOIT_ACCUM_EXTINCTION_LOCATION)]] float4 accumExtinction : SV_Target1;
};

static float4 inColor;
static float3 inNormal;
static float4 inTangent;
static float3 inWorldPosition;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


uint nwbCsgTransparentCapActivePhysicalSliceCount(){
    return clamp(g_Control[NWB_AVBOIT_CONTROL_ACTIVE_PHYSICAL_SLICES], 1u, nwbAvboitPhysicalSliceCount());
}

float nwbCsgTransparentCapWarpedPhysicalSliceFromVirtualSlice(uint virtualSlice){
    const uint clampedVirtualSlice = min(virtualSlice, nwbAvboitVirtualSliceCount() - 1u);
    return float(min(g_DepthWarp[clampedVirtualSlice], nwbCsgTransparentCapActivePhysicalSliceCount() - 1u));
}

float nwbCsgTransparentCapPhysicalSliceFromVirtualSampleSlice(float sampleSlice){
    const uint virtualSliceCount = nwbAvboitVirtualSliceCount();
    const float sliceBase = floor(sampleSlice);
    const uint virtualSlice0 = uint(sliceBase);
    const uint virtualSlice1 = min(virtualSlice0 + 1u, virtualSliceCount - 1u);
    const float physicalSlice0 = nwbCsgTransparentCapWarpedPhysicalSliceFromVirtualSlice(virtualSlice0);
    const float physicalSlice1 = nwbCsgTransparentCapWarpedPhysicalSliceFromVirtualSlice(virtualSlice1);
    return lerp(physicalSlice0, physicalSlice1, sampleSlice - sliceBase);
}

float nwbCsgTransparentCapLinearBiasedTransmittance(float2 fragCoord, float depth){
    const uint virtualSliceCount = nwbAvboitVirtualSliceCount();
    const float virtualSliceMax = max(float(virtualSliceCount) - 1.0, 0.0);
    const float sampleSlice = clamp(
        depth * float(virtualSliceCount) - nwbAvboitSelfOcclusionSliceBias(),
        0.0,
        virtualSliceMax
    );
    const float physicalSlice = nwbCsgTransparentCapPhysicalSliceFromVirtualSampleSlice(sampleSlice);
    const float physicalSliceUv = (physicalSlice + 0.5) / max(float(nwbAvboitPhysicalSliceCount()), 1.0);
    return g_Transmittance.Sample(
        g_LinearSampler,
        float3(nwbAvboitFullUvFromFragCoord(fragCoord), clamp(physicalSliceUv, 0.0, 1.0))
    ).r;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NwbCsgTransparentCapAccumulatePixelOutput main(NwbCsgTransparentCapAvboitPixelInput input, float4 fragCoord : SV_Position){
    inColor = input.color;
    inNormal = input.normal;
    inTangent = input.tangent;
    inWorldPosition = input.worldPosition.xyz;
    if(nwbCsgTransparentCapPixelDiscard(input))
        discard;

    const float alpha = nwbAvboitResolveFragmentAlpha(input.color);
    if(alpha <= 0.0)
        discard;

    const float transmittance = nwbCsgTransparentCapLinearBiasedTransmittance(fragCoord.xy, fragCoord.z);
    const float weightedAlpha = alpha * transmittance;

    const float3 baseColor = clamp(inColor.rgb, float3(0.0), float3(1.0));
    const float3 shadedColor = clamp(
        nwbSceneApplyDirectionalShading(baseColor, inNormal, inTangent, inWorldPosition),
        float3(0.0),
        float3(1.0)
    );

    NwbCsgTransparentCapAccumulatePixelOutput output;
    output.accumColor = float4(shadedColor * weightedAlpha, weightedAlpha);
    output.accumExtinction = float4(nwbAvboitExtinctionFromAlpha(alpha), 0.0, 0.0, 0.0);
    return output;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

