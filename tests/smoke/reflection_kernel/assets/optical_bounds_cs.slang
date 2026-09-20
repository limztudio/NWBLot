// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "raytrace/optical_bounds.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NwbOpticalBoundsTestPushConstants{
    uint inputSlot;
    uint outputSlot;
    uint caseCount;
};

[[vk::push_constant]]
ConstantBuffer<NwbOpticalBoundsTestPushConstants> g_NwbOpticalBoundsTest;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID){
    if(id.x >= g_NwbOpticalBoundsTest.caseCount)
        return;
    const uint offset = id.x * 80u;
    const ByteAddressBuffer input = NwbHeapRawBuffer(g_NwbOpticalBoundsTest.inputSlot);
    const float4 origin = asfloat(input.Load4(offset));
    const float4 direction = asfloat(input.Load4(offset + 16u));
    const float3 boundsMin = asfloat(input.Load3(offset + 32u));
    const float3 boundsMax = asfloat(input.Load3(offset + 48u));
    const uint2 control = input.Load2(offset + 64u);
    const float lengthSquared = dot(direction.xyz, direction.xyz);
    const float3 normalized = isfinite(lengthSquared) && lengthSquared > 0.0 ? normalize(direction.xyz) : direction.xyz;
    const bool outside = nwbOpticalSegmentOutsideBounds(origin.xyz, normalized, direction.w, boundsMin, boundsMax);
    const bool exterior = nwbOpticalSegmentProvenExterior(control.x, origin.xyz, direction.xyz, origin.w, direction.w, control.y);
    g_NwbHeapStorageBuffers[g_NwbOpticalBoundsTest.outputSlot].Store2(id.x * 8u, uint2(outside ? 1u : 0u, exterior ? 1u : 0u));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

