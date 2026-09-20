// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "optical_bounds_finalize_constants.h"
#include "optical_scene_constants.h"
#include "../mesh/runtime_bounds_constants.h"
#include "../bindless/bindless_heap.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NwbOpticalBoundsFinalizePushConstants{
    uint sourceSceneSlot;
    uint runtimeInputsSlot;
    uint outputSceneSlot;
    uint instanceCount;
    uint runtimeCount;
    uint staticBoundsComplete;
};

[[vk::push_constant]]
ConstantBuffer<NwbOpticalBoundsFinalizePushConstants> g_NwbOpticalBoundsFinalize;

groupshared float3 g_NwbOpticalMinimum[NWB_OPTICAL_BOUNDS_FINALIZE_GROUP_SIZE];
groupshared float3 g_NwbOpticalMaximum[NWB_OPTICAL_BOUNDS_FINALIZE_GROUP_SIZE];
groupshared uint g_NwbOpticalInvalid[NWB_OPTICAL_BOUNDS_FINALIZE_GROUP_SIZE];


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool nwbOpticalTransformRuntimeBounds(
    const uint inputOffset,
    out float3 boundsMin,
    out float3 boundsMax){
    boundsMin = float3(0.0);
    boundsMax = float3(0.0);
    const ByteAddressBuffer inputs = NwbHeapRawBuffer(g_NwbOpticalBoundsFinalize.runtimeInputsSlot);
    const uint localSlot = inputs.Load(inputOffset + NWB_OPTICAL_BOUNDS_RUNTIME_SLOT_OFFSET);
    if(localSlot == 0xffffffffu)
        return false;
    const ByteAddressBuffer localBounds = NwbHeapRawBuffer(localSlot);
    if((localBounds.Load(NWB_RUNTIME_MESH_BOUNDS_FLAGS_OFFSET) & NWB_RUNTIME_MESH_BOUNDS_FLAG_VALID) == 0u)
        return false;
    const float3 localMin = asfloat(localBounds.Load3(NWB_RUNTIME_MESH_BOUNDS_MIN_OFFSET));
    const float3 localMax = asfloat(localBounds.Load3(NWB_RUNTIME_MESH_BOUNDS_MAX_OFFSET));
    if(!all(isfinite(localMin)) || !all(isfinite(localMax)) || any(localMin > localMax))
        return false;
    for(uint rowIndex = 0u; rowIndex < 3u; ++rowIndex){
        const float4 row = asfloat(inputs.Load4(inputOffset + rowIndex * 16u));
        if(!all(isfinite(row)))
            return false;
        const float3 first = row.xyz * localMin;
        const float3 second = row.xyz * localMax;
        const float3 low = min(first, second);
        const float3 high = max(first, second);
        const float3 absolute = max(abs(first), abs(second));
        const float magnitude = abs(row.w) + absolute.x + absolute.y + absolute.z + 1.0;
        // Enclose both the bounds arithmetic and the frozen float affine transform used by traversal.
        // The unit floor covers flushed subnormals; any overflow leaves the entire union unproven.
        const float margin = magnitude * (16.0 * 1.1920928955078125e-7);
        boundsMin[rowIndex] = ((row.w + low.x) + low.y) + low.z - margin;
        boundsMax[rowIndex] = ((row.w + high.x) + high.y) + high.z + margin;
        if(
            !all(isfinite(first)) || !all(isfinite(second)) || !isfinite(magnitude) || !isfinite(margin)
            || !isfinite(boundsMin[rowIndex]) || !isfinite(boundsMax[rowIndex])
        )
            return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_OPTICAL_BOUNDS_FINALIZE_GROUP_SIZE, 1, 1)]
