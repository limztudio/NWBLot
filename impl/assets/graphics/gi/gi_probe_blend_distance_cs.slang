// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// DDGI distance atlas blend (U4). Gather-style EMA: one thread per interior atlas texel. For each texel we
// reconstruct the octahedral direction, gather the mean + mean-squared hit distance over the rays whose direction
// matches the texel's hemisphere lobe, then EMA-blend into the front distance atlas -> back distance atlas. The
// mean + mean^2 feed the Chebyshev leak rejection in nwbBxdfIndirectIrradiance (U2) + the self-shadow bias.
// hitT is clamped to ~1.5x the grid diagonal so a stray far miss does not poison the distance statistics.
//
// Storage layout: RG16F, .x = mean distance, .y = mean-squared distance. The Chebyshev variance = mean^2 - mean^2
// (= E[X^2] - E[X]^2) is reconstructed at the consumer, so we store the two raw moments. See .helper/ddgi_plan.md
// 2/3 (U4).


#include "binding_slots.h"
#include "probe_grid.slangi"
#include "octahedral.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[vk::binding(NWB_GI_BLEND_BINDING_GRID_CONSTANTS, NWB_GI_SET)]]
ConstantBuffer<NwbGiGridConstants> g_NwbGiGrid;

[[vk::binding(NWB_GI_BLEND_BINDING_RAY_DATA, NWB_GI_SET)]]
Texture2D<float4> g_NwbGiRayData;

[[vk::binding(NWB_GI_BLEND_BINDING_FRONT_ATLAS, NWB_GI_SET)]]
Texture2D<float2> g_NwbGiFrontDistance;

[[vk::binding(NWB_GI_BLEND_BINDING_BACK_ATLAS, NWB_GI_SET)]]
RWTexture2D<float2> g_NwbGiBackDistance;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_GI_GROUP_SIZE, NWB_GI_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint probeCount = nwbGiProbeCount(g_NwbGiGrid);
    const uint tile = NWB_GI_DISTANCE_ATLAS_TILE;
    const uint interior = NWB_GI_DISTANCE_ATLAS_INTERIOR;
    const uint border = 1u;

    // One thread per interior texel of one probe tile. The dispatch grid is (probeCount * interior, interior).
    const uint texelX = dispatchThreadID.x;
    const uint texelY = dispatchThreadID.y;
    if(texelX >= probeCount * interior || texelY >= interior)
        return;

    const uint probeIndex = texelX / interior;
    const uint localX = texelX % interior;
    if(probeIndex >= probeCount)
        return;

    // The texel's atlas coordinate (interior offset by the border).
    const uint2 atlasCoord = nwbGiAtlasTileOrigin(probeIndex, tile) + uint2(localX + border, texelY + border);

    // Reconstruct the direction this texel represents.
    const float2 uv = (float2(localX, texelY) + 0.5) / float(interior);
    const float3 texelDir = nwbGiOctahedralDecode(uv);

    // Round-robin: only updated probes have fresh ray-data this frame. A non-updated probe pass-through-copies its
    // front value into the back atlas so its history is preserved exactly.
    const bool updated = nwbGiProbeActiveThisFrame(probeIndex, g_NwbGiGrid);
    if(!updated){
        g_NwbGiBackDistance[atlasCoord] = g_NwbGiFrontDistance[atlasCoord];
        return;
    }

    // Clamp hitT to ~1.5x the grid diagonal so a stray far miss does not poison the distance statistics. The grid
    // diagonal = length(dims * spacing); the 1.5x slack matches the plan.
    const float3 dims = float3(g_NwbGiGrid.gridDims.xyz);
    const float3 spacing = g_NwbGiGrid.gridSpacing.xyz;
    const float gridDiagonal = length(dims * spacing);
    const float tClamp = gridDiagonal * 1.5;

    // Gather the mean + mean-squared hit distance over the rays matching this texel's hemisphere lobe.
    const uint raysPerProbe = uint(g_NwbGiGrid.params0.x);
    const float2 rotation = nwbGiFrameRotation(uint(g_NwbGiGrid.params0.z));
    float distSum = 0.0;
    float distSqSum = 0.0;
    float weightSum = 0.0;
    for(uint rayIndex = 0u; rayIndex < raysPerProbe; ++rayIndex){
        const float3 rayDir = nwbGiFibonacciDirection(rayIndex, raysPerProbe, rotation);
        const float weight = max(dot(texelDir, rayDir), 0.0);
        if(weight <= 0.0)
            continue;
        const float4 rayResult = g_NwbGiRayData.Load(int3(int(probeIndex), int(rayIndex), 0));
        const float hitT = clamp(rayResult.a, 0.0, tClamp);
        distSum += hitT * weight;
        distSqSum += hitT * hitT * weight;
        weightSum += weight;
    }

    float meanDist = 0.0;
    float meanDistSq = 0.0;
    if(weightSum > 0.0){
        meanDist = distSum / weightSum;
        meanDistSq = distSqSum / weightSum;
    }

    // EMA blend against the front (history-in) atlas. Hysteresis is 0 on frame 0 (pure current).
    const float hysteresis = g_NwbGiGrid.params1.x;
    const float2 history = g_NwbGiFrontDistance[atlasCoord];
    const float blendedMean = lerp(meanDist, history.x, hysteresis);
    const float blendedMeanSq = lerp(meanDistSq, history.y, hysteresis);

    g_NwbGiBackDistance[atlasCoord] = float2(blendedMean, blendedMeanSq);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
