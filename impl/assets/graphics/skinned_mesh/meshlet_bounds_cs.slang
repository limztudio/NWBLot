// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../mesh/meshlet_payload.slangi"
#include "binding_slots.h"
#include "constants.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static const float NWB_SKINNED_MESH_BOUNDS_FLOAT_MAX = 3.402823e+38;
static const float NWB_SKINNED_MESH_BOUNDS_RADIUS_INFLATION = 1.25;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[vk::binding(NWB_SKINNED_MESH_BOUNDS_BINDING_POSITIONS, NWB_SKINNED_MESH_BOUNDS_SET)]] StructuredBuffer<NwbMeshPosition, Std430DataLayout> g_NwbSkinnedMeshBoundsPositions;
[[vk::binding(NWB_SKINNED_MESH_BOUNDS_BINDING_MESHLET_DESC, NWB_SKINNED_MESH_BOUNDS_SET)]] StructuredBuffer<NwbMeshletDesc, Std430DataLayout> g_NwbSkinnedMeshBoundsMeshlets;
[[vk::binding(NWB_SKINNED_MESH_BOUNDS_BINDING_POSITION_REF_DELTAS, NWB_SKINNED_MESH_BOUNDS_SET)]] ByteAddressBuffer g_NwbSkinnedMeshBoundsPositionRefDeltas;
[[vk::binding(NWB_SKINNED_MESH_BOUNDS_BINDING_LOCAL_VERTEX_REFS, NWB_SKINNED_MESH_BOUNDS_SET)]] StructuredBuffer<NwbMeshletLocalVertexRef, Std430DataLayout> g_NwbSkinnedMeshBoundsLocalVertexRefs;
[[vk::binding(NWB_SKINNED_MESH_BOUNDS_BINDING_PRIMITIVE_INDICES, NWB_SKINNED_MESH_BOUNDS_SET)]] ByteAddressBuffer g_NwbSkinnedMeshBoundsPrimitiveIndices;
[[vk::binding(NWB_SKINNED_MESH_BOUNDS_BINDING_DYNAMIC_BOUNDS, NWB_SKINNED_MESH_BOUNDS_SET)]] RWByteAddressBuffer g_NwbSkinnedMeshBoundsDynamicBounds;

struct NwbSkinnedMeshBoundsPushConstants{
    uint4 payload0;
};

[[vk::push_constant]]
ConstantBuffer<NwbSkinnedMeshBoundsPushConstants> g_NwbSkinnedMeshBoundsPushConstants;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


groupshared float3 g_NwbSkinnedMeshBoundsMin[NWB_SKINNED_MESH_BOUNDS_GROUP_SIZE_X];
groupshared float3 g_NwbSkinnedMeshBoundsMax[NWB_SKINNED_MESH_BOUNDS_GROUP_SIZE_X];
groupshared float g_NwbSkinnedMeshBoundsRadiusSq[NWB_SKINNED_MESH_BOUNDS_GROUP_SIZE_X];
groupshared float3 g_NwbSkinnedMeshBoundsAreaNormal[NWB_SKINNED_MESH_BOUNDS_GROUP_SIZE_X];
groupshared float g_NwbSkinnedMeshBoundsConeCutoff[NWB_SKINNED_MESH_BOUNDS_GROUP_SIZE_X];
groupshared uint g_NwbSkinnedMeshBoundsValidPositionCount[NWB_SKINNED_MESH_BOUNDS_GROUP_SIZE_X];
groupshared uint g_NwbSkinnedMeshBoundsValidFaceNormalCount[NWB_SKINNED_MESH_BOUNDS_GROUP_SIZE_X];
groupshared float3 g_NwbSkinnedMeshBoundsCenter;
groupshared float3 g_NwbSkinnedMeshBoundsConeAxis;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


uint nwbSkinnedMeshBoundsMeshletCount(){
    return g_NwbSkinnedMeshBoundsPushConstants.payload0[NWB_SKINNED_MESH_BOUNDS_PUSH_MESHLET_COUNT];
}

