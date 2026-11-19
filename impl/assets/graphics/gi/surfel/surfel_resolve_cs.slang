// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "surfel_gather.slangi"
#include "surfel_gbuffer.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define g_NwbSurfelResolveOutput (nwbHeapStorageImage2DFloat4(g_NwbSurfelHeapPushConstants.outputStorageHeapSlot))


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_SURFEL_RESOLVE_GROUP_SIZE, NWB_SURFEL_RESOLVE_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint factor = uint(g_NwbSceneSurfelParams.screenHeightPad.y);
    const uint screenWidth = uint(g_NwbSceneSurfelParams.ageRaysTileScreen.w);
    const uint screenHeight = uint(g_NwbSceneSurfelParams.screenHeightPad.x);
    const uint halfWidth = (screenWidth + factor - 1u) / factor;
    const uint halfHeight = (screenHeight + factor - 1u) / factor;
    const uint2 halfPixel = dispatchThreadID.xy;
    if(halfPixel.x >= halfWidth || halfPixel.y >= halfHeight)
        return;

    const uint2 fullPixel = min(halfPixel * factor, uint2(screenWidth - 1u, screenHeight - 1u));
    const int3 loadCoord = int3(int2(fullPixel), 0);
    const half3 rawNormalSample = half3(nwbHeapSampledImage2D(g_NwbSurfelHeapPushConstants.normalSlot).Load(loadCoord).xyz);
    const float3 rawNormal = float3(rawNormalSample);
    if(dot(rawNormal, rawNormal) < NWB_SURFEL_GBUFFER_NORMAL_LENGTH_SQUARED_MIN){
        g_NwbSurfelResolveOutput[halfPixel] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    const half3 worldPositionSample = half3(nwbHeapSampledImage2D(g_NwbSurfelHeapPushConstants.worldPositionSlot).Load(loadCoord).xyz);
    const float3 worldPosition = float3(worldPositionSample);
    const float3 normal = float3(nwbSurfelDecodeGbufferNormal(rawNormalSample));

    half3 halfGi;
    half coverage;
    nwbSurfelGather(worldPosition, normal, halfGi, coverage);
    g_NwbSurfelResolveOutput[halfPixel] = float4(float3(halfGi), float(coverage));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

