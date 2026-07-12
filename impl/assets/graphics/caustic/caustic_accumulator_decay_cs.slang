
#include "resolve_binding_slots.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Splat-space temporal EMA decay pre-pass. Runs BEFORE the producer's photon splat, once per accumulator texel: it
// multiplies the resident R32_UINT fixed-point flux by decayFactor (accum_N = decayFactor*accum_{N-1}), and the producer
// then atomic-adds this frame's photons on top. So the accumulator holds an exponential moving average of the photon
// density across frames -- static steady state = photons/(1-decayFactor) -- which the resolve normalizes by (1-decay)
// so the STATIC caustic brightness is byte-unchanged, while a MOVING caustic is temporally smoothed (the sparkle-flicker
// of a spinning refractor averages out). This is a decay in SPLAT space, NOT an image-space reprojection: there is no
// per-pixel motion warp, so it cannot ghost. Each of the 3 layers (RGB flux) is decayed independently.

[[vk::binding(NWB_CAUSTIC_ACCUMULATOR_DECAY_BINDING_ACCUMULATOR, NWB_CAUSTIC_ACCUMULATOR_DECAY_SET)]]
RWTexture2DArray<uint> g_NwbCausticAccumulatorDecayAccumulator;

struct NwbCausticAccumulatorDecayPushConstants{
    uint width;        // accumulator width
    uint height;       // accumulator height
    float decayFactor; // per-frame EMA decay in [0,1)
    uint pad;
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
    for(uint layer = 0u; layer < 3u; ++layer){
        const uint3 coord = uint3(texel, layer);
        const uint value = g_NwbCausticAccumulatorDecayAccumulator[coord];
        g_NwbCausticAccumulatorDecayAccumulator[coord] = (uint)((float)value * decayFactor);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

