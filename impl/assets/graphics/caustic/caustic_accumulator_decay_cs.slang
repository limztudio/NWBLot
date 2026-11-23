// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "resolve_binding_slots.h"
#include "../bindless/bindless_heap.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NwbCausticAccumulatorDecayPushConstants{
    uint width;
    uint height;
    float decayFactor;
    uint accumulatorStorageSlot;
};

[[vk::push_constant]] ConstantBuffer<NwbCausticAccumulatorDecayPushConstants> g_NwbCausticAccumulatorDecayPushConstants;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_CAUSTIC_RESOLVE_GROUP_SIZE, NWB_CAUSTIC_RESOLVE_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint2 texel = dispatchThreadID.xy;
    if(texel.x >= g_NwbCausticAccumulatorDecayPushConstants.width || texel.y >= g_NwbCausticAccumulatorDecayPushConstants.height)
        return;

    const float decayFactor = g_NwbCausticAccumulatorDecayPushConstants.decayFactor;

    [unroll] for(uint layer = 0u; layer < NWB_CAUSTIC_ACCUMULATOR_CHANNEL_COUNT; ++layer){
        const uint3 coord = uint3(texel, layer);
        const uint value = nwbHeapStorageImage2DArrayUint(g_NwbCausticAccumulatorDecayPushConstants.accumulatorStorageSlot)[coord];
        if(value != 0u){
            const uint decayed = (uint)round(max((float)value * decayFactor, 0.0));
            nwbHeapStorageImage2DArrayUint(g_NwbCausticAccumulatorDecayPushConstants.accumulatorStorageSlot)[coord] = (decayFactor < 1.0) ? min(value - 1u, decayed) : decayed;
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

