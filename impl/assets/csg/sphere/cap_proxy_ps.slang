// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_CSG_MESH_INSTANCE_INDEX 0u

#include "../../graphics/csg/binding_slots.h"

#include "../../graphics/mesh/gbuffer_io.slangi"
#include "../../graphics/mesh/view_buffer.slangi"
#include "../../graphics/csg/cap_proxy/projection.slangi"

#include "cap_proxy_projection.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


float3 nwbCsgCapProxyShapeShrinkWorldPosition(const float3 worldPosition, const NwbCsgCutter cutter){
    const float3 shapePosition = nwbCsgTransformWorldToShape(worldPosition, cutter);
    return nwbCsgTransformShapeToWorld(nwbCsgCapProxyShrinkSphereShapePosition(shapePosition, cutter), cutter);
}

bool nwbCsgCapProxyShapeResolveWorldPosition(
    const float3 proxyWorldPosition,
    const NwbCsgCutter cutter,
    const NwbCsgReceiverRange receiverRange,
    out float3 outWorldPosition
){
    const float3 rayOrigin = nwbCsgCapProxyShapeRayOrigin(cutter);
    const float3 rayDirection = nwbCsgCapProxyShapeRayDirection(proxyWorldPosition, cutter, rayOrigin);
    const float radius = max(cutter.parameter0.x, 0.0001);
    const float b = dot(rayOrigin, rayDirection);
    const float c = dot(rayOrigin, rayOrigin) - radius * radius;
    const float discriminant = b * b - c;
    if(discriminant < 0.0 || !isfinite(discriminant)){
        outWorldPosition = nwbCsgCapProxyShapeShrinkWorldPosition(proxyWorldPosition, cutter);
        return false;
    }

    const float root = sqrt(discriminant);
    float nearestT = 1e30;
    nwbCsgCapProxyUpdateNearestReceiverRayT(-b - root, rayOrigin, rayDirection, cutter, receiverRange, nearestT);
    nwbCsgCapProxyUpdateNearestReceiverRayT(-b + root, rayOrigin, rayDirection, cutter, receiverRange, nearestT);
    if(nearestT >= 1e29){
        outWorldPosition = nwbCsgCapProxyShapeShrinkWorldPosition(proxyWorldPosition, cutter);
        return false;
    }

    outWorldPosition = nwbCsgTransformShapeToWorld(rayOrigin + rayDirection * nearestT, cutter);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../../graphics/csg/cap_proxy/ps_main.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
