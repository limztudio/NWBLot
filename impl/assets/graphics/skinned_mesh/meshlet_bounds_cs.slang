// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../mesh/meshlet_payload.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_SKINNED_MESH_BOUNDS_GROUP_SIZE_X 128

static const float NWB_SKINNED_MESH_BOUNDS_FLOAT_MAX = 3.402823e+38;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[vk::binding(0, 0)]] StructuredBuffer<NwbMeshPosition, Std430DataLayout> g_NwbSkinnedMeshBoundsPositions;
[[vk::binding(1, 0)]] StructuredBuffer<NwbMeshletDesc, Std430DataLayout> g_NwbSkinnedMeshBoundsMeshlets;
[[vk::binding(2, 0)]] ByteAddressBuffer g_NwbSkinnedMeshBoundsPositionRefDeltas;
[[vk::binding(3, 0)]] StructuredBuffer<NwbMeshletLocalVertexRef, Std430DataLayout> g_NwbSkinnedMeshBoundsLocalVertexRefs;
[[vk::binding(4, 0)]] ByteAddressBuffer g_NwbSkinnedMeshBoundsPrimitiveIndices;
[[vk::binding(5, 0)]] RWByteAddressBuffer g_NwbSkinnedMeshBoundsDynamicBounds;

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
    return g_NwbSkinnedMeshBoundsPushConstants.payload0.x;
}

uint nwbSkinnedMeshBoundsLoadPrimitiveIndex(const uint primitiveIndex){
    const uint word = g_NwbSkinnedMeshBoundsPrimitiveIndices.Load(primitiveIndex & ~3u);
    return (word >> ((primitiveIndex & 3u) << 3u)) & 0xffu;
}

uint3 nwbSkinnedMeshBoundsLoadPrimitive(const NwbMeshletDesc meshlet, const uint localPrimitiveIndex){
    const uint primitiveBase = meshlet.primitiveOffset + localPrimitiveIndex * 3u;
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
    g_NwbSkinnedMeshBoundsDynamicBounds.Store4(byteOffset, asuint(sphere));
    g_NwbSkinnedMeshBoundsDynamicBounds.Store(byteOffset + 16u, conePacked);
    g_NwbSkinnedMeshBoundsDynamicBounds.Store(byteOffset + 20u, 0u);
}

void nwbSkinnedMeshBoundsReduceMinMax(const uint groupThreadIndex){
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
}

void nwbSkinnedMeshBoundsReduceValidPositionCount(const uint groupThreadIndex){
    GroupMemoryBarrierWithGroupSync();
    for(uint stride = NWB_SKINNED_MESH_BOUNDS_GROUP_SIZE_X >> 1u; stride > 0u; stride >>= 1u){
        if(groupThreadIndex < stride)
            g_NwbSkinnedMeshBoundsValidPositionCount[groupThreadIndex] += g_NwbSkinnedMeshBoundsValidPositionCount[groupThreadIndex + stride];
        GroupMemoryBarrierWithGroupSync();
    }
}

void nwbSkinnedMeshBoundsReduceRadius(const uint groupThreadIndex){
    GroupMemoryBarrierWithGroupSync();
    for(uint stride = NWB_SKINNED_MESH_BOUNDS_GROUP_SIZE_X >> 1u; stride > 0u; stride >>= 1u){
        if(groupThreadIndex < stride)
            g_NwbSkinnedMeshBoundsRadiusSq[groupThreadIndex] = max(
                g_NwbSkinnedMeshBoundsRadiusSq[groupThreadIndex],
                g_NwbSkinnedMeshBoundsRadiusSq[groupThreadIndex + stride]
            );
        GroupMemoryBarrierWithGroupSync();
    }
}

void nwbSkinnedMeshBoundsReduceAreaNormal(const uint groupThreadIndex){
    GroupMemoryBarrierWithGroupSync();
    for(uint stride = NWB_SKINNED_MESH_BOUNDS_GROUP_SIZE_X >> 1u; stride > 0u; stride >>= 1u){
        if(groupThreadIndex < stride)
            g_NwbSkinnedMeshBoundsAreaNormal[groupThreadIndex] += g_NwbSkinnedMeshBoundsAreaNormal[groupThreadIndex + stride];
        GroupMemoryBarrierWithGroupSync();
    }
}

void nwbSkinnedMeshBoundsReduceValidFaceNormalCount(const uint groupThreadIndex){
    GroupMemoryBarrierWithGroupSync();
    for(uint stride = NWB_SKINNED_MESH_BOUNDS_GROUP_SIZE_X >> 1u; stride > 0u; stride >>= 1u){
        if(groupThreadIndex < stride)
            g_NwbSkinnedMeshBoundsValidFaceNormalCount[groupThreadIndex] += g_NwbSkinnedMeshBoundsValidFaceNormalCount[groupThreadIndex + stride];
        GroupMemoryBarrierWithGroupSync();
    }
}

void nwbSkinnedMeshBoundsReduceConeCutoff(const uint groupThreadIndex){
    GroupMemoryBarrierWithGroupSync();
    for(uint stride = NWB_SKINNED_MESH_BOUNDS_GROUP_SIZE_X >> 1u; stride > 0u; stride >>= 1u){
        if(groupThreadIndex < stride)
            g_NwbSkinnedMeshBoundsConeCutoff[groupThreadIndex] = min(
                g_NwbSkinnedMeshBoundsConeCutoff[groupThreadIndex],
                g_NwbSkinnedMeshBoundsConeCutoff[groupThreadIndex + stride]
            );
        GroupMemoryBarrierWithGroupSync();
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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

    const float radius = sqrt(g_NwbSkinnedMeshBoundsRadiusSq[0u]);
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

