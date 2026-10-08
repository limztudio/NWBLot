// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shadow_resolve_binding_slots.h"
#include "constants.h"
#include "sw_binding_slots.h"
#include "shadow_heap_resources.slangi"
#include "../common/math.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Receiver geometry packs the octahedral normal in .xy, camera distance in .z, and validity in .w.

struct NwbShadowGeometryDownsamplePushConstants{
    uint width;       // full-res G-buffer width
    uint height;      // full-res G-buffer height
    uint halfWidth;   // receiver-grid output width
    uint halfHeight;  // receiver-grid output height
    // Global heap SampledImage slots select the full-resolution G-buffer inputs.
    uint worldPositionSlot;
    uint normalSlot;
    uint depthSlot;
    uint outputStorageSlot;
    uint sceneShadingSlot;
    uint receiverFactor;
};

[[vk::push_constant]]
ConstantBuffer<NwbShadowGeometryDownsamplePushConstants> g_NwbShadowGeometryDownsamplePushConstants;

#define NWB_SCENE_SHADING_HEAP_SLOT g_NwbShadowGeometryDownsamplePushConstants.sceneShadingSlot
// This pass never reads lights, but scene/buffer.slangi requires a valid heap expression for the shared helper ABI.
#define NWB_SCENE_LIGHT_LIST_HEAP_SLOT 0u
#include "../scene/buffer.slangi"

#define g_NwbShadowGeometryOutput nwbHeapStorageImage2DFloat4(g_NwbShadowGeometryDownsamplePushConstants.outputStorageSlot)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_SHADOW_RESOLVE_GROUP_SIZE, NWB_SHADOW_RESOLVE_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint2 halfTexel = dispatchThreadID.xy;
    if(halfTexel.x >= g_NwbShadowGeometryDownsamplePushConstants.halfWidth || halfTexel.y >= g_NwbShadowGeometryDownsamplePushConstants.halfHeight)
        return;

    // Sample the receiver block's top-left full-resolution texel, matching the traces.
    const int2 full = int2(
        min((int)halfTexel.x * g_NwbShadowGeometryDownsamplePushConstants.receiverFactor, (int)g_NwbShadowGeometryDownsamplePushConstants.width - 1),
        min((int)halfTexel.y * g_NwbShadowGeometryDownsamplePushConstants.receiverFactor, (int)g_NwbShadowGeometryDownsamplePushConstants.height - 1)
    );

    const float depth = nwbHeapSampledImage2D(g_NwbShadowGeometryDownsamplePushConstants.depthSlot).Load(int3(full, 0)).r;
    if(depth >= NWB_SCENE_BACKGROUND_DEPTH){
        // Background (no receiver): validity 0. Normal/distance are irrelevant (the resolve skips invalid taps).
        g_NwbShadowGeometryOutput[halfTexel] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    // Both G-buffer vector attachments are RGBA16F. Preserve their native precision until the full-precision normalization/distance calculations that actually need it.
    const half3 worldSample = half3(nwbHeapSampledImage2D(g_NwbShadowGeometryDownsamplePushConstants.worldPositionSlot).Load(int3(full, 0)).xyz);
    const float3 world = float3(worldSample);
    // The G-buffer normal is stored [0,1]-encoded (matching the SW/HW traces' `n*2-1` decode); normalize the decode so the octahedral pack sees a unit vector.
    const half3 normalSample = half3(nwbHeapSampledImage2D(g_NwbShadowGeometryDownsamplePushConstants.normalSlot).Load(int3(full, 0)).xyz);
    const float3 normal = normalize(float3(normalSample) * 2.0 - 1.0);
    const half2 octNormal = half2(nwbOctEncodeNormal(normal));
    const float cameraDistance = length(world - g_NwbSceneCameraPosition.xyz);

    g_NwbShadowGeometryOutput[halfTexel] = float4(float2(octNormal), cameraDistance, 1.0);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