uint nwbSkinnedMeshBoundsLoadPrimitiveIndex(const uint primitiveIndex){
    const uint word = g_NwbSkinnedMeshBoundsPrimitiveIndices.Load(primitiveIndex & ~NWB_MESHLET_PACKED_WORD_BYTE_MASK);
    return (word >> ((primitiveIndex & NWB_MESHLET_PACKED_WORD_BYTE_MASK) * NWB_MESHLET_PACKED_BYTE_BITS)) & NWB_MESHLET_PACKED_BYTE_MASK;
}

uint3 nwbSkinnedMeshBoundsLoadPrimitive(const NwbMeshletDesc meshlet, const uint localPrimitiveIndex){
    const uint primitiveBase = meshlet.primitiveOffset + localPrimitiveIndex * NWB_MESHLET_TRIANGLE_INDEX_COUNT;
    return uint3(
        nwbSkinnedMeshBoundsLoadPrimitiveIndex(primitiveBase + 0u),
        nwbSkinnedMeshBoundsLoadPrimitiveIndex(primitiveBase + 1u),
        nwbSkinnedMeshBoundsLoadPrimitiveIndex(primitiveBase + 2u)
    );
}

bool nwbSkinnedMeshBoundsLoadLocalPosition(
    const NwbMeshletDesc meshlet,
    const NwbMeshletRefDecodeInfo decodeInfo,
    const uint localPositionIndex,
    out float3 outPosition
){
    const NwbMeshletPositionStreamRef positionRef = nwbMeshletDecodePositionRef(
        g_NwbSkinnedMeshBoundsPositionRefDeltas,
        meshlet,
        decodeInfo,
        localPositionIndex,
        true
    );
    outPosition = nwbMeshLoadPosition(g_NwbSkinnedMeshBoundsPositions[positionRef.position]);
    return nwbFiniteFloat3(outPosition);
}

bool nwbSkinnedMeshBoundsLoadLocalVertexPosition(
    const NwbMeshletDesc meshlet,
    const NwbMeshletRefDecodeInfo decodeInfo,
    const uint localVertexIndex,
    out float3 outPosition
){
    const NwbMeshletLocalVertexRef localVertexRef = g_NwbSkinnedMeshBoundsLocalVertexRefs[meshlet.localVertexOffset + localVertexIndex];
    return nwbSkinnedMeshBoundsLoadLocalPosition(meshlet, decodeInfo, nwbMeshletLocalDeformedPosition(localVertexRef), outPosition);
}

bool nwbSkinnedMeshBoundsFaceNormal(
    const NwbMeshletDesc meshlet,
    const NwbMeshletRefDecodeInfo decodeInfo,
    const uint localPrimitiveIndex,
    out float3 outFaceNormal
){
    const uint3 localVertices = nwbSkinnedMeshBoundsLoadPrimitive(meshlet, localPrimitiveIndex);
    float3 p0 = float3(0.0);
    float3 p1 = float3(0.0);
    float3 p2 = float3(0.0);
    if(
        !nwbSkinnedMeshBoundsLoadLocalVertexPosition(meshlet, decodeInfo, localVertices.x, p0)
        || !nwbSkinnedMeshBoundsLoadLocalVertexPosition(meshlet, decodeInfo, localVertices.y, p1)
        || !nwbSkinnedMeshBoundsLoadLocalVertexPosition(meshlet, decodeInfo, localVertices.z, p2)
    ){
        outFaceNormal = float3(0.0);
        return false;
    }

    outFaceNormal = cross(p1 - p0, p2 - p0);
    return nwbValidFrameDirection(outFaceNormal);
}

