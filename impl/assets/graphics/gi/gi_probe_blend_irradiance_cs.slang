// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// DDGI irradiance atlas blend (U4). Gather-style cosine-weighted EMA, one thread per interior atlas texel (NO float
// image atomics -- the caustic accumulator precedent). For each texel we reconstruct the octahedral direction, gather
// this frame's ray radiance weighted by max(dot(texelDir, rayDir), 0) over the probe's rays, normalize by the cosine
// weight sum, then EMA-blend into the front irradiance atlas -> back irradiance atlas. Non-updated (round-robin) probes
// pass-through-copy their front value so history is preserved across the flip. Mirrors gi_probe_blend_distance_cs; the
// two share the blend binding layout (CB + ray-data SRV + front SRV + back UAV), only the atlas format differs
// (RGBA16F irradiance vs RG16F distance). See .helper/ddgi_plan.md 2/3 (U4).
//
// NOTE (P1 follow-up): the gathered value is scene-referred incoming radiance; a 1/pi diffuse normalization belongs
// either here or in the consumer (currently neither) -- tracked as a brightness-tuning item, not a P0 blocker.


#include "binding_slots.h"
#include "probe_grid.slangi"
#include "octahedral.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[vk::binding(NWB_GI_BLEND_BINDING_GRID_CONSTANTS, NWB_GI_SET)]]
ConstantBuffer<NwbGiGridConstants> g_NwbGiGrid;

[[vk::binding(NWB_GI_BLEND_BINDING_RAY_DATA, NWB_GI_SET)]]
Texture2D<float4> g_NwbGiRayData;

[[vk::binding(NWB_GI_BLEND_BINDING_FRONT_ATLAS, NWB_GI_SET)]]
Texture2D<float4> g_NwbGiFrontIrradiance;

[[vk::binding(NWB_GI_BLEND_BINDING_BACK_ATLAS, NWB_GI_SET)]]
RWTexture2D<float4> g_NwbGiBackIrradiance;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_GI_GROUP_SIZE, NWB_GI_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint probeCount = nwbGiProbeCount(g_NwbGiGrid);
    const uint tile = NWB_GI_IRRADIANCE_ATLAS_TILE;
    const uint interior = NWB_GI_IRRADIANCE_ATLAS_INTERIOR;
    const uint border = 1u;

    // One thread per interior texel of one probe tile. Tiles pack into a 2D grid of NWB_GI_ATLAS_TILES_PER_ROW
    // columns, so the dispatch grid is (tilesPerRow * interior, ceil(probeCount/tilesPerRow) * interior). Map the
    // thread's atlas-interior coordinate back to (tileX, tileY) and the linear probe index.
    const uint tilesPerRow = NWB_GI_ATLAS_TILES_PER_ROW;
    const uint texelX = dispatchThreadID.x;
    const uint texelY = dispatchThreadID.y;
    if(texelX >= tilesPerRow * interior)
        return;

    const uint tileX = texelX / interior;
    const uint tileY = texelY / interior;
    const uint localX = texelX % interior;
    const uint localY = texelY % interior;
    const uint probeIndex = tileY * tilesPerRow + tileX;
    if(probeIndex >= probeCount)
        return;

    // The texel's atlas coordinate (interior offset by the border).
    const uint2 atlasCoord = nwbGiAtlasTileOrigin(probeIndex, tile) + uint2(localX + border, localY + border);

    // Reconstruct the direction this texel represents.
    const float2 uv = (float2(localX, localY) + 0.5) / float(interior);
    const float3 texelDir = nwbGiOctahedralDecode(uv);

    // Round-robin: only updated probes have fresh ray-data this frame. A non-updated probe pass-through-copies its
    // front value into the back atlas so its history is preserved exactly across the flip.
    const bool updated = nwbGiProbeActiveThisFrame(probeIndex, g_NwbGiGrid);
    if(!updated){
        g_NwbGiBackIrradiance[atlasCoord] = g_NwbGiFrontIrradiance[atlasCoord];
        return;
    }

    // Gather the cosine-weighted radiance over the rays whose direction matches this texel's hemisphere lobe. Clamp
    // the CB-sourced ray count to the ray-data texture height so a stray/garbage params0.x can never spin the loop
    // into a GPU hang (defensive; the CPU writes the real 64).
    const uint raysPerProbe = min(uint(g_NwbGiGrid.params0.x), NWB_GI_PROBE_MAX_RAY_COUNT);
    const float2 rotation = nwbGiFrameRotation(uint(g_NwbGiGrid.params0.z));
    float3 irradianceSum = float3(0.0, 0.0, 0.0);
    float weightSum = 0.0;
    for(uint rayIndex = 0u; rayIndex < raysPerProbe; ++rayIndex){
        const float3 rayDir = nwbGiFibonacciDirection(rayIndex, raysPerProbe, rotation);
        const float weight = max(dot(texelDir, rayDir), 0.0);
        if(weight <= 0.0)
            continue;
        const float4 rayResult = g_NwbGiRayData.Load(int3(int(probeIndex), int(rayIndex), 0));
        irradianceSum += rayResult.rgb * weight;
        weightSum += weight;
    }

    float3 irradiance = float3(0.0, 0.0, 0.0);
    if(weightSum > 0.0)
        irradiance = irradianceSum / weightSum;

    // EMA blend against the front (history-in) atlas. Hysteresis is 0 on frame 0 / newly-seeded (pure current).
    const float hysteresis = g_NwbGiGrid.params1.x;
    const float3 history = g_NwbGiFrontIrradiance[atlasCoord].rgb;
    const float3 blended = lerp(irradiance, history, hysteresis);

    g_NwbGiBackIrradiance[atlasCoord] = float4(blended, 1.0);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
