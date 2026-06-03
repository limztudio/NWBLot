// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cap_proxy_common.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


float3 nwbCsgCapProxyBoxCorner(const float3 halfExtents, const uint corner){
    return float3(
        (corner & 1u) != 0u ? halfExtents.x : -halfExtents.x,
        (corner & 2u) != 0u ? halfExtents.y : -halfExtents.y,
        (corner & 4u) != 0u ? halfExtents.z : -halfExtents.z
    );
}

void nwbCsgCapProxyBoxFace(const uint face, out float3 normal, out uint4 corners){
    switch(face){
    case 0u: normal = float3( 1.0,  0.0,  0.0); corners = uint4(1u, 5u, 7u, 3u); return;
    case 1u: normal = float3(-1.0,  0.0,  0.0); corners = uint4(0u, 2u, 6u, 4u); return;
    case 2u: normal = float3( 0.0,  1.0,  0.0); corners = uint4(2u, 3u, 7u, 6u); return;
    case 3u: normal = float3( 0.0, -1.0,  0.0); corners = uint4(0u, 4u, 5u, 1u); return;
    case 4u: normal = float3( 0.0,  0.0,  1.0); corners = uint4(4u, 6u, 7u, 5u); return;
    default: normal = float3( 0.0,  0.0, -1.0); corners = uint4(0u, 1u, 3u, 2u); return;
    }
}

NwbMeshRasterVertex nwbCsgCapProxyBoxVertex(
    const NwbCsgCapProxyDrawItem item,
    const NwbCsgCutter cutter,
    const uint localVertex
){
    const uint face = localVertex / 4u;
    const uint cornerInFace = localVertex - face * 4u;
    float3 shapeNormal;
    uint4 corners;
    nwbCsgCapProxyBoxFace(face, shapeNormal, corners);
    const uint corner = corners[cornerInFace];
    const float3 shapePosition = nwbCsgCapProxyBoxCorner(max(cutter.parameter0.xyz, float3(0.0001)), corner);
    return nwbCsgCapProxyBuildVertex(
        nwbCsgTransformShapeToWorld(shapePosition, cutter),
        shapeNormal,
        cutter,
        item.receiverCutterShapePass.x,
        item.receiverCutterShapePass.y
    );
}

void nwbCsgCapProxyBoxPrimitive(const uint localPrimitive, out uint3 indices){
    const uint face = localPrimitive / 2u;
    const uint baseVertex = face * 4u;
    indices = (localPrimitive & 1u) == 0u
        ? uint3(baseVertex + 0u, baseVertex + 1u, baseVertex + 2u)
        : uint3(baseVertex + 0u, baseVertex + 2u, baseVertex + 3u)
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if NWB_MESH_SHADER_EMULATION_COMPUTE


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_CSG_CAP_PROXY_GROUP_SIZE, 1, 1)]
void main(uint3 groupThreadID : SV_GroupThreadID, uint3 groupID : SV_GroupID){
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#else


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[shader("mesh")]
[numthreads(NWB_CSG_CAP_PROXY_GROUP_SIZE, 1, 1)]
[outputtopology("triangle")]
void main(
    uint3 groupThreadID : SV_GroupThreadID,
    uint3 groupID : SV_GroupID,
    OutputVertices<NwbMeshRasterVertex, NWB_CSG_CAP_PROXY_MAX_VERTICES> vertices,
    OutputIndices<uint3, NWB_CSG_CAP_PROXY_MAX_TRIANGLES> triangles
){
    const uint proxyIndex = groupID.x;
    if(proxyIndex >= g_NwbCsgCapProxyPushConstants.dispatch.x){
        SetMeshOutputCounts(0u, 0u);
        return;
    }

    const uint groupThreadIndex = groupThreadID.x;
    const NwbCsgCapProxyDrawItem item = g_NwbCsgCapProxies[proxyIndex];
    if(item.receiverCutterShapePass.z != NWB_CSG_SHAPE_BOX){
        SetMeshOutputCounts(0u, 0u);
        return;
    }

    const uint vertexCount = 24u;
    const uint primitiveCount = 12u;
    const NwbCsgCutter cutter = g_NwbCsgCutters[item.receiverCutterShapePass.y];

    SetMeshOutputCounts(vertexCount, primitiveCount);

    for(uint localVertex = groupThreadIndex; localVertex < vertexCount; localVertex += NWB_CSG_CAP_PROXY_GROUP_SIZE)
        vertices[localVertex] = nwbCsgCapProxyBoxVertex(item, cutter, localVertex);

    for(uint localPrimitive = groupThreadIndex; localPrimitive < primitiveCount; localPrimitive += NWB_CSG_CAP_PROXY_GROUP_SIZE)
        nwbCsgCapProxyBoxPrimitive(localPrimitive, triangles[localPrimitive]);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
