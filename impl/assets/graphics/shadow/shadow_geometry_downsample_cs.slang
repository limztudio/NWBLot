// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shadow_resolve_binding_slots.h"
#include "sw_binding_slots.h" // NWB_SW_SHADOW_SOFT_FACTOR -- the half-res downscale, shared with the trace + resolve.
#include "../deferred/bindless_resources.slangi"
#include "../common/math.slangi"

// The pass needs only the camera world position. Fetch the shared scene-shading CB through the same target-generation
// slot cbuffer as deferred lighting, AVBOIT, and surfel GI; scene/buffer.slangi emits typed heap aliases rather than a
// local light-list binding, so its declaration cannot perturb this pass's compact local set.
[[vk::binding(NWB_SHADOW_GEOMETRY_DOWNSAMPLE_BINDING_BINDLESS_RESOURCES, NWB_SHADOW_GEOMETRY_DOWNSAMPLE_SET)]]
ConstantBuffer<NwbDeferredBindlessResources, Std140DataLayout> g_NwbShadowGeometryDownsampleBindlessResources;
#define NWB_SCENE_BUFFERS_FROM_HEAP 1
#define NWB_SCENE_SHADING_HEAP_SLOT g_NwbShadowGeometryDownsampleBindlessResources.avboitSlots.z
#define NWB_SCENE_LIGHT_LIST_HEAP_SLOT g_NwbShadowGeometryDownsampleBindlessResources.avboitSlots.w
#include "../scene/buffer.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Geometry downsample pre-pass for the half-res soft shadow resolve. The resolve's a-trous wavelet + its
// upsample need the receiver's NORMAL + a distance edge-stop at every tap (5x5 taps x N passes), and re-reading the
// full-res normal + world-position G-buffer at the half pixel's 2x location every tap is a scattered, cache-unfriendly
// load. This pass resolves it ONCE: each half-res texel point-samples the full-res G-buffer at its top-left full texel
// and writes a single compact half-res record -- .xy = octahedral-packed receiver normal, .z = world-space distance
// from the camera (a linear, world-unit depth proxy for the edge-stop), .w = receiver validity (1 = receiver, 0 =
// background). It stores the packed normal + camera-distance the shadow edge-stop uses (shadow needs a discontinuity
// metric, not an area Jacobian). Same 2x point sample the resolve's half->full map would use, so no quality change vs
// per-tap full-res reads; it only removes redundant full-res bandwidth.

[[vk::binding(NWB_SHADOW_GEOMETRY_DOWNSAMPLE_BINDING_GEOMETRY_OUTPUT, NWB_SHADOW_GEOMETRY_DOWNSAMPLE_SET)]]
RWTexture2D<float4> g_NwbShadowGeometryOutput;

struct NwbShadowGeometryDownsamplePushConstants{
    uint width;       // full-res G-buffer width
    uint height;      // full-res G-buffer height
    uint halfWidth;   // half-res output width
    uint halfHeight;  // half-res output height
    // Global heap SampledImage slots: the full-res G-buffer inputs no longer consume local set-0 descriptors.
    uint worldPositionSlot;
    uint normalSlot;
    uint depthSlot;
    uint _pad0;
};

[[vk::push_constant]]
ConstantBuffer<NwbShadowGeometryDownsamplePushConstants> g_NwbShadowGeometryDownsamplePushConstants;


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
    if(depth >= 0.999999){
        // Background (no receiver): validity 0. Normal/distance are irrelevant (the resolve skips invalid taps).
        g_NwbShadowGeometryOutput[halfTexel] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    const float3 world = NwbHeapSampledImage2D(g_NwbShadowGeometryDownsamplePushConstants.worldPositionSlot).Load(int3(full, 0)).xyz;
    // The G-buffer normal is stored [0,1]-encoded (matching the SW/HW traces' `n*2-1` decode); normalize the decode so
    // the octahedral pack sees a unit vector.
    const float3 normal = normalize(NwbHeapSampledImage2D(g_NwbShadowGeometryDownsamplePushConstants.normalSlot).Load(int3(full, 0)).xyz * 2.0 - 1.0);
    const float2 octNormal = nwbOctEncodeNormal(normal);
    const float cameraDistance = length(world - g_NwbSceneCameraPosition.xyz);

    g_NwbShadowGeometryOutput[halfTexel] = float4(octNormal.x, octNormal.y, cameraDistance, 1.0);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