void main(uint3 groupThreadId : SV_GroupThreadID){
    const uint lane = groupThreadId.x;
    const ByteAddressBuffer source = NwbHeapRawBuffer(g_NwbOpticalBoundsFinalize.sourceSceneSlot);
    const RWByteAddressBuffer destination = g_NwbHeapStorageBuffers[g_NwbOpticalBoundsFinalize.outputSceneSlot];
    for(uint index = lane; index < g_NwbOpticalBoundsFinalize.instanceCount; index += NWB_OPTICAL_BOUNDS_FINALIZE_GROUP_SIZE){
        const uint offset = NWB_RT_OPTICAL_SCENE_HEADER_BYTES + index * NWB_RT_OPTICAL_INSTANCE_BYTES;
        destination.Store4(offset, source.Load4(offset));
    }
    DeviceMemoryBarrierWithGroupSync();

    const float largest = 3.402823466e+38;
    float3 minimum = float3(largest);
    float3 maximum = float3(-largest);
    uint invalid = g_NwbOpticalBoundsFinalize.staticBoundsComplete == 0u ? 1u : 0u;
    const uint transparentCount = source.Load(NWB_RT_OPTICAL_SCENE_TRANSPARENT_COUNT_OFFSET);
    if(g_NwbOpticalBoundsFinalize.runtimeCount > transparentCount || transparentCount > g_NwbOpticalBoundsFinalize.instanceCount)
        invalid = 1u;
    if(lane == 0u && transparentCount > g_NwbOpticalBoundsFinalize.runtimeCount){
        minimum = asfloat(source.Load3(NWB_RT_OPTICAL_SCENE_BOUNDS_MIN_OFFSET));
        maximum = asfloat(source.Load3(NWB_RT_OPTICAL_SCENE_BOUNDS_MAX_OFFSET));
        if(!all(isfinite(minimum)) || !all(isfinite(maximum)) || any(minimum > maximum))
            invalid = 1u;
    }
    const ByteAddressBuffer inputs = NwbHeapRawBuffer(g_NwbOpticalBoundsFinalize.runtimeInputsSlot);
    for(uint index = lane; index < g_NwbOpticalBoundsFinalize.runtimeCount; index += NWB_OPTICAL_BOUNDS_FINALIZE_GROUP_SIZE){
        const uint inputOffset = index * NWB_OPTICAL_BOUNDS_RUNTIME_INPUT_BYTES;
        const uint instanceIndex = inputs.Load(inputOffset + NWB_OPTICAL_BOUNDS_RUNTIME_INSTANCE_OFFSET);
        if(instanceIndex >= g_NwbOpticalBoundsFinalize.instanceCount){
            invalid = 1u;
            continue;
        }
        const uint flagOffset = NWB_RT_OPTICAL_SCENE_HEADER_BYTES + instanceIndex * NWB_RT_OPTICAL_INSTANCE_BYTES
            + NWB_RT_OPTICAL_INSTANCE_FLAGS_OFFSET;
        uint flags = source.Load(flagOffset) & ~NWB_RT_OPTICAL_INSTANCE_FLAG_BOUNDS_VALID;
        float3 boundsMin = float3(0.0);
        float3 boundsMax = float3(0.0);
        const bool valid = (flags & NWB_RT_OPTICAL_INSTANCE_FLAG_TRANSPARENT) != 0u
            && nwbOpticalTransformRuntimeBounds(inputOffset, boundsMin, boundsMax);
        if(valid){
            minimum = min(minimum, boundsMin);
            maximum = max(maximum, boundsMax);
            flags |= NWB_RT_OPTICAL_INSTANCE_FLAG_BOUNDS_VALID;
        }
        else
            invalid = 1u;
        destination.Store(flagOffset, flags);
    }
    g_NwbOpticalMinimum[lane] = minimum;
    g_NwbOpticalMaximum[lane] = maximum;
    g_NwbOpticalInvalid[lane] = invalid;
    GroupMemoryBarrierWithGroupSync();
    for(uint stride = NWB_OPTICAL_BOUNDS_FINALIZE_GROUP_SIZE / 2u; stride != 0u; stride >>= 1u){
        if(lane < stride){
            g_NwbOpticalMinimum[lane] = min(g_NwbOpticalMinimum[lane], g_NwbOpticalMinimum[lane + stride]);
            g_NwbOpticalMaximum[lane] = max(g_NwbOpticalMaximum[lane], g_NwbOpticalMaximum[lane + stride]);
            g_NwbOpticalInvalid[lane] |= g_NwbOpticalInvalid[lane + stride];
        }
        GroupMemoryBarrierWithGroupSync();
    }
    if(lane == 0u){
        const bool complete = g_NwbOpticalInvalid[0] == 0u;
        const float3 finalMin = complete && transparentCount != 0u ? g_NwbOpticalMinimum[0] : float3(0.0);
        const float3 finalMax = complete && transparentCount != 0u ? g_NwbOpticalMaximum[0] : float3(0.0);
        const uint flags = (source.Load(NWB_RT_OPTICAL_SCENE_FLAGS_OFFSET) & ~NWB_RT_OPTICAL_SCENE_FLAG_BOUNDS_VALID)
            | (complete ? NWB_RT_OPTICAL_SCENE_FLAG_BOUNDS_VALID : 0u);
        destination.Store4(0u, uint4(asuint(finalMin), transparentCount));
        destination.Store4(16u, uint4(asuint(finalMax), flags));
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

