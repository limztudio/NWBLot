// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cap_proxy_common.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NwbMeshRasterVertex nwbCsgCapProxySphereVertex(
    const NwbCsgCapProxyGpuData item,
    const NwbCsgCutter cutter,
    const uint localVertex,
    const uint segments,
    const uint rows
){
    const uint row = localVertex / (segments + 1u);
    const uint column = localVertex - row * (segments + 1u);
    const float theta = NWB_CSG_CAP_PROXY_PI * float(row) / float(rows);
    const float phi = NWB_CSG_CAP_PROXY_TWO_PI * float(column) / float(segments);
    float sinTheta;
    float cosTheta;
    float sinPhi;
    float cosPhi;
    sincos(theta, sinTheta, cosTheta);
    sincos(phi, sinPhi, cosPhi);
    const float3 shapeNormal = float3(sinTheta * cosPhi, cosTheta, sinTheta * sinPhi);
    const float radiusSlack = nwbCsgCapProxySphereRadiusSlack(cutter.parameter0.x, segments, rows);
    const float radius = max(cutter.parameter0.x, 0.0001) + radiusSlack;
    return nwbCsgCapProxyBuildVertex(
        nwbCsgTransformShapeToWorld(shapeNormal * radius, cutter),
        shapeNormal,
        cutter,
        item.receiverCutterShapePass.x,
        item.receiverCutterShapePass.y,
        item.color
    );
}


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
    if(!nwbCsgCapProxyItemMatchesPassAndShape(item, NWB_CSG_SHAPE_SPHERE)){
        SetMeshOutputCounts(0u, 0u);
        return;
    }

    const NwbCsgCutter cutter = g_NwbCsgCutters[item.receiverCutterShapePass.y];
    const uint segments = nwbCsgCapProxySegmentCount(item, cutter, cutter.parameter0.x);
    const uint rows = max(3u, segments / 2u - 1u);
    const uint vertexCount = (rows + 1u) * (segments + 1u);
    const uint primitiveCount = rows * segments * 2u;

    SetMeshOutputCounts(vertexCount, primitiveCount);

    for(uint localVertex = groupThreadIndex; localVertex < vertexCount; localVertex += NWB_CSG_CAP_PROXY_GROUP_SIZE)
        vertices[localVertex] = nwbCsgCapProxySphereVertex(item, cutter, localVertex, segments, rows);

    for(uint localPrimitive = groupThreadIndex; localPrimitive < primitiveCount; localPrimitive += NWB_CSG_CAP_PROXY_GROUP_SIZE)
        nwbCsgCapProxyGridPrimitive(localPrimitive, segments, triangles[localPrimitive]);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
