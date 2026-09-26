// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shadow_resolve_binding_slots.h"
#include "constants.h"
#include "sw_binding_slots.h" // NWB_SW_SHADOW_SOFT_FACTOR -- the half-res downscale, shared with the trace + resolve.
#include "shadow_heap_resources.slangi"
#include "../common/math.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Half-res geometry pack for the soft-shadow resolve: one 2x point sample per half texel bakes .xy = octahedral normal,
// .z = camera distance (edge-stop proxy), .w = receiver validity. Same taps as per-tap full-res reads, minus the redundant bandwidth.

struct NwbShadowGeometryDownsamplePushConstants{
    uint width;       // full-res G-buffer width
    uint height;      // full-res G-buffer height
    uint halfWidth;   // half-res output width
    uint halfHeight;  // half-res output height
    // Global heap SampledImage slots select the full-resolution G-buffer inputs.
    uint worldPositionSlot;
    uint normalSlot;
    uint depthSlot;
    uint outputStorageSlot;
    uint sceneShadingSlot;
    uint _pad0;
};

[[vk::push_constant]]
ConstantBuffer<NwbShadowGeometryDownsamplePushConstants> g_NwbShadowGeometryDownsamplePushConstants;

#define NWB_SCENE_SHADING_HEAP_SLOT g_NwbShadowGeometryDownsamplePushConstants.sceneShadingSlot
// This pass never reads lights, but scene/buffer.slangi requires a valid heap expression for the shared helper ABI.
#define NWB_SCENE_LIGHT_LIST_HEAP_SLOT 0u
#include "../scene/buffer.slangi"

#define g_NwbShadowGeometryOutput NwbHeapStorageImage2DFloat4(g_NwbShadowGeometryDownsamplePushConstants.outputStorageSlot)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_SHADOW_RESOLVE_GROUP_SIZE, NWB_SHADOW_RESOLVE_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint2 halfTexel = dispatchThreadID.xy;
    if(halfTexel.x >= g_NwbShadowGeometryDownsamplePushConstants.halfWidth || halfTexel.y >= g_NwbShadowGeometryDownsamplePushConstants.halfHeight)
        return;

    // Top-left full-res texel of this half-res pixel's 2x2 block (clamped), matching the trace's + resolve's 2x sample.
    const int2 full = int2(
        min((int)halfTexel.x * NWB_SW_SHADOW_SOFT_FACTOR, (int)g_NwbShadowGeometryDownsamplePushConstants.width - 1),
        min((int)halfTexel.y * NWB_SW_SHADOW_SOFT_FACTOR, (int)g_NwbShadowGeometryDownsamplePushConstants.height - 1)
    );

    const float depth = NwbHeapSampledImage2D(g_NwbShadowGeometryDownsamplePushConstants.depthSlot).Load(int3(full, 0)).r;
    if(depth >= NWB_SCENE_BACKGROUND_DEPTH){
        // Background (no receiver): validity 0. Normal/distance are irrelevant (the resolve skips invalid taps).
        g_NwbShadowGeometryOutput[halfTexel] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    // Both G-buffer vector attachments are RGBA16F. Preserve their native precision until the full-precision
    // normalization/distance calculations that actually need it.
    const half3 worldSample = half3(NwbHeapSampledImage2D(g_NwbShadowGeometryDownsamplePushConstants.worldPositionSlot).Load(int3(full, 0)).xyz);
    const float3 world = float3(worldSample);
    // The G-buffer normal is stored [0,1]-encoded (matching the SW/HW traces' `n*2-1` decode); normalize the decode so
    // the octahedral pack sees a unit vector.
    const half3 normalSample = half3(NwbHeapSampledImage2D(g_NwbShadowGeometryDownsamplePushConstants.normalSlot).Load(int3(full, 0)).xyz);
    const float3 normal = normalize(float3(normalSample) * 2.0 - 1.0);
    const half2 octNormal = half2(nwbOctEncodeNormal(normal));
    const float cameraDistance = length(world - g_NwbSceneCameraPosition.xyz);

    g_NwbShadowGeometryOutput[halfTexel] = float4(float2(octNormal), cameraDistance, 1.0);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

