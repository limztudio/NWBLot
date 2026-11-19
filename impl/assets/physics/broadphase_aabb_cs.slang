// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "physics_common.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool nwbPhysicsShouldCollide(const NwbPhysicsGpuShapeProxy a, const NwbPhysicsGpuShapeProxy b){
    const uint bodyIdxA = a.bodyIndexTypeShape.x;
    const uint bodyIdxB = b.bodyIndexTypeShape.x;
    if(bodyIdxA == bodyIdxB)
        return false;

    const bool anyDynamic = ((a.bodyIndexTypeShape.w | b.bodyIndexTypeShape.w) & NWB_PHYSICS_SHAPE_FLAG_DYNAMIC_BODY) != 0u;
    if(!anyDynamic)
        return false;

    const int groupA = asint(a.filterData.z);
    const int groupB = asint(b.filterData.z);
    if(groupA == groupB && groupA != 0)
        return groupA > 0;

    return ((a.filterData.x & b.filterData.y) != 0u) && ((b.filterData.x & a.filterData.y) != 0u);
}

bool nwbPhysicsAabbOverlaps(const float3 minA, const float3 maxA, const float3 minB, const float3 maxB){
    return all(minA <= maxB) && all(minB <= maxA);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_PHYSICS_WORKGROUP_SIZE, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint idx = dispatchThreadID.x;
    const uint shapeCount = g_NwbPhysicsPushConstants.itemCount;
    if(idx >= shapeCount)
        return;

    const uint mode = g_NwbPhysicsPushConstants.modeOrSecondaryCount;
    const uint bodySimSlot = g_NwbPhysicsPushConstants.slots0.x;
    const uint shapeSlot = g_NwbPhysicsPushConstants.slots0.z;

    if(mode == NWB_PHYSICS_BROADPHASE_MODE_UPDATE_AABBS){
        NwbPhysicsGpuShapeProxy proxy = g_NwbPhysicsShapeProxyBuffers[shapeSlot][idx];
        const NwbPhysicsGpuBodySim bodySim = g_NwbPhysicsBodySimBuffers[bodySimSlot][proxy.bodyIndexTypeShape.x];
        const float4 bodyQ = bodySim.rotation;
        const float3 bodyLocalCenter = float3(bodySim.invInertiaWorldRow0.w, bodySim.invInertiaWorldRow1.w, bodySim.invInertiaWorldRow2.w);
        const float3 bodyOrigin = bodySim.positionInvMass.xyz - nwbPhysicsQuatRotate(bodyLocalCenter, bodyQ);

        const uint shapeType = proxy.bodyIndexTypeShape.y;
        const float radius = proxy.localCenterRadius.w;
        const float margin = proxy.localExtentOrP1.w;

        float3 minOut = float3(0.0f, 0.0f, 0.0f);
        float3 maxOut = float3(0.0f, 0.0f, 0.0f);

        if(shapeType == NWB_PHYSICS_SHAPE_TYPE_SPHERE){
            const float3 worldCenter = bodyOrigin + nwbPhysicsQuatRotate(proxy.localCenterRadius.xyz, bodyQ);
            const float r = radius + margin;
            minOut = worldCenter - float3(r, r, r);
            maxOut = worldCenter + float3(r, r, r);
        }
        else if(shapeType == NWB_PHYSICS_SHAPE_TYPE_CAPSULE){
            const float3 p0 = bodyOrigin + nwbPhysicsQuatRotate(proxy.localCenterRadius.xyz, bodyQ);
            const float3 p1 = bodyOrigin + nwbPhysicsQuatRotate(proxy.localExtentOrP1.xyz, bodyQ);
            const float r = radius + margin;
            minOut = min(p0, p1) - float3(r, r, r);
            maxOut = max(p0, p1) + float3(r, r, r);
        }
        else{
            const float4 worldQ = nwbPhysicsQuatNormalize(nwbPhysicsQuatMul(bodyQ, proxy.localRotation));
            const float3 worldCenter = bodyOrigin + nwbPhysicsQuatRotate(proxy.localCenterRadius.xyz, bodyQ);
            const float3 axisX = abs(nwbPhysicsQuatRotate(float3(1.0f, 0.0f, 0.0f), worldQ));
            const float3 axisY = abs(nwbPhysicsQuatRotate(float3(0.0f, 1.0f, 0.0f), worldQ));
            const float3 axisZ = abs(nwbPhysicsQuatRotate(float3(0.0f, 0.0f, 1.0f), worldQ));
            const float3 h = proxy.localExtentOrP1.xyz;
            const float3 extent = axisX * h.x + axisY * h.y + axisZ * h.z + float3(radius + margin, radius + margin, radius + margin);
            minOut = worldCenter - extent;
            maxOut = worldCenter + extent;
        }

        proxy.aabbMinPad = float4(minOut, proxy.aabbMinPad.w);
        proxy.aabbMaxPad = float4(maxOut, proxy.aabbMaxPad.w);
        g_NwbPhysicsShapeProxyBuffers[shapeSlot][idx] = proxy;
        return;
    }

    const uint pairSlot = g_NwbPhysicsPushConstants.slots1.x;
    const uint counterSlot = g_NwbPhysicsPushConstants.slots1.y;
    const uint maxPairs = g_NwbPhysicsPushConstants.flags;
    const NwbPhysicsGpuShapeProxy proxyA = g_NwbPhysicsShapeProxyBuffers[shapeSlot][idx];
    const float3 minA = proxyA.aabbMinPad.xyz;
    const float3 maxA = proxyA.aabbMaxPad.xyz;

    for(uint idxB = idx + 1u; idxB < shapeCount; ++idxB){
        const NwbPhysicsGpuShapeProxy proxyB = g_NwbPhysicsShapeProxyBuffers[shapeSlot][idxB];
        if(!nwbPhysicsShouldCollide(proxyA, proxyB))
            continue;
        if(!nwbPhysicsAabbOverlaps(minA, maxA, proxyB.aabbMinPad.xyz, proxyB.aabbMaxPad.xyz))
            continue;

        uint writeIndex = 0u;
        InterlockedAdd(g_NwbPhysicsUintBuffers[counterSlot][0u], 1u, writeIndex);
        if(writeIndex < maxPairs){
            NwbPhysicsGpuBroadphasePair pairOut;
            pairOut.indices = uint4(idx, idxB, proxyA.bodyIndexTypeShape.x, proxyB.bodyIndexTypeShape.x);
            g_NwbPhysicsBroadphasePairBuffers[pairSlot][writeIndex] = pairOut;
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