void nwbSkinnedMeshBoundsStore(const uint meshletIndex, const float4 sphere, const uint conePacked){
    const uint byteOffset = nwbMeshletBoundsByteOffset(meshletIndex);
    g_NwbSkinnedMeshBoundsDynamicBounds.Store4(byteOffset + NWB_MESHLET_BOUNDS_SPHERE_BYTE_OFFSET, asuint(sphere));
    g_NwbSkinnedMeshBoundsDynamicBounds.Store(byteOffset + NWB_MESHLET_BOUNDS_CONE_BYTE_OFFSET, conePacked);
    g_NwbSkinnedMeshBoundsDynamicBounds.Store(byteOffset + NWB_MESHLET_BOUNDS_PADDING_BYTE_OFFSET, 0u);
}

void nwbSkinnedMeshBoundsReduceMinMax(const uint groupThreadIndex){
#if NWB_SKINNED_MESH_BOUNDS_USE_WAVE_REDUCE
    // Two-stage fold. Intra-wave: each lane reads ONLY its own groupshared slot (the value the
    // caller just wrote), so WaveActiveMin/Max combine register values across the wave with no
    // barrier -- the same property the surfel SH reduce relies on. Lane 0 of each wave then holds
    // the per-wave min/max. Cross-wave: lane 0 of each wave stores its result, one barrier, then a
    // log2(waveCount)-stride tree folds the per-wave results into slot 0. 128-thread group ->
    // waveCount 2 on AMD BC-250 (subgroup 64) / 4 on NVIDIA (subgroup 32); 8 baseline barriers -> 2.
    const uint waveLaneCount = WaveGetLaneCount();
    const uint laneIndex = WaveGetLaneIndex();
    const uint waveIndex = groupThreadIndex / waveLaneCount;
    const float3 waveMin = WaveReadLaneFirst(WaveActiveMin(g_NwbSkinnedMeshBoundsMin[groupThreadIndex]));
    const float3 waveMax = WaveReadLaneFirst(WaveActiveMax(g_NwbSkinnedMeshBoundsMax[groupThreadIndex]));
    if(laneIndex == 0u){
        g_NwbSkinnedMeshBoundsMin[waveIndex] = waveMin;
        g_NwbSkinnedMeshBoundsMax[waveIndex] = waveMax;
    }
    GroupMemoryBarrierWithGroupSync();
    for(uint stride = (NWB_SKINNED_MESH_BOUNDS_GROUP_SIZE_X / waveLaneCount) >> 1u; stride > 0u; stride >>= 1u){
        if(groupThreadIndex < stride){
            g_NwbSkinnedMeshBoundsMin[groupThreadIndex] = min(
                g_NwbSkinnedMeshBoundsMin[groupThreadIndex],
                g_NwbSkinnedMeshBoundsMin[groupThreadIndex + stride]
            );
            g_NwbSkinnedMeshBoundsMax[groupThreadIndex] = max(
                g_NwbSkinnedMeshBoundsMax[groupThreadIndex],
                g_NwbSkinnedMeshBoundsMax[groupThreadIndex + stride]
            );
        }
        GroupMemoryBarrierWithGroupSync();
    }
#else
    GroupMemoryBarrierWithGroupSync();
    for(uint stride = NWB_SKINNED_MESH_BOUNDS_GROUP_SIZE_X >> 1u; stride > 0u; stride >>= 1u){
        if(groupThreadIndex < stride){
            g_NwbSkinnedMeshBoundsMin[groupThreadIndex] = min(
                g_NwbSkinnedMeshBoundsMin[groupThreadIndex],
                g_NwbSkinnedMeshBoundsMin[groupThreadIndex + stride]
            );
            g_NwbSkinnedMeshBoundsMax[groupThreadIndex] = max(
                g_NwbSkinnedMeshBoundsMax[groupThreadIndex],
                g_NwbSkinnedMeshBoundsMax[groupThreadIndex + stride]
            );
        }
        GroupMemoryBarrierWithGroupSync();
    }
#endif
}

