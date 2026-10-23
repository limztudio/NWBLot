// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "resolve_binding_slots.h"
#include "../bindless/bindless_heap.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Splat-space temporal EMA decay (pre-splat, per texel): resolve divides by (1-decay) so static brightness holds while motion smooths. No reprojection warp, so no ghosting. Per RGB layer.

struct NwbCausticAccumulatorDecayPushConstants{
    uint width;
    uint height;
    float decayFactor; // per-frame EMA decay in [0,1)
    uint accumulatorStorageSlot;
};

[[vk::push_constant]] ConstantBuffer<NwbCausticAccumulatorDecayPushConstants> g_NwbCausticAccumulatorDecayPushConstants;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_CAUSTIC_RESOLVE_GROUP_SIZE, NWB_CAUSTIC_RESOLVE_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint2 texel = dispatchThreadID.xy;
    if(texel.x >= g_NwbCausticAccumulatorDecayPushConstants.width || texel.y >= g_NwbCausticAccumulatorDecayPushConstants.height)
        return;

    // Decay is a (0, 1) bounded EMA factor, so half math suffices; widen only at the float fixed-point multiply.
    const half decayFactor = half(g_NwbCausticAccumulatorDecayPushConstants.decayFactor);

    // One layer per RGB flux channel. Truncation to uint is the intended EMA quantization: values below 1/decay fall to 0, so stale sparkle fades out.
    [unroll] for(uint layer = 0u; layer < NWB_CAUSTIC_ACCUMULATOR_CHANNEL_COUNT; ++layer){
        const uint3 coord = uint3(texel, layer);
        const uint value = nwbHeapStorageImage2DArrayUint(g_NwbCausticAccumulatorDecayPushConstants.accumulatorStorageSlot)[coord];
        nwbHeapStorageImage2DArrayUint(g_NwbCausticAccumulatorDecayPushConstants.accumulatorStorageSlot)[coord] = (uint)((float)value * float(decayFactor));
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

