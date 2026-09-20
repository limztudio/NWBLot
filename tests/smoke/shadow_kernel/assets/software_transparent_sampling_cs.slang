// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "bindless/bindless_heap.slangi"
#include "shadow/software_transparent_sampling.slangi"

struct NwbSamplingTestPush{
    uint inputSlot;
    uint outputSlot;
    uint count;
};
[[vk::push_constant]] ConstantBuffer<NwbSamplingTestPush> g_NwbSamplingTest;

[numthreads(64, 1, 1)]
void main(const uint3 threadId : SV_DispatchThreadID){
    if(threadId.x >= g_NwbSamplingTest.count)
        return;
    const ByteAddressBuffer source = NwbHeapRawBuffer(g_NwbSamplingTest.inputSlot);
    const uint base = threadId.x * 96u;
    const float4 world = asfloat(source.Load4(base));
    const half4 current = half4(asfloat(source.Load4(base + 16u)));
    const half4 previous = half4(asfloat(source.Load4(base + 32u)));
    const half3 moments = half3(asfloat(source.Load3(base + 48u)));
    const uint4 coordinates = source.Load4(base + 64u);
    const uint2 controls = source.Load2(base + 80u);
    const float4x4 matrix = float4x4(1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, world.w);
    int2 previousPixel;
    const bool accepted = nwbSwTransparentSamplingReproject(coordinates.xy, coordinates.zw, world.xyz,
        matrix, controls.x != 0u, previousPixel) && nwbSwTransparentSamplingGeometryMatches(current, previous);
    const uint count = nwbSwTransparentSamplingBudget(accepted, moments, controls.y);
    g_NwbHeapStorageBuffers[g_NwbSamplingTest.outputSlot].Store4(threadId.x * 16u,
        uint4(count, accepted ? 1u : 0u, uint2(previousPixel)));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

