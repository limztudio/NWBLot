// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "resolve_binding_slots.h"
#include "../bindless/bindless_heap.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Splat-space temporal EMA decay (pre-splat, per texel): accum_N = decay*accum_{N-1} + photons; resolve normalizes by
// (1-decay) so static brightness is unchanged while motion smooths. No reprojection warp: cannot ghost. Per RGB layer.

struct NwbCausticAccumulatorDecayPushConstants{
    uint width;        // accumulator width
    uint height;       // accumulator height
    float decayFactor; // per-frame EMA decay in [0,1)
    uint accumulatorStorageSlot;
};

[[vk::push_constant]]
ConstantBuffer<NwbCausticAccumulatorDecayPushConstants> g_NwbCausticAccumulatorDecayPushConstants;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_CAUSTIC_RESOLVE_GROUP_SIZE, NWB_CAUSTIC_RESOLVE_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint2 texel = dispatchThreadID.xy;
    if(texel.x >= g_NwbCausticAccumulatorDecayPushConstants.width || texel.y >= g_NwbCausticAccumulatorDecayPushConstants.height)
        return;

    const float decayFactor = g_NwbCausticAccumulatorDecayPushConstants.decayFactor;

    // One layer per RGB flux channel. Multiply the fixed-point integer by decayFactor and write it back (truncation to
    // uint is the intended EMA quantization -- values below 1/decay eventually fall to 0, so a stale sparkle fades out).
    [unroll]
    for(uint layer = 0u; layer < NWB_CAUSTIC_ACCUMULATOR_CHANNEL_COUNT; ++layer){
        const uint3 coord = uint3(texel, layer);
        const uint value = NwbHeapStorageImage2DArrayUint(g_NwbCausticAccumulatorDecayPushConstants.accumulatorStorageSlot)[coord];
        NwbHeapStorageImage2DArrayUint(g_NwbCausticAccumulatorDecayPushConstants.accumulatorStorageSlot)[coord] = (uint)((float)value * decayFactor);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

