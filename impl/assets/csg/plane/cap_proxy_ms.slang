// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../../graphics/csg/cap_proxy/common.slangi"
#include "eval.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


float3 nwbCsgCapProxyPlaneAxis(const float3 normal, const bool secondAxis){
    const float3 base = abs(normal.y) < 0.95 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
    const float3 axis0 = nwbCsgNormalizeOr(cross(base, normal), float3(1.0, 0.0, 0.0));
    if(!secondAxis)
        return axis0;
    return nwbCsgNormalizeOr(cross(normal, axis0), float3(0.0, 0.0, 1.0));
}

NwbMeshRasterVertex nwbCsgCapProxyPlaneVertex(
    const NwbCsgCapProxyGpuData item,
    const NwbCsgCutter cutter,
    const uint localVertex
){
    const float3 shapeNormal = nwbCsgPlaneNormal(cutter);
    const float3 centerShape = -shapeNormal * cutter.parameter0.w;
    const float3 receiverExtents = nwbCsgCapProxyBoundsExtents(item.receiverBounds);
    const float extent = max(length(receiverExtents) * 1.5, 0.5);
    const float3 axis0 = nwbCsgCapProxyPlaneAxis(shapeNormal, false);
    const float3 axis1 = nwbCsgCapProxyPlaneAxis(shapeNormal, true);
    const float sx = (localVertex == 1u || localVertex == 2u) ? 1.0 : -1.0;
    const float sy = localVertex >= 2u ? 1.0 : -1.0;
    const float3 shapePosition = centerShape + (axis0 * sx + axis1 * sy) * extent;
    return nwbCsgCapProxyBuildVertex(
        nwbCsgTransformShapeToWorld(shapePosition, cutter),
        shapeNormal,
        cutter,
        item.receiverCutterShapePass.x,
        item.receiverCutterShapePass.y,
        item.color
    );
}

void nwbCsgCapProxyPlanePrimitive(const uint localPrimitive, out uint3 indices){
    indices = localPrimitive == 0u ? uint3(0u, 2u, 1u) : uint3(0u, 3u, 2u);
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

    for(uint localPrimitive = groupThreadIndex; localPrimitive < NWB_CSG_CAP_PROXY_MAX_TRIANGLES; localPrimitive += NWB_CSG_CAP_PROXY_GROUP_SIZE){
        if(!itemMatches || localPrimitive >= 2u){
            nwbCsgCapProxyStoreCulledEmulatedPrimitive(proxyIndex, localPrimitive);
            continue;
        }

        uint3 indices;
        nwbCsgCapProxyPlanePrimitive(localPrimitive, indices);
        nwbCsgCapProxyStoreEmulatedPrimitive(
            proxyIndex,
            localPrimitive,
            nwbCsgCapProxyPlaneVertex(item, cutter, indices.x),
            nwbCsgCapProxyPlaneVertex(item, cutter, indices.y),
            nwbCsgCapProxyPlaneVertex(item, cutter, indices.z)
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

    const uint vertexCount = 4u;
    const uint primitiveCount = 2u;
    const NwbCsgCutter cutter = g_NwbCsgCutters[item.receiverCutterShapePass.y];

    SetMeshOutputCounts(vertexCount, primitiveCount);

    for(uint localVertex = groupThreadIndex; localVertex < vertexCount; localVertex += NWB_CSG_CAP_PROXY_GROUP_SIZE)
        vertices[localVertex] = nwbCsgCapProxyPlaneVertex(item, cutter, localVertex);

    for(uint localPrimitive = groupThreadIndex; localPrimitive < primitiveCount; localPrimitive += NWB_CSG_CAP_PROXY_GROUP_SIZE)
        nwbCsgCapProxyPlanePrimitive(localPrimitive, triangles[localPrimitive]);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