void nwbSkinnedMeshBoundsReduceValidPositionCount(const uint groupThreadIndex){
#if NWB_SKINNED_MESH_BOUNDS_USE_WAVE_REDUCE
    const uint waveLaneCount = WaveGetLaneCount();
    const uint laneIndex = WaveGetLaneIndex();
    const uint waveIndex = groupThreadIndex / waveLaneCount;
    const uint waveSum = WaveReadLaneFirst(WaveActiveSum(g_NwbSkinnedMeshBoundsValidPositionCount[groupThreadIndex]));
    if(laneIndex == 0u)
        g_NwbSkinnedMeshBoundsValidPositionCount[waveIndex] = waveSum;
    GroupMemoryBarrierWithGroupSync();
    for(uint stride = (NWB_SKINNED_MESH_BOUNDS_GROUP_SIZE_X / waveLaneCount) >> 1u; stride > 0u; stride >>= 1u){
        if(groupThreadIndex < stride)
            g_NwbSkinnedMeshBoundsValidPositionCount[groupThreadIndex] += g_NwbSkinnedMeshBoundsValidPositionCount[groupThreadIndex + stride];
        GroupMemoryBarrierWithGroupSync();
    }
#else
    GroupMemoryBarrierWithGroupSync();
    for(uint stride = NWB_SKINNED_MESH_BOUNDS_GROUP_SIZE_X >> 1u; stride > 0u; stride >>= 1u){
        if(groupThreadIndex < stride)
            g_NwbSkinnedMeshBoundsValidPositionCount[groupThreadIndex] += g_NwbSkinnedMeshBoundsValidPositionCount[groupThreadIndex + stride];
        GroupMemoryBarrierWithGroupSync();
    }
#endif
}

void nwbSkinnedMeshBoundsReduceRadius(const uint groupThreadIndex){
#if NWB_SKINNED_MESH_BOUNDS_USE_WAVE_REDUCE
    const uint waveLaneCount = WaveGetLaneCount();
    const uint laneIndex = WaveGetLaneIndex();
    const uint waveIndex = groupThreadIndex / waveLaneCount;
    const float waveMax = WaveReadLaneFirst(WaveActiveMax(g_NwbSkinnedMeshBoundsRadiusSq[groupThreadIndex]));
    if(laneIndex == 0u)
        g_NwbSkinnedMeshBoundsRadiusSq[waveIndex] = waveMax;
    GroupMemoryBarrierWithGroupSync();
    for(uint stride = (NWB_SKINNED_MESH_BOUNDS_GROUP_SIZE_X / waveLaneCount) >> 1u; stride > 0u; stride >>= 1u){
        if(groupThreadIndex < stride)
            g_NwbSkinnedMeshBoundsRadiusSq[groupThreadIndex] = max(
                g_NwbSkinnedMeshBoundsRadiusSq[groupThreadIndex],
                g_NwbSkinnedMeshBoundsRadiusSq[groupThreadIndex + stride]
            );
        GroupMemoryBarrierWithGroupSync();
    }
#else
    GroupMemoryBarrierWithGroupSync();
    for(uint stride = NWB_SKINNED_MESH_BOUNDS_GROUP_SIZE_X >> 1u; stride > 0u; stride >>= 1u){
        if(groupThreadIndex < stride)
            g_NwbSkinnedMeshBoundsRadiusSq[groupThreadIndex] = max(
                g_NwbSkinnedMeshBoundsRadiusSq[groupThreadIndex],
                g_NwbSkinnedMeshBoundsRadiusSq[groupThreadIndex + stride]
            );
        GroupMemoryBarrierWithGroupSync();
    }
#endif
}

