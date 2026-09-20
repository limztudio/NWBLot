// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_BINDLESS_TLAS 1
#include "bindless/bindless_heap.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NwbRefractionHitInput{
    float4 originTMin;
    float4 directionTMax;
    uint4 slots;
    uint4 operation;
};

struct NwbRefractionHitPush{
    uint inputSlot;
    uint outputSlot;
    uint outputOffset;
    uint reserved;
};
[[vk::push_constant]] ConstantBuffer<NwbRefractionHitPush> g_NwbRefractionHitPush;
#pragma warning(push)
#pragma warning(disable: 39001)
[[vk::binding(NWB_BINDLESS_HEAP_BINDING_UNIFORM_BUFFER, NWB_BINDLESS_HEAP_RESOURCE_SET)]]
ConstantBuffer<NwbRefractionHitInput, Std140DataLayout> g_NwbRefractionHitInputs[];
#pragma warning(pop)
#define g_NwbRefractionHitInput g_NwbRefractionHitInputs[g_NwbRefractionHitPush.inputSlot]

uint nwbMeshFrameMaterialTypedHeapSlot(){ return g_NwbRefractionHitInput.slots.z; }
uint nwbMeshFrameInstanceHeapSlot(){ return g_NwbRefractionHitInput.slots.y; }
uint nwbMeshFrameViewHeapSlot(){ return 0xffffffffu; }
#include "bindless/bindless_heap_mesh.slangi"
#if NWB_TEST_REFRACTION_GEOMETRY_FIRST
#include "refraction/interface_hit.slangi"
#else
#include "reference/surface_hit.slangi"
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(8, 1, 1)]
void main(const uint3 dispatchId : SV_DispatchThreadID){
    if(dispatchId.x != 0u)
        return;
    const NwbRefractionHitInput input = g_NwbRefractionHitInput;
    NwbRayTraceSurfaceHit hit;
#if NWB_TEST_REFRACTION_GEOMETRY_FIRST
    if(input.operation.x == 1u)
        hit = nwbRefractionClosestEntryInterface(input.slots.x, input.originTMin.xyz, input.directionTMax.xyz, input.originTMin.w, input.directionTMax.w);
    else if(input.operation.x == 2u)
        hit = nwbRefractionClosestExitInterface(input.slots.x, input.originTMin.xyz, input.directionTMax.xyz, input.originTMin.w, input.directionTMax.w, input.operation.y);
    else
        hit = nwbRayTraceClosestSurfaceHit(input.slots.x, input.originTMin.xyz, input.directionTMax.xyz, input.originTMin.w, input.directionTMax.w);
#else
    hit = nwbRayTraceClosestSurfaceHit(input.slots.x, input.originTMin.xyz, input.directionTMax.xyz, input.originTMin.w, input.directionTMax.w);
    // Frozen consumer admission: compare only the result observable after the existing screen-fallback decision.
    if((input.operation.x == 1u && (!hit.hit || !hit.frontFace))
        || (input.operation.x == 2u && (!hit.hit || hit.instance != input.operation.y || hit.frontFace)))
        hit = (NwbRayTraceSurfaceHit)0;
#endif
    // The original IOR comparisons, including their behavior for nonfinite material values, are not changed.
    if(input.operation.x == 1u && (!hit.hit || float(hit.surface.refractionIor) <= 1.001
        || abs(float(hit.surface.refractionIor) - asfloat(input.operation.z)) > 0.02))
        hit = (NwbRayTraceSurfaceHit)0;
    RWByteAddressBuffer output = g_NwbHeapStorageBuffers[g_NwbRefractionHitPush.outputSlot];
    const uint offset = g_NwbRefractionHitPush.outputOffset;
    output.Store4(offset, uint4(hit.hit ? 1u : 0u, hit.frontFace ? 1u : 0u, hit.instance, hit.primitive));
    output.Store4(offset + 16u, uint4(hit.flags, hit.shadingModelId, asuint(hit.distance), 0u));
    output.Store4(offset + 32u, asuint(float4(hit.position, hit.geometricNormal.x)));
    output.Store4(offset + 48u, asuint(float4(hit.geometricNormal.yz, hit.surface.normal.xy)));
    output.Store4(offset + 64u, asuint(float4(hit.surface.normal.z, float3(hit.surface.baseColor))));
    output.Store4(offset + 80u, asuint(float4(hit.surface.param0, hit.surface.param1, hit.surface.refractionIor, hit.surface.renderCoverage)));
    output.Store4(offset + 96u, asuint(float4(float3(hit.surface.shadowAbsorptionTint), hit.surface.perceptualRoughness)));
    output.Store4(offset + 112u, uint4(asuint(float3(hit.surface.specularF0)), 0u));
    // Word32 is the material-hook counter written at the actual reconstruction call site; leave it untouched here.
    // Completion/guard markers are not query counters; the one-query contract remains in the actual shared source.
    output.Store3(offset + 132u, uint3(1u, 0u, 0u));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

