// Surfel resolve: one thread per HALF-res pixel (U6). Reads the G-buffer world position + normal, gathers the surfel
// irradiance (nwbSurfelGather -- the (2*EXTENT+1)^3 hash walk, reused verbatim), and writes it to a HALF-res texture
// that surfel_upsample_cs then reconstructs into the full-res surfelIrradiance the deferred-lighting PIXEL shader samples.
// Running the 125-cell gather at 1/FACTOR^2 the pixels is the U6 perf win (a half-res 5x5x5 ~= a full-res 3x3x3).
// This is the caustic-style decoupling: the surfel pool is touched ONLY by COMPUTE (spawn / hash-build / trace / this
// resolve), never by the pixel shader, so the frames-in-flight race (the pixel gather of frame N overlapping the compute
// RMW of frame N+1 on the shared single-buffered pool) is eliminated.
// Output alpha = 1 where a surfel covered the half texel (rgb = gathered irradiance); 0 elsewhere (background or no
// coverage) so the upsample -> lighting falls back to hemiAmbient. Decode the query normal the same way lighting does
// (encode = n*0.5+0.5), so the surfel normalWeight matches the deferred path.


// Point the gather's pool / hash / params bindings at the resolve set BEFORE surfel_gather declares them.
#include "surfel_binding_slots.h"
#define NWB_SCENE_GI_SURFEL_POOL_SET NWB_SURFEL_RESOLVE_SET
#define NWB_SCENE_GI_SURFEL_POOL_BINDING NWB_SURFEL_RESOLVE_BINDING_POOL
#define NWB_SCENE_GI_SURFEL_HASH_SET NWB_SURFEL_RESOLVE_SET
#define NWB_SCENE_GI_SURFEL_HASH_BINDING NWB_SURFEL_RESOLVE_BINDING_CELL_HEAD
#define NWB_SCENE_GI_SURFEL_PARAMS_SET NWB_SURFEL_RESOLVE_SET
#define NWB_SCENE_GI_SURFEL_PARAMS_BINDING NWB_SURFEL_RESOLVE_BINDING_CONSTANTS

#include "surfel_gather.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[vk::binding(NWB_SURFEL_RESOLVE_BINDING_GBUFFER_WORLD_POSITION, NWB_SURFEL_RESOLVE_SET)]]
Texture2D<float4> g_NwbSurfelResolveGbufferWorldPosition;

[[vk::binding(NWB_SURFEL_RESOLVE_BINDING_GBUFFER_NORMAL, NWB_SURFEL_RESOLVE_SET)]]
Texture2D<float4> g_NwbSurfelResolveGbufferNormal;

[[vk::binding(NWB_SURFEL_RESOLVE_BINDING_OUTPUT, NWB_SURFEL_RESOLVE_SET)]]
RWTexture2D<float4> g_NwbSurfelResolveOutput;


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
    const float3 rawNormal = g_NwbSurfelResolveGbufferNormal.Load(loadCoord).xyz;
    if(dot(rawNormal, rawNormal) < 0.01){
        g_NwbSurfelResolveOutput[halfPixel] = float4(0.0, 0.0, 0.0, 0.0);   // background -> no GI (upsample -> hemiAmbient)
        return;
    }

    const float3 worldPosition = g_NwbSurfelResolveGbufferWorldPosition.Load(loadCoord).xyz;
    const float3 normal = normalize(rawNormal * 2.0 - 1.0);   // decode the packed G-buffer normal (matches the lighting)

    float3 gi;
    if(nwbSurfelGather(worldPosition, normal, gi))
        g_NwbSurfelResolveOutput[halfPixel] = float4(gi, 1.0);
    else
        g_NwbSurfelResolveOutput[halfPixel] = float4(0.0, 0.0, 0.0, 0.0);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

