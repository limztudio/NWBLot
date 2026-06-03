// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cap_proxy_common.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void nwbCsgCapProxyCapsuleRow(const uint row, const float radius, const float halfHeight, out float y, out float ringRadius){
    switch(row){
    case 0u: y = halfHeight + radius; ringRadius = 0.0; return;
    case 1u: y = halfHeight + radius * 0.70710678; ringRadius = radius * 0.70710678; return;
    case 2u: y = halfHeight; ringRadius = radius; return;
    case 3u: y = -halfHeight; ringRadius = radius; return;
    case 4u: y = -halfHeight - radius * 0.70710678; ringRadius = radius * 0.70710678; return;
    default: y = -halfHeight - radius; ringRadius = 0.0; return;
    }
}

NwbMeshRasterVertex nwbCsgCapProxyCapsuleVertex(
    const NwbCsgCapProxyGpuData item,
    const NwbCsgCutter cutter,
    const uint localVertex,
    const uint segments
){
    const uint row = localVertex / (segments + 1u);
    const uint column = localVertex - row * (segments + 1u);
    const float phi = 6.28318531 * float(column) / float(segments);
    const float radius = max(cutter.parameter0.x, 0.0001);
    const float halfHeight = max(cutter.parameter0.y, 0.0);
    float sinPhi;
    float cosPhi;
    sincos(phi, sinPhi, cosPhi);
    float y;
    float ringRadius;
    nwbCsgCapProxyCapsuleRow(row, radius, halfHeight, y, ringRadius);
    const float3 shapePosition = float3(ringRadius * cosPhi, y, ringRadius * sinPhi);
    const float3 axisPoint = float3(0.0, clamp(y, -halfHeight, halfHeight), 0.0);
    const float3 shapeNormal = nwbCsgCapProxyNormalizeOr(shapePosition - axisPoint, float3(0.0, 1.0, 0.0));
    return nwbCsgCapProxyBuildVertex(
        nwbCsgTransformShapeToWorld(shapePosition, cutter),
        shapeNormal,
        cutter,
        item.receiverCutterShapePass.x,
        item.receiverCutterShapePass.y
    );
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
    const NwbCsgCapProxyGpuData item = g_NwbCsgCapProxies[proxyIndex];
    if(!nwbCsgCapProxyItemMatchesPassAndShape(item, NWB_CSG_SHAPE_CAPSULE)){
        SetMeshOutputCounts(0u, 0u);
        return;
    }

    const NwbCsgCutter cutter = g_NwbCsgCutters[item.receiverCutterShapePass.y];
    const uint segments = nwbCsgCapProxySegmentCount(item, cutter, cutter.parameter0.x);
    const uint rows = NWB_CSG_CAP_PROXY_MAX_ROWS;
    const uint vertexCount = (rows + 1u) * (segments + 1u);
    const uint primitiveCount = rows * segments * 2u;

    SetMeshOutputCounts(vertexCount, primitiveCount);

    for(uint localVertex = groupThreadIndex; localVertex < vertexCount; localVertex += NWB_CSG_CAP_PROXY_GROUP_SIZE)
        vertices[localVertex] = nwbCsgCapProxyCapsuleVertex(item, cutter, localVertex, segments);

    for(uint localPrimitive = groupThreadIndex; localPrimitive < primitiveCount; localPrimitive += NWB_CSG_CAP_PROXY_GROUP_SIZE)
        nwbCsgCapProxyGridPrimitive(localPrimitive, segments, triangles[localPrimitive]);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
