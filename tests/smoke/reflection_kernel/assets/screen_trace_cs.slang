// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "reflection/screen_trace.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(64, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID){
    const uint index = dispatchThreadId.x;
    if(index >= g_NwbReflectionParameters.queueCapacity)
        return;
    const uint byteOffset = index * 48u;
    const uint4 first = NwbHeapRawBuffer(g_NwbReflectionParameters.counterSlot).Load4(byteOffset);
    const uint4 second = NwbHeapRawBuffer(g_NwbReflectionParameters.counterSlot).Load4(byteOffset + 16u);
    const uint4 third = NwbHeapRawBuffer(g_NwbReflectionParameters.counterSlot).Load4(byteOffset + 32u);
    NwbReflectionSurface surface = (NwbReflectionSurface)0;
    surface.position = asfloat(first.xyz);
    surface.normal = asfloat(second.xyz);
    surface.direction = asfloat(third.xyz);
    surface.bias = asfloat(third.w);
    half3 radiance;
    float confidence;
    uint iterations;
    bool limited;
    const bool returned = nwbReflectionTraceScreen(
        uint2(first.w, second.w), surface, radiance, confidence, iterations, limited
    );
    const uint output = index * 32u;
    g_NwbReflectionQueue.Store4(output, uint4(returned ? 1u : 0u, iterations, limited ? 1u : 0u, asuint(confidence)));
    g_NwbReflectionQueue.Store4(output + 16u, uint4(asuint(float3(radiance)), 0x51a7e001u));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