void nwbSkinnedMeshBoundsReduceAreaNormal(const uint groupThreadIndex){
#if NWB_SKINNED_MESH_BOUNDS_USE_WAVE_REDUCE
    const uint waveLaneCount = WaveGetLaneCount();
    const uint laneIndex = WaveGetLaneIndex();
    const uint waveIndex = groupThreadIndex / waveLaneCount;
    const float3 waveSum = WaveReadLaneFirst(WaveActiveSum(g_NwbSkinnedMeshBoundsAreaNormal[groupThreadIndex]));
    if(laneIndex == 0u)
        g_NwbSkinnedMeshBoundsAreaNormal[waveIndex] = waveSum;
    GroupMemoryBarrierWithGroupSync();
    for(uint stride = (NWB_SKINNED_MESH_BOUNDS_GROUP_SIZE_X / waveLaneCount) >> 1u; stride > 0u; stride >>= 1u){
        if(groupThreadIndex < stride)
            g_NwbSkinnedMeshBoundsAreaNormal[groupThreadIndex] += g_NwbSkinnedMeshBoundsAreaNormal[groupThreadIndex + stride];
        GroupMemoryBarrierWithGroupSync();
    }
#else
    GroupMemoryBarrierWithGroupSync();
    for(uint stride = NWB_SKINNED_MESH_BOUNDS_GROUP_SIZE_X >> 1u; stride > 0u; stride >>= 1u){
        if(groupThreadIndex < stride)
            g_NwbSkinnedMeshBoundsAreaNormal[groupThreadIndex] += g_NwbSkinnedMeshBoundsAreaNormal[groupThreadIndex + stride];
        GroupMemoryBarrierWithGroupSync();
    }
#endif
}

void nwbSkinnedMeshBoundsReduceValidFaceNormalCount(const uint groupThreadIndex){
#if NWB_SKINNED_MESH_BOUNDS_USE_WAVE_REDUCE
    const uint waveLaneCount = WaveGetLaneCount();
    const uint laneIndex = WaveGetLaneIndex();
    const uint waveIndex = groupThreadIndex / waveLaneCount;
    const uint waveSum = WaveReadLaneFirst(WaveActiveSum(g_NwbSkinnedMeshBoundsValidFaceNormalCount[groupThreadIndex]));
    if(laneIndex == 0u)
        g_NwbSkinnedMeshBoundsValidFaceNormalCount[waveIndex] = waveSum;
    GroupMemoryBarrierWithGroupSync();
    for(uint stride = (NWB_SKINNED_MESH_BOUNDS_GROUP_SIZE_X / waveLaneCount) >> 1u; stride > 0u; stride >>= 1u){
        if(groupThreadIndex < stride)
            g_NwbSkinnedMeshBoundsValidFaceNormalCount[groupThreadIndex] += g_NwbSkinnedMeshBoundsValidFaceNormalCount[groupThreadIndex + stride];
        GroupMemoryBarrierWithGroupSync();
    }
#else
    GroupMemoryBarrierWithGroupSync();
    for(uint stride = NWB_SKINNED_MESH_BOUNDS_GROUP_SIZE_X >> 1u; stride > 0u; stride >>= 1u){
        if(groupThreadIndex < stride)
            g_NwbSkinnedMeshBoundsValidFaceNormalCount[groupThreadIndex] += g_NwbSkinnedMeshBoundsValidFaceNormalCount[groupThreadIndex + stride];
        GroupMemoryBarrierWithGroupSync();
    }
#endif
}

