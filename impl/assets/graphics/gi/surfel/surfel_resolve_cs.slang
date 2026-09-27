// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Resolve surfel irradiance at reduced resolution; alpha marks coverage for the ambient fallback.
// Pool access is compute-only, so deferred lighting reads the resolved texture.


#include "surfel_gather.slangi"
#include "surfel_gbuffer.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define g_NwbSurfelResolveOutput (NwbHeapStorageImage2DFloat4(g_NwbSurfelHeapPushConstants.outputStorageHeapSlot))


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_SURFEL_RESOLVE_GROUP_SIZE, NWB_SURFEL_RESOLVE_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint factor = uint(g_NwbSceneSurfelParams.screenHeightPad.y);
    const uint screenWidth = uint(g_NwbSceneSurfelParams.ageRaysTileScreen.w);
    const uint screenHeight = uint(g_NwbSceneSurfelParams.screenHeightPad.x);
    const uint halfWidth = (screenWidth + factor - 1u) / factor;    // Matches the CPU reduced-size dispatch.
    const uint halfHeight = (screenHeight + factor - 1u) / factor;
    const uint2 halfPixel = dispatchThreadID.xy;
    if(halfPixel.x >= halfWidth || halfPixel.y >= halfHeight)
        return;

    // Gather and upsample use the same top-left full-resolution pixel for each reduced-grid texel.
    const uint2 fullPixel = min(halfPixel * factor, uint2(screenWidth - 1u, screenHeight - 1u));
    const int3 loadCoord = int3(int2(fullPixel), 0);
    // G-buffer vectors are RGBA16F. Retain them as half during fetch and expand only for the spatial gather math.
    const half3 rawNormalSample = half3(NwbHeapSampledImage2D(g_NwbSurfelHeapPushConstants.normalSlot).Load(loadCoord).xyz);
    const float3 rawNormal = float3(rawNormalSample);
    if(dot(rawNormal, rawNormal) < NWB_SURFEL_GBUFFER_NORMAL_LENGTH_SQUARED_MIN){
        g_NwbSurfelResolveOutput[halfPixel] = float4(0.0, 0.0, 0.0, 0.0);   // background -> no GI (upsample -> hemiAmbient)
        return;
    }

    const half3 worldPositionSample = half3(NwbHeapSampledImage2D(g_NwbSurfelHeapPushConstants.worldPositionSlot).Load(loadCoord).xyz);
    const float3 worldPosition = float3(worldPositionSample);
    const float3 normal = nwbSurfelDecodeGbufferNormal(rawNormal);

    float3 gi;
    half3 halfGi;
    if(nwbSurfelGather(worldPosition, normal, halfGi)){
        gi = float3(halfGi);
        g_NwbSurfelResolveOutput[halfPixel] = float4(gi, 1.0);
    }
    else
        g_NwbSurfelResolveOutput[halfPixel] = float4(0.0, 0.0, 0.0, 0.0);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

