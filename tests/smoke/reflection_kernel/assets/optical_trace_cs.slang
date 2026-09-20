// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_BINDLESS_TLAS 1

#include "bindless/bindless_heap.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NwbOpticalTraceTestInput{
    float4 originTMin;
    float4 directionTMax;
    uint4 slots;
    uint4 limits;
};

struct NwbOpticalTraceTestPush{
    uint inputSlot;
    uint outputSlot;
    uint outputOffset;
    uint reserved;
};

[[vk::push_constant]]
ConstantBuffer<NwbOpticalTraceTestPush> g_NwbOpticalTraceTestPush;

#pragma warning(push)
#pragma warning(disable: 39001)
[[vk::binding(NWB_BINDLESS_HEAP_BINDING_UNIFORM_BUFFER, NWB_BINDLESS_HEAP_RESOURCE_SET)]]
ConstantBuffer<NwbOpticalTraceTestInput, Std140DataLayout> g_NwbOpticalTraceTestInputs[];
#pragma warning(pop)
#define g_NwbOpticalTraceTestInput g_NwbOpticalTraceTestInputs[g_NwbOpticalTraceTestPush.inputSlot]

uint nwbMeshFrameMaterialTypedHeapSlot(){ return g_NwbOpticalTraceTestInput.slots.w; }
uint nwbMeshFrameInstanceHeapSlot(){ return g_NwbOpticalTraceTestInput.slots.z; }
uint nwbMeshFrameViewHeapSlot(){ return 0xffffffffu; }
#include "bindless/bindless_heap_mesh.slangi"
#include "raytrace/optical_transport.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


half3 nwbOpticalSurfaceRadiance(const NwbRayTraceSurfaceHit hit, const float3 observerDirection){
    // Binary fractions make the independent alpha oracle exact while exposing instance selection and ray normalization.
    return half3(float3(0.25 + 0.125 * float(hit.instance % 3u), 0.5 + 0.25 * observerDirection.z, 0.125 + 0.125 * float(hit.instance % 2u)));
}

half3 nwbOpticalEnvironmentRadiance(const float3 direction){
    return half3(0.125h, 0.25h, 0.5h) * half(0.5 + 0.5 * direction.z);
}

[numthreads(8, 1, 1)]
void main(const uint3 dispatchId : SV_DispatchThreadID){
    if(dispatchId.x != 0u)
        return;
    const NwbOpticalTraceTestInput input = g_NwbOpticalTraceTestInput;
    const NwbOpticalTraceConfig config = { input.slots.x, input.slots.y, input.limits.x, input.limits.y, input.originTMin.w, input.directionTMax.w };
    const NwbOpticalTraceResult result = nwbOpticalTrace(config, input.originTMin.xyz, input.directionTMax.xyz);
    RWByteAddressBuffer output = g_NwbHeapStorageBuffers[g_NwbOpticalTraceTestPush.outputSlot];
    const uint offset = g_NwbOpticalTraceTestPush.outputOffset;
    output.Store4(offset, uint4(asuint(float3(result.radiance)), asuint(result.distance)));
    output.Store4(offset + 16u, uint4(result.queryCount, result.bootstrapEventCount, result.tirCount, result.termination));
    output.Store4(offset + 32u, uint4(result.transparentPath ? 1u : 0u, result.surfaceHit ? 1u : 0u, 0u, 0u));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

