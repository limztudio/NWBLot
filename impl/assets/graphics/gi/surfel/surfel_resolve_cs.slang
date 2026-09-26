// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Half-res surfel resolve: one thread per half texel runs the (2*EXTENT+1)^3 gather (125 cells at 1/FACTOR^2 cost) and
// writes rgb = irradiance, a = coverage (0 falls back to hemiAmbient). Compute-only pool access: no lighting overlap.
// Query normal decodes as lighting does (n*0.5+0.5) so normalWeight matches the deferred path.


#include "surfel_gather.slangi"
#include "surfel_gbuffer.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define g_NwbSurfelResolveOutput (NwbHeapStorageImage2DFloat4(g_NwbSurfelHeapPushConstants.outputStorageHeapSlot))


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_SURFEL_RESOLVE_GROUP_SIZE, NWB_SURFEL_RESOLVE_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint factor = uint(NWB_SURFEL_RESOLVE_HALF_FACTOR);
    const uint screenWidth = uint(g_NwbSceneSurfelParams.ageRaysTileScreen.w);
    const uint screenHeight = uint(g_NwbSceneSurfelParams.screenHeightPad.x);
    const uint halfWidth = (screenWidth + factor - 1u) / factor;    // DivideUp -- matches the CPU half-dims dispatch
    const uint halfHeight = (screenHeight + factor - 1u) / factor;
    const uint2 halfPixel = dispatchThreadID.xy;
    if(halfPixel.x >= halfWidth || halfPixel.y >= halfHeight)
        return;

    // Point-sample the full-res G-buffer at the half texel's top-left full pixel -- the SAME full texel the upsample
    // re-reads per tap to recover this texel's geometry, so the gather normal and the upsample gate agree exactly.
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

