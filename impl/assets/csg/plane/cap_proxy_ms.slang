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

void nwbCsgCapProxyPlanePrimitive(const uint localPrimitive, out uint3 indices){
    indices = localPrimitive == 0u ? uint3(0u, 2u, 1u) : uint3(0u, 3u, 2u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NwbCsgCapProxyShapeContext{
    float3 shapeNormal;
    float3 centerShape;
    float3 axis0;
    float3 axis1;
    float extent;
};

NwbCsgCapProxyShapeContext nwbCsgCapProxyBuildShapeContext(const NwbCsgCapProxyGpuData item, const NwbCsgCutter cutter){
    const float3 shapeNormal = nwbCsgPlaneNormal(cutter);
    const float3 receiverExtents = nwbCsgCapProxyBoundsExtents(item.receiverBounds);
    NwbCsgCapProxyShapeContext context;
    context.shapeNormal = shapeNormal;
    context.centerShape = -shapeNormal * cutter.parameter0.w;
    context.axis0 = nwbCsgCapProxyPlaneAxis(shapeNormal, false);
    context.axis1 = nwbCsgCapProxyPlaneAxis(shapeNormal, true);
    context.extent = max(length(receiverExtents) * 1.5, 0.5);
    return context;
}

uint nwbCsgCapProxyShapeVertexCount(const NwbCsgCapProxyShapeContext context){
    return 4u;
}

uint nwbCsgCapProxyShapePrimitiveCount(const NwbCsgCapProxyShapeContext context){
    return 2u;
}

NwbMeshRasterVertex nwbCsgCapProxyShapeVertex(
    const NwbCsgCapProxyShapeContext context,
    const NwbCsgCapProxyGpuData item,
    const NwbCsgCutter cutter,
    const uint localVertex
){
    const float sx = (localVertex == 1u || localVertex == 2u) ? 1.0 : -1.0;
    const float sy = localVertex >= 2u ? 1.0 : -1.0;
    const float3 shapePosition = context.centerShape + (context.axis0 * sx + context.axis1 * sy) * context.extent;
    return nwbCsgCapProxyBuildVertex(
        nwbCsgTransformShapeToWorld(shapePosition, cutter),
        context.shapeNormal,
        cutter,
        item.receiverCutterShapePass.x,
        item.receiverCutterShapePass.y,
        item.color
    );
}

void nwbCsgCapProxyShapePrimitive(
    const NwbCsgCapProxyShapeContext context,
    const uint localPrimitive,
    out uint3 indices
){
    nwbCsgCapProxyPlanePrimitive(localPrimitive, indices);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../../graphics/csg/cap_proxy/ms_main.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
