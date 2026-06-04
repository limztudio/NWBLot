// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../../graphics/csg/cap_proxy/common.slangi"
#include "cap_proxy_helpers.slangi"
#include "eval.slangi"
#include "cap_proxy_projection.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NwbCsgCapProxyShapeContext{
    uint segments;
    uint rows;
    float radius;
    float halfHeight;
    float radiusSlack;
};

NwbMeshRasterVertex nwbCsgCapProxyCapsuleVertex(
    const NwbCsgCapProxyShapeContext context,
    const NwbCsgCapProxyGpuData item,
    const NwbCsgCutter cutter,
    const uint localVertex
){
    const uint row = localVertex / (context.segments + 1u);
    const uint column = localVertex - row * (context.segments + 1u);
    const float phi = NWB_CSG_CAP_PROXY_TWO_PI * float(column) / float(context.segments);
    float sinPhi;
    float cosPhi;
    sincos(phi, sinPhi, cosPhi);
    float y;
    float ringRadius;
    nwbCsgCapProxyCapsuleRow(row, context.rows, context.radius, context.halfHeight, y, ringRadius);
    const float3 shapePosition = float3(ringRadius * cosPhi, y, ringRadius * sinPhi);
    const float3 shapeNormal = nwbCsgCapsuleNormal(shapePosition, cutter);
    const float3 enlargedShapePosition = nwbCsgCapProxyEnlargeCapsuleShapePosition(shapePosition, cutter, context.radiusSlack);
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


NwbCsgCapProxyShapeContext nwbCsgCapProxyBuildShapeContext(const NwbCsgCapProxyGpuData item, const NwbCsgCutter cutter){
    const uint segments = nwbCsgCapProxySegmentCount(item, cutter, cutter.parameter0.x);
    const uint rows = nwbCsgCapProxyCapsuleRowCount(segments);
    NwbCsgCapProxyShapeContext context;
    context.segments = segments;
    context.rows = rows;
    context.radius = max(cutter.parameter0.x, 0.0001);
    context.halfHeight = max(cutter.parameter0.y, 0.0);
    context.radiusSlack = nwbCsgCapProxyCapsuleRadiusSlack(cutter.parameter0.x, segments, rows);
    return context;
}

uint nwbCsgCapProxyShapeVertexCount(const NwbCsgCapProxyShapeContext context){
    return (context.rows + 1u) * (context.segments + 1u);
}

uint nwbCsgCapProxyShapePrimitiveCount(const NwbCsgCapProxyShapeContext context){
    return context.rows * context.segments * 2u;
}

NwbMeshRasterVertex nwbCsgCapProxyShapeVertex(
    const NwbCsgCapProxyShapeContext context,
    const NwbCsgCapProxyGpuData item,
    const NwbCsgCutter cutter,
    const uint localVertex
){
    return nwbCsgCapProxyCapsuleVertex(context, item, cutter, localVertex);
}

void nwbCsgCapProxyShapePrimitive(
    const NwbCsgCapProxyShapeContext context,
    const uint localPrimitive,
    out uint3 indices
){
    nwbCsgCapProxyGridPrimitive(localPrimitive, context.segments, indices);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../../graphics/csg/cap_proxy/ms_main.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
