// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_SCENE_SHADING_SET 1
#define NWB_SCENE_SHADING_BINDING 4
#include "avboit_common.slangi"
#include "scene_shading_lighting.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[vk::binding(1, 1)]] Texture3D<float4> g_Transmittance;
[[vk::binding(3, 1)]] SamplerState g_LinearSampler;
[[vk::binding(0, 1)]] StructuredBuffer<uint, Std430DataLayout> g_DepthWarp;
[[vk::binding(2, 1)]] StructuredBuffer<uint, Std430DataLayout> g_Control;

struct NwbAvboitAccumulatePixelInput{
    [[vk::location(0)]] float4 color : COLOR0;
    [[vk::location(1)]] float3 normal : NORMAL0;
    [[vk::location(2)]] float4 tangent : TANGENT0;
    [[vk::location(4)]] float3 worldPosition : TEXCOORD1;
};

struct NwbAvboitAccumulatePixelOutput{
    [[vk::location(0)]] float4 accumColor : SV_Target0;
    [[vk::location(1)]] float4 accumExtinction : SV_Target1;
};

static float4 inColor;
static float3 inNormal;
static float4 inTangent;
static float3 inWorldPosition;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


uint nwbAvboitActivePhysicalSliceCount(){
    return clamp(g_Control[0], 1u, nwbAvboitPhysicalSliceCount());
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
    inWorldPosition = input.worldPosition;

    const float alpha = nwbAvboitMaterialAlpha();
    if(alpha <= 0.0)
        discard;

    const float transmittance = nwbAvboitLinearBiasedTransmittance(fragCoord.xy, fragCoord.z);
    const float weightedAlpha = alpha * transmittance;

    const float3 baseColor = clamp(inColor.rgb, float3(0.0), float3(1.0));
    const float3 shadedColor = clamp(
        nwbSceneApplyDirectionalShading(baseColor, inNormal, inTangent, inWorldPosition),
        float3(0.0),
        float3(1.0)
    );

    NwbAvboitAccumulatePixelOutput output;
    output.accumColor = float4(shadedColor * weightedAlpha, weightedAlpha);
    output.accumExtinction = float4(nwbAvboitExtinctionFromAlpha(alpha), 0.0, 0.0, 0.0);
    return output;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