void nwbSkinnedMeshBoundsReduceConeCutoff(const uint groupThreadIndex){
#if NWB_SKINNED_MESH_BOUNDS_USE_WAVE_REDUCE
    const uint waveLaneCount = WaveGetLaneCount();
    const uint laneIndex = WaveGetLaneIndex();
    const uint waveIndex = groupThreadIndex / waveLaneCount;
    const float waveMin = WaveReadLaneFirst(WaveActiveMin(g_NwbSkinnedMeshBoundsConeCutoff[groupThreadIndex]));
    if(laneIndex == 0u)
        g_NwbSkinnedMeshBoundsConeCutoff[waveIndex] = waveMin;
    GroupMemoryBarrierWithGroupSync();
    for(uint stride = (NWB_SKINNED_MESH_BOUNDS_GROUP_SIZE_X / waveLaneCount) >> 1u; stride > 0u; stride >>= 1u){
        if(groupThreadIndex < stride)
            g_NwbSkinnedMeshBoundsConeCutoff[groupThreadIndex] = min(
                g_NwbSkinnedMeshBoundsConeCutoff[groupThreadIndex],
                g_NwbSkinnedMeshBoundsConeCutoff[groupThreadIndex + stride]
            );
        GroupMemoryBarrierWithGroupSync();
    }
#else
    GroupMemoryBarrierWithGroupSync();
    for(uint stride = NWB_SKINNED_MESH_BOUNDS_GROUP_SIZE_X >> 1u; stride > 0u; stride >>= 1u){
        if(groupThreadIndex < stride)
            g_NwbSkinnedMeshBoundsConeCutoff[groupThreadIndex] = min(
                g_NwbSkinnedMeshBoundsConeCutoff[groupThreadIndex],
                g_NwbSkinnedMeshBoundsConeCutoff[groupThreadIndex + stride]
            );
        GroupMemoryBarrierWithGroupSync();
    }
#endif
}


