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
    return nwbCsgTransformShapeToWorld(nwbCsgCapProxyShrinkCapsuleShapePosition(shapePosition, cutter), cutter);
}

bool nwbCsgCapProxyUpdateCapsuleSphereRayHit(
    const float3 rayOrigin,
    const float3 rayDirection,
    const float3 sphereCenter,
    const float radius,
    const float capSide,
    const NwbCsgCutter cutter,
    const NwbCsgReceiverRange receiverRange,
    inout float nearestT
){
    const float3 localOrigin = rayOrigin - sphereCenter;
    const float b = dot(localOrigin, rayDirection);
    const float c = dot(localOrigin, localOrigin) - radius * radius;
    const float discriminant = b * b - c;
    if(discriminant < 0.0 || !isfinite(discriminant))
        return false;

    const float root = sqrt(discriminant);
    bool updated = false;
    const float t0 = -b - root;
    const float y0 = rayOrigin.y + rayDirection.y * t0;
    if((y0 - sphereCenter.y) * capSide >= -0.0001)
        updated = nwbCsgCapProxyUpdateNearestReceiverRayT(t0, rayOrigin, rayDirection, cutter, receiverRange, nearestT) || updated;

    const float t1 = -b + root;
    const float y1 = rayOrigin.y + rayDirection.y * t1;
    if((y1 - sphereCenter.y) * capSide >= -0.0001)
        updated = nwbCsgCapProxyUpdateNearestReceiverRayT(t1, rayOrigin, rayDirection, cutter, receiverRange, nearestT) || updated;

    return updated;
}

bool nwbCsgCapProxyUpdateCapsuleCylinderRayHit(
    const float3 rayOrigin,
    const float3 rayDirection,
    const float radius,
    const float halfHeight,
    const NwbCsgCutter cutter,
    const NwbCsgReceiverRange receiverRange,
    inout float nearestT
){
    const float a = rayDirection.x * rayDirection.x + rayDirection.z * rayDirection.z;
    if(a <= 0.00000001 || !isfinite(a))
        return false;

    const float b = rayOrigin.x * rayDirection.x + rayOrigin.z * rayDirection.z;
    const float c = rayOrigin.x * rayOrigin.x + rayOrigin.z * rayOrigin.z - radius * radius;
    const float discriminant = b * b - a * c;
    if(discriminant < 0.0 || !isfinite(discriminant))
        return false;

    const float root = sqrt(discriminant);
    const float invA = 1.0 / a;
    const float t0 = (-b - root) * invA;
    const float t1 = (-b + root) * invA;
    bool updated = false;
    const float y0 = rayOrigin.y + rayDirection.y * t0;
    if(y0 >= -halfHeight && y0 <= halfHeight)
        updated = nwbCsgCapProxyUpdateNearestReceiverRayT(t0, rayOrigin, rayDirection, cutter, receiverRange, nearestT) || updated;

    const float y1 = rayOrigin.y + rayDirection.y * t1;
    if(y1 >= -halfHeight && y1 <= halfHeight)
        updated = nwbCsgCapProxyUpdateNearestReceiverRayT(t1, rayOrigin, rayDirection, cutter, receiverRange, nearestT) || updated;

    return updated;
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
    const float halfHeight = max(cutter.parameter0.y, 0.0);
    float nearestT = 1e30;

    nwbCsgCapProxyUpdateCapsuleCylinderRayHit(rayOrigin, rayDirection, radius, halfHeight, cutter, receiverRange, nearestT);
    nwbCsgCapProxyUpdateCapsuleSphereRayHit(rayOrigin, rayDirection, float3(0.0, halfHeight, 0.0), radius, 1.0, cutter, receiverRange, nearestT);
    nwbCsgCapProxyUpdateCapsuleSphereRayHit(rayOrigin, rayDirection, float3(0.0, -halfHeight, 0.0), radius, -1.0, cutter, receiverRange, nearestT);
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
