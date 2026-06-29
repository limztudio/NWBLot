// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "binding_slots.h"
#include "../scene/binding_slots.h"
#include "../mesh/runtime_constants.h"

#define NWB_SCENE_SHADING_SET NWB_AVBOIT_TRANSPARENT_SET
#define NWB_SCENE_SHADING_BINDING NWB_AVBOIT_ACCUMULATE_BINDING_SCENE_SHADING
#define NWB_SCENE_LIGHT_LIST_SET NWB_AVBOIT_TRANSPARENT_SET
#define NWB_SCENE_LIGHT_LIST_BINDING NWB_AVBOIT_ACCUMULATE_BINDING_LIGHT_LIST
#include "common.slangi"
#include "../scene/lighting.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[vk::binding(NWB_AVBOIT_ACCUMULATE_BINDING_TRANSMITTANCE, NWB_AVBOIT_TRANSPARENT_SET)]] Texture3D<float4> g_Transmittance;
[[vk::binding(NWB_AVBOIT_ACCUMULATE_BINDING_LINEAR_SAMPLER, NWB_AVBOIT_TRANSPARENT_SET)]] SamplerState g_LinearSampler;
[[vk::binding(NWB_AVBOIT_ACCUMULATE_BINDING_DEPTH_WARP, NWB_AVBOIT_TRANSPARENT_SET)]] StructuredBuffer<uint, Std430DataLayout> g_DepthWarp;
[[vk::binding(NWB_AVBOIT_ACCUMULATE_BINDING_CONTROL, NWB_AVBOIT_TRANSPARENT_SET)]] StructuredBuffer<uint, Std430DataLayout> g_Control;

struct NwbAvboitAccumulatePixelInput{
    [[vk::location(NWB_MESH_RASTER_COLOR_LOCATION)]] float4 color : COLOR0;
    [[vk::location(NWB_MESH_RASTER_NORMAL_LOCATION)]] float3 normal : NORMAL0;
    [[vk::location(NWB_MESH_RASTER_TANGENT_LOCATION)]] float4 tangent : TANGENT0;
    [[vk::location(NWB_MESH_RASTER_WORLD_POSITION_LOCATION)]] float4 worldPosition : TEXCOORD1;
};

struct NwbAvboitAccumulatePixelOutput{
    [[vk::location(NWB_AVBOIT_ACCUM_COLOR_LOCATION)]] float4 accumColor : SV_Target0;
    [[vk::location(NWB_AVBOIT_ACCUM_EXTINCTION_LOCATION)]] float4 accumExtinction : SV_Target1;
};

static half4 inColor;
static float3 inNormal;
static float4 inTangent;
static float3 inWorldPosition;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


uint nwbAvboitActivePhysicalSliceCount(){
    return clamp(g_Control[NWB_AVBOIT_CONTROL_ACTIVE_PHYSICAL_SLICES], 1u, nwbAvboitPhysicalSliceCount());
}

float nwbAvboitWarpedPhysicalSliceFromVirtualSlice(uint virtualSlice){
    const uint clampedVirtualSlice = min(virtualSlice, nwbAvboitVirtualSliceCount() - 1u);
    return float(min(g_DepthWarp[clampedVirtualSlice], nwbAvboitActivePhysicalSliceCount() - 1u));
}

float nwbAvboitPhysicalSliceFromVirtualSampleSlice(float sampleSlice){
    const uint virtualSliceCount = nwbAvboitVirtualSliceCount();
    const float sliceBase = floor(sampleSlice);
    const uint virtualSlice0 = uint(sliceBase);
    const uint virtualSlice1 = min(virtualSlice0 + 1u, virtualSliceCount - 1u);
    const float physicalSlice0 = nwbAvboitWarpedPhysicalSliceFromVirtualSlice(virtualSlice0);
    const float physicalSlice1 = nwbAvboitWarpedPhysicalSliceFromVirtualSlice(virtualSlice1);
    return lerp(physicalSlice0, physicalSlice1, sampleSlice - sliceBase);
}

float nwbAvboitLinearBiasedTransmittance(float2 fragCoord, float depth){
    const uint virtualSliceCount = nwbAvboitVirtualSliceCount();
    const float virtualSliceMax = max(float(virtualSliceCount) - 1.0, 0.0);
    const float sampleSlice = clamp(
        depth * float(virtualSliceCount) - nwbAvboitSelfOcclusionSliceBias(),
        0.0,
        virtualSliceMax
    );
    const float physicalSlice = nwbAvboitPhysicalSliceFromVirtualSampleSlice(sampleSlice);
    const float physicalSliceUv = (physicalSlice + 0.5) / max(float(nwbAvboitPhysicalSliceCount()), 1.0);
    return g_Transmittance.Sample(
        g_LinearSampler,
        float3(nwbAvboitFullUvFromFragCoord(fragCoord), clamp(physicalSliceUv, 0.0, 1.0))
    ).r;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NwbAvboitAccumulatePixelOutput main(NwbAvboitAccumulatePixelInput input, float4 fragCoord : SV_Position){
    inColor = input.color;
    inNormal = input.normal;
    inTangent = input.tangent;
    inWorldPosition = input.worldPosition.xyz;
#if NWB_CSG_ENABLED
    if(nwbAvboitCsgPixelDiscard(fragCoord, input.worldPosition))
        discard;
#endif

    const half alpha = nwbAvboitResolveFragmentAlpha(inColor);
    if(alpha <= half(0.0))
        discard;

    const half transmittance = half(nwbAvboitLinearBiasedTransmittance(fragCoord.xy, fragCoord.z));
    const half weightedAlpha = alpha * transmittance;

    const half3 baseColor = clamp(inColor.rgb, half3(0.0), half3(1.0));
    const half3 shadedColor = clamp(
        // Transparent fragments are not in the opaque G-buffer the screen-space shadow visibility was traced
        // from, so pass the negative-pixel sentinel: nwbSceneApplyLighting skips shadow sampling and treats
        // every light as fully visible (the pre-colored-shadow transparent behaviour).
        nwbSceneApplyLighting(baseColor, inNormal, inTangent, inWorldPosition, int2(-1, -1)),
        half3(0.0),
        half3(1.0)
    );

    NwbAvboitAccumulatePixelOutput output;
    output.accumColor = float4(shadedColor * weightedAlpha, weightedAlpha);
    output.accumExtinction = float4(nwbAvboitExtinctionFromAlpha(alpha), 0.0, 0.0, 0.0);
    return output;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

