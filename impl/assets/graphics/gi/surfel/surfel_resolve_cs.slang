// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Surfel resolve: one thread per HALF-res pixel. Reads the G-buffer world position + normal, gathers the surfel
// irradiance (nwbSurfelGather -- the (2*EXTENT+1)^3 hash walk, reused verbatim), and writes it to a HALF-res texture
// that surfel_upsample_cs then reconstructs into the full-res surfelIrradiance the deferred-lighting compute shader samples.
// Running the 125-cell gather at 1/FACTOR^2 the pixels reduces its cost to roughly a full-res 3x3x3 gather.
// The surfel pool is touched only by compute (spawn / hash-build / trace / resolve), never by the lighting shader,
// avoiding frames-in-flight read/write overlap on the shared single-buffered pool.
// Output alpha = 1 where a surfel covered the half texel (rgb = gathered irradiance); 0 elsewhere (background or no
// coverage) so the upsample -> lighting falls back to hemiAmbient. Decode the query normal the same way lighting does
// (encode = n*0.5+0.5), so the surfel normalWeight matches the deferred path.


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
    if(nwbSurfelGather(worldPosition, normal, gi))
        g_NwbSurfelResolveOutput[halfPixel] = float4(gi, 1.0);
    else
        g_NwbSurfelResolveOutput[halfPixel] = float4(0.0, 0.0, 0.0, 0.0);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

