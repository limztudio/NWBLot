// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "resolve_binding_slots.h"
#include "../bindless/bindless_heap.slangi"
#include "../scene/binding_slots.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Half-res geometry pack for the caustic resolve: one 2x point sample per half texel bakes xyz = world position,
// w = receiver validity. Same taps as per-tap full-res reads, minus the redundant full-res bandwidth.

struct NwbCausticGeometryDownsamplePushConstants{
    uint width;       // full-res G-buffer width
    uint height;      // full-res G-buffer height
    uint halfWidth;   // half-res output width
    uint halfHeight;  // half-res output height
    uint worldPositionSlot;
    uint depthSlot;
    uint outputStorageSlot;
    uint pad1;
};

[[vk::push_constant]]
ConstantBuffer<NwbCausticGeometryDownsamplePushConstants> g_NwbCausticGeometryDownsamplePushConstants;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_CAUSTIC_RESOLVE_GROUP_SIZE, NWB_CAUSTIC_RESOLVE_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint2 halfTexel = dispatchThreadID.xy;
    if(halfTexel.x >= g_NwbCausticGeometryDownsamplePushConstants.halfWidth || halfTexel.y >= g_NwbCausticGeometryDownsamplePushConstants.halfHeight)
        return;

    // Top-left full-res texel of this half-res pixel's 2x2 block (clamped), matching the resolve's half->full point sample.
    const int2 full = int2(
        min((int)halfTexel.x * 2, (int)g_NwbCausticGeometryDownsamplePushConstants.width - 1),
        min((int)halfTexel.y * 2, (int)g_NwbCausticGeometryDownsamplePushConstants.height - 1)
    );

    // This copy is RGBA16F -> RGBA16F. Keep the world-position transport as native half and widen only at the typed storage
    // boundary, which avoids retaining a redundant float payload in this pure copy path.
    const half3 world = half3(NwbHeapSampledImage2D(g_NwbCausticGeometryDownsamplePushConstants.worldPositionSlot).Load(int3(full, 0)).xyz);
    const float depth = NwbHeapSampledImage2D(g_NwbCausticGeometryDownsamplePushConstants.depthSlot).Load(int3(full, 0)).r;
    const float validity = (depth >= NWB_SCENE_BACKGROUND_DEPTH) ? 0.0 : 1.0; // background (no receiver) -> 0
    NwbHeapStorageImage2DFloat4(g_NwbCausticGeometryDownsamplePushConstants.outputStorageSlot)[halfTexel] = float4(float3(world), validity);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

