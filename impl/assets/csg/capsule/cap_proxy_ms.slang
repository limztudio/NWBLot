// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../../graphics/csg/cap_proxy/common.slangi"
#include "cap_proxy_helpers.slangi"
#include "eval.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NwbMeshRasterVertex nwbCsgCapProxyCapsuleVertex(
    const NwbCsgCapProxyGpuData item,
    const NwbCsgCutter cutter,
    const uint localVertex,
    const uint segments
){
    const uint row = localVertex / (segments + 1u);
    const uint column = localVertex - row * (segments + 1u);
    const float phi = NWB_CSG_CAP_PROXY_TWO_PI * float(column) / float(segments);
    const uint rows = nwbCsgCapProxyCapsuleRowCount(segments);
    const float radiusSlack = nwbCsgCapProxyCapsuleRadiusSlack(cutter.parameter0.x, segments, rows);
    const float radius = max(cutter.parameter0.x, 0.0001);
    const float halfHeight = max(cutter.parameter0.y, 0.0);
    float sinPhi;
    float cosPhi;
    sincos(phi, sinPhi, cosPhi);
    float y;
    float ringRadius;
    nwbCsgCapProxyCapsuleRow(row, rows, radius, halfHeight, y, ringRadius);
    const float3 shapePosition = float3(ringRadius * cosPhi, y, ringRadius * sinPhi);
    const float3 shapeNormal = nwbCsgCapsuleNormal(shapePosition, cutter);
    const float3 enlargedShapePosition = nwbCsgCapProxyEnlargeShapePosition(shapePosition, cutter, radiusSlack);
    return nwbCsgCapProxyBuildVertex(
        nwbCsgTransformShapeToWorld(enlargedShapePosition, cutter),
        shapeNormal,
        cutter,
        item.receiverCutterShapePass.x,
        item.receiverCutterShapePass.y,
        item.color
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if NWB_MESH_SHADER_EMULATION_COMPUTE


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_CSG_CAP_PROXY_GROUP_SIZE, 1, 1)]
void main(uint3 groupThreadID : SV_GroupThreadID, uint3 groupID : SV_GroupID){
    const uint proxyIndex = groupID.x;
    if(proxyIndex >= g_NwbCsgCapProxyPushConstants.dispatch.x)
        return;

    const uint groupThreadIndex = groupThreadID.x;
    const NwbCsgCapProxyGpuData item = g_NwbCsgCapProxies[proxyIndex];
    const bool itemMatches = nwbCsgCapProxyItemMatchesPassAndShape(item, NWB_CSG_CAP_PROXY_SHADER_SHAPE_ID);
    const NwbCsgCutter cutter = g_NwbCsgCutters[item.receiverCutterShapePass.y];
    const uint segments = nwbCsgCapProxySegmentCount(item, cutter, cutter.parameter0.x);
    const uint rows = nwbCsgCapProxyCapsuleRowCount(segments);
    const uint primitiveCount = rows * segments * 2u;

    for(uint localPrimitive = groupThreadIndex; localPrimitive < NWB_CSG_CAP_PROXY_MAX_TRIANGLES; localPrimitive += NWB_CSG_CAP_PROXY_GROUP_SIZE){
        if(!itemMatches || localPrimitive >= primitiveCount){
            nwbCsgCapProxyStoreCulledEmulatedPrimitive(proxyIndex, localPrimitive);
            continue;
        }

        uint3 indices;
        nwbCsgCapProxyGridPrimitive(localPrimitive, segments, indices);
        nwbCsgCapProxyStoreEmulatedPrimitive(
            proxyIndex,
            localPrimitive,
            nwbCsgCapProxyCapsuleVertex(item, cutter, indices.x, segments),
            nwbCsgCapProxyCapsuleVertex(item, cutter, indices.y, segments),
            nwbCsgCapProxyCapsuleVertex(item, cutter, indices.z, segments)
        );
    }
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
    if(!nwbCsgCapProxyItemMatchesPassAndShape(item, NWB_CSG_CAP_PROXY_SHADER_SHAPE_ID)){
        SetMeshOutputCounts(0u, 0u);
        return;
    }

    const NwbCsgCutter cutter = g_NwbCsgCutters[item.receiverCutterShapePass.y];
    const uint segments = nwbCsgCapProxySegmentCount(item, cutter, cutter.parameter0.x);
    const uint rows = nwbCsgCapProxyCapsuleRowCount(segments);
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