[numthreads(NWB_SKINNED_MESH_BOUNDS_GROUP_SIZE_X, 1, 1)]
void main(uint3 groupThreadID : SV_GroupThreadID, uint3 groupID : SV_GroupID){
    const uint groupThreadIndex = groupThreadID.x;
    const uint meshletIndex = groupID.x;
    if(meshletIndex >= nwbSkinnedMeshBoundsMeshletCount())
        return;

    const NwbMeshletDesc meshlet = g_NwbSkinnedMeshBoundsMeshlets[meshletIndex];
    const NwbMeshletRefDecodeInfo decodeInfo = nwbMeshletBuildRefDecodeInfo(meshlet);
    const uint positionCount = nwbMeshletPositionCount(meshlet);
    const uint primitiveCount = nwbMeshletPrimitiveCount(meshlet);
    if(positionCount == 0u || primitiveCount == 0u){
        if(groupThreadIndex == 0u)
            nwbSkinnedMeshBoundsStore(meshletIndex, float4(0.0, 0.0, 0.0, 0.0), 0u);
        return;
    }

    float3 localMin = float3(NWB_SKINNED_MESH_BOUNDS_FLOAT_MAX);
    float3 localMax = float3(-NWB_SKINNED_MESH_BOUNDS_FLOAT_MAX);
    uint localValidPositionCount = 0u;
    for(uint localPositionIndex = groupThreadIndex; localPositionIndex < positionCount; localPositionIndex += NWB_SKINNED_MESH_BOUNDS_GROUP_SIZE_X){
        float3 position = float3(0.0);
        if(!nwbSkinnedMeshBoundsLoadLocalPosition(meshlet, decodeInfo, localPositionIndex, position))
            continue;

        localMin = min(localMin, position);
        localMax = max(localMax, position);
        ++localValidPositionCount;
    }
    g_NwbSkinnedMeshBoundsMin[groupThreadIndex] = localMin;
    g_NwbSkinnedMeshBoundsMax[groupThreadIndex] = localMax;
    g_NwbSkinnedMeshBoundsValidPositionCount[groupThreadIndex] = localValidPositionCount;
    nwbSkinnedMeshBoundsReduceMinMax(groupThreadIndex);
    nwbSkinnedMeshBoundsReduceValidPositionCount(groupThreadIndex);

    if(g_NwbSkinnedMeshBoundsValidPositionCount[0u] == 0u){
        if(groupThreadIndex == 0u)
            nwbSkinnedMeshBoundsStore(meshletIndex, float4(0.0, 0.0, 0.0, 0.0), 0u);
        return;
    }

    if(groupThreadIndex == 0u)
        g_NwbSkinnedMeshBoundsCenter = (g_NwbSkinnedMeshBoundsMin[0u] + g_NwbSkinnedMeshBoundsMax[0u]) * 0.5;
    GroupMemoryBarrierWithGroupSync();

    float localRadiusSq = 0.0;
    for(uint localPositionIndex = groupThreadIndex; localPositionIndex < positionCount; localPositionIndex += NWB_SKINNED_MESH_BOUNDS_GROUP_SIZE_X){
        float3 position = float3(0.0);
        if(!nwbSkinnedMeshBoundsLoadLocalPosition(meshlet, decodeInfo, localPositionIndex, position))
            continue;

        const float3 delta = position - g_NwbSkinnedMeshBoundsCenter;
        localRadiusSq = max(localRadiusSq, dot(delta, delta));
    }
    g_NwbSkinnedMeshBoundsRadiusSq[groupThreadIndex] = localRadiusSq;
    nwbSkinnedMeshBoundsReduceRadius(groupThreadIndex);

    float3 localAreaNormal = float3(0.0);
    uint localValidFaceNormalCount = 0u;
    for(uint localPrimitiveIndex = groupThreadIndex; localPrimitiveIndex < primitiveCount; localPrimitiveIndex += NWB_SKINNED_MESH_BOUNDS_GROUP_SIZE_X){
        float3 faceNormal = float3(0.0);
        if(nwbSkinnedMeshBoundsFaceNormal(meshlet, decodeInfo, localPrimitiveIndex, faceNormal)){
            localAreaNormal += faceNormal;
            ++localValidFaceNormalCount;
        }
    }
    g_NwbSkinnedMeshBoundsAreaNormal[groupThreadIndex] = localAreaNormal;
    g_NwbSkinnedMeshBoundsValidFaceNormalCount[groupThreadIndex] = localValidFaceNormalCount;
    nwbSkinnedMeshBoundsReduceAreaNormal(groupThreadIndex);
    nwbSkinnedMeshBoundsReduceValidFaceNormalCount(groupThreadIndex);

    if(groupThreadIndex == 0u)
        g_NwbSkinnedMeshBoundsConeAxis = nwbSafeNormalize(g_NwbSkinnedMeshBoundsAreaNormal[0u], float3(0.0));
    GroupMemoryBarrierWithGroupSync();

    float localConeCutoff = 1.0;
    for(uint localPrimitiveIndex = groupThreadIndex; localPrimitiveIndex < primitiveCount; localPrimitiveIndex += NWB_SKINNED_MESH_BOUNDS_GROUP_SIZE_X){
        float3 areaFaceNormal = float3(0.0);
        if(!nwbSkinnedMeshBoundsFaceNormal(meshlet, decodeInfo, localPrimitiveIndex, areaFaceNormal))
            continue;

        const float3 faceNormal = nwbSafeNormalize(areaFaceNormal, float3(0.0));
        if(nwbValidFrameDirection(faceNormal))
            localConeCutoff = min(localConeCutoff, dot(g_NwbSkinnedMeshBoundsConeAxis, faceNormal));
    }
    g_NwbSkinnedMeshBoundsConeCutoff[groupThreadIndex] = localConeCutoff;
    nwbSkinnedMeshBoundsReduceConeCutoff(groupThreadIndex);

    if(groupThreadIndex != 0u)
        return;

    const float radius = sqrt(g_NwbSkinnedMeshBoundsRadiusSq[0u]) * NWB_SKINNED_MESH_BOUNDS_RADIUS_INFLATION;
    const float coneCutoff = g_NwbSkinnedMeshBoundsConeCutoff[0u];
    const bool coneValid = g_NwbSkinnedMeshBoundsValidFaceNormalCount[0u] == primitiveCount
        && nwbValidFrameDirection(g_NwbSkinnedMeshBoundsConeAxis)
        && coneCutoff > 0.0
    ;
    const uint conePacked = coneValid
        ? nwbMeshPackCone(g_NwbSkinnedMeshBoundsConeAxis, coneCutoff)
        : 0u
    ;
    nwbSkinnedMeshBoundsStore(meshletIndex, float4(g_NwbSkinnedMeshBoundsCenter, radius), conePacked);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

