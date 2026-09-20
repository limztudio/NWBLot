// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "bindless_resources.slangi"
#include "constants.h"
#include "../mesh/runtime_bounds_constants.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NwbSkinnedMeshLocalBoundsPushConstants{
    uint4 payload0;
};

[[vk::push_constant]]
ConstantBuffer<NwbSkinnedMeshLocalBoundsPushConstants> g_NwbSkinnedMeshLocalBoundsPush;

groupshared float3 g_NwbSkinnedMeshLocalBoundsMin[NWB_SKINNED_MESH_BOUNDS_GROUP_SIZE_X];
groupshared float3 g_NwbSkinnedMeshLocalBoundsMax[NWB_SKINNED_MESH_BOUNDS_GROUP_SIZE_X];
groupshared uint g_NwbSkinnedMeshLocalBoundsInvalid[NWB_SKINNED_MESH_BOUNDS_GROUP_SIZE_X];


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_SKINNED_MESH_BOUNDS_GROUP_SIZE_X, 1, 1)]
void main(uint3 groupThreadID : SV_GroupThreadID){
    const uint lane = groupThreadID.x;
    const uint meshletCount = g_NwbSkinnedMeshLocalBoundsPush.payload0[NWB_SKINNED_MESH_BOUNDS_PUSH_MESHLET_COUNT];
    const uint slots = g_NwbSkinnedMeshLocalBoundsPush.payload0[NWB_SKINNED_MESH_BOUNDS_PUSH_BINDLESS_RESOURCES_SLOT];
    const uint4 boundsSlots = nwbSkinnedMeshHeapBindlessResources(slots).streamSlots4;
    const float largest = asfloat(0x7f7fffffu);
    float3 minimum = float3(largest);
    float3 maximum = float3(-largest);
    uint invalid = 0u;
    for(uint index = lane; index < meshletCount; index += NWB_SKINNED_MESH_BOUNDS_GROUP_SIZE_X){
        const uint byteOffset = index * NWB_RUNTIME_MESH_BOUNDS_BYTE_SIZE;
        const uint flags = NwbHeapRawBuffer(boundsSlots.x).Load(byteOffset + NWB_RUNTIME_MESH_BOUNDS_FLAGS_OFFSET);
        const float3 partialMin = asfloat(NwbHeapRawBuffer(boundsSlots.x).Load3(byteOffset + NWB_RUNTIME_MESH_BOUNDS_MIN_OFFSET));
        const float3 partialMax = asfloat(NwbHeapRawBuffer(boundsSlots.x).Load3(byteOffset + NWB_RUNTIME_MESH_BOUNDS_MAX_OFFSET));
        const bool valid = (flags & NWB_RUNTIME_MESH_BOUNDS_FLAG_VALID) != 0u
            && all(isfinite(partialMin)) && all(isfinite(partialMax)) && all(partialMin <= partialMax);
        invalid |= valid ? 0u : 1u;
        if(valid){
            minimum = min(minimum, partialMin);
            maximum = max(maximum, partialMax);
        }
    }
    g_NwbSkinnedMeshLocalBoundsMin[lane] = minimum;
    g_NwbSkinnedMeshLocalBoundsMax[lane] = maximum;
    g_NwbSkinnedMeshLocalBoundsInvalid[lane] = invalid;
    GroupMemoryBarrierWithGroupSync();
    for(uint stride = NWB_SKINNED_MESH_BOUNDS_GROUP_SIZE_X >> 1u; stride > 0u; stride >>= 1u){
        if(lane < stride){
            g_NwbSkinnedMeshLocalBoundsMin[lane] = min(g_NwbSkinnedMeshLocalBoundsMin[lane], g_NwbSkinnedMeshLocalBoundsMin[lane + stride]);
            g_NwbSkinnedMeshLocalBoundsMax[lane] = max(g_NwbSkinnedMeshLocalBoundsMax[lane], g_NwbSkinnedMeshLocalBoundsMax[lane + stride]);
            g_NwbSkinnedMeshLocalBoundsInvalid[lane] |= g_NwbSkinnedMeshLocalBoundsInvalid[lane + stride];
        }
        GroupMemoryBarrierWithGroupSync();
    }
    if(lane != 0u)
        return;

    // Cover float rounding and subnormal flush before a consumer applies an arbitrary world-space scale.
    float3 lower = g_NwbSkinnedMeshLocalBoundsMin[0u];
    float3 upper = g_NwbSkinnedMeshLocalBoundsMax[0u];
    const float3 margin = max(max(abs(lower), abs(upper)) * 4.76837158203125e-7, float3(asfloat(0x00800000u)));
    lower -= margin;
    upper += margin;
    const bool valid = meshletCount != 0u && g_NwbSkinnedMeshLocalBoundsInvalid[0u] == 0u
        && all(isfinite(lower)) && all(isfinite(upper)) && all(lower <= upper);
    RWByteAddressBuffer output = nwbSkinnedMeshHeapWritableRawBuffer(boundsSlots.y);
    output.Store3(NWB_RUNTIME_MESH_BOUNDS_MIN_OFFSET, asuint(valid ? lower : float3(0.0)));
    output.Store(NWB_RUNTIME_MESH_BOUNDS_FLAGS_OFFSET, valid ? NWB_RUNTIME_MESH_BOUNDS_FLAG_VALID : 0u);
    output.Store3(NWB_RUNTIME_MESH_BOUNDS_MAX_OFFSET, asuint(valid ? upper : float3(0.0)));
    output.Store(NWB_RUNTIME_MESH_BOUNDS_RESERVED_OFFSET, 0u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

