// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "physics_common.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool nwbPhysicsShouldCollide(const NwbPhysicsGpuShapeProxy a, const NwbPhysicsGpuShapeProxy b){
    const uint bodyIdxA = a.bodyIndexTypeShape.x;
    const uint bodyIdxB = b.bodyIndexTypeShape.x;
    if(bodyIdxA == bodyIdxB)
        return false;

    const uint flagsUnion = a.bodyIndexTypeShape.w | b.bodyIndexTypeShape.w;
    const bool anyDynamic = (flagsUnion & NWB_PHYSICS_SHAPE_FLAG_DYNAMIC_BODY) != 0u;
    const bool anySensor = (flagsUnion & NWB_PHYSICS_SHAPE_FLAG_SENSOR) != 0u;
    const bool anyKinematic = (flagsUnion & NWB_PHYSICS_SHAPE_FLAG_KINEMATIC_BODY) != 0u;
    if(!anyDynamic && !(anySensor && anyKinematic))
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


uint nwbPhysicsHashCell(const int3 cell){
    const uint hx = asuint(cell.x) * 73856093u;
    const uint hy = asuint(cell.y) * 19349663u;
    const uint hz = asuint(cell.z) * 83492791u;
    return (hx ^ hy ^ hz) & (NWB_PHYSICS_GPU_GRID_TABLE_SIZE - 1u);
}

void nwbPhysicsEmitBroadphasePair(const uint pairSlot, const uint counterSlot, const uint maxPairs, const uint idxA, const uint idxB, const uint bodyA, const uint bodyB){
    uint writeIndex = 0u;
    InterlockedAdd(g_NwbPhysicsUintBuffers[counterSlot][0u], 1u, writeIndex);
    if(writeIndex < maxPairs){
        NwbPhysicsGpuBroadphasePair pairOut;
        pairOut.indices = uint4(idxA, idxB, bodyA, bodyB);
        g_NwbPhysicsBroadphasePairBuffers[pairSlot][writeIndex] = pairOut;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_PHYSICS_WORKGROUP_SIZE, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint idx = dispatchThreadID.x;
    const uint mode = g_NwbPhysicsPushConstants.modeOrSecondaryCount;
    const uint shapeCount = g_NwbPhysicsPushConstants.itemCount;
    const uint bodySimSlot = g_NwbPhysicsPushConstants.slots0.x;
    const uint shapeSlot = g_NwbPhysicsPushConstants.slots0.z;
    const uint pairSlot = g_NwbPhysicsPushConstants.slots1.x;
    const uint counterSlot = g_NwbPhysicsPushConstants.slots1.y;
    const uint gridSlot = g_NwbPhysicsPushConstants.slots1.z;
    const uint largeListSlot = g_NwbPhysicsPushConstants.slots1.w;

    if(mode == NWB_PHYSICS_BROADPHASE_MODE_GRID_CLEAR){
        if(idx >= NWB_PHYSICS_GPU_GRID_TABLE_SIZE)
            return;
        g_NwbPhysicsUintBuffers[gridSlot][idx * NWB_PHYSICS_GPU_GRID_BUCKET_STRIDE] = 0u;
        if(idx == 0u)
            g_NwbPhysicsUintBuffers[largeListSlot][0u] = 0u;
        return;
    }

    if(idx >= shapeCount)
        return;

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

        proxy.aabbMinPad = float4(minOut, 0.0f);
        proxy.aabbMaxPad = float4(maxOut, proxy.aabbMaxPad.w);
        g_NwbPhysicsShapeProxyBuffers[shapeSlot][idx] = proxy;
        return;
    }

    if(mode == NWB_PHYSICS_BROADPHASE_MODE_GRID_INSERT){
        NwbPhysicsGpuShapeProxy proxy = g_NwbPhysicsShapeProxyBuffers[shapeSlot][idx];
        const float invCellSize = g_NwbPhysicsPushConstants.deltaTime;
        const int3 cMin = int3(floor(proxy.aabbMinPad.xyz * invCellSize));
        const int3 cMax = int3(floor(proxy.aabbMaxPad.xyz * invCellSize));
        const int3 span = cMax - cMin + int3(1, 1, 1);

        bool isLarge = (span.x <= 0 || span.y <= 0 || span.z <= 0 || span.x > 3 || span.y > 3 || span.z > 3);
        if(!isLarge){
            const uint cellCount = uint(span.x) * uint(span.y) * uint(span.z);
            if(cellCount > NWB_PHYSICS_GPU_GRID_MAX_CELLS_PER_SHAPE)
                isLarge = true;
        }

        if(!isLarge){
            for(int cz = cMin.z; cz <= cMax.z; ++cz){
                for(int cy = cMin.y; cy <= cMax.y; ++cy){
                    for(int cx = cMin.x; cx <= cMax.x; ++cx){
                        const uint bucket = nwbPhysicsHashCell(int3(cx, cy, cz));
                        const uint base = bucket * NWB_PHYSICS_GPU_GRID_BUCKET_STRIDE;
                        uint slotIdx = 0u;
                        InterlockedAdd(g_NwbPhysicsUintBuffers[gridSlot][base], 1u, slotIdx);
                        if(slotIdx < NWB_PHYSICS_GPU_GRID_BUCKET_CAPACITY)
                            g_NwbPhysicsUintBuffers[gridSlot][base + 1u + slotIdx] = idx;
                        else
                            isLarge = true;
                    }
                }
            }
        }

        if(isLarge){
            uint largePos = 0u;
            InterlockedAdd(g_NwbPhysicsUintBuffers[largeListSlot][0u], 1u, largePos);
            if(largePos < shapeCount)
                g_NwbPhysicsUintBuffers[largeListSlot][1u + largePos] = idx;
        }

        proxy.aabbMinPad.w = isLarge ? 1.0f : 0.0f;
        g_NwbPhysicsShapeProxyBuffers[shapeSlot][idx] = proxy;
        return;
    }

    const uint maxPairs = g_NwbPhysicsPushConstants.flags;
    const NwbPhysicsGpuShapeProxy proxyA = g_NwbPhysicsShapeProxyBuffers[shapeSlot][idx];
    const float3 minA = proxyA.aabbMinPad.xyz;
    const float3 maxA = proxyA.aabbMaxPad.xyz;

    if(mode == NWB_PHYSICS_BROADPHASE_MODE_GRID_EMIT_PAIRS){
        const float invCellSize = g_NwbPhysicsPushConstants.deltaTime;
        const bool isLargeA = (proxyA.aabbMinPad.w > 0.5f);

        if(!isLargeA){
            const int3 cMinA = int3(floor(minA * invCellSize));
            const int3 cMaxA = int3(floor(maxA * invCellSize));
            for(int cz = cMinA.z; cz <= cMaxA.z; ++cz){
                for(int cy = cMinA.y; cy <= cMaxA.y; ++cy){
                    for(int cx = cMinA.x; cx <= cMaxA.x; ++cx){
                        const uint bucket = nwbPhysicsHashCell(int3(cx, cy, cz));
                        const uint base = bucket * NWB_PHYSICS_GPU_GRID_BUCKET_STRIDE;
                        const uint count = min(g_NwbPhysicsUintBuffers[gridSlot][base], NWB_PHYSICS_GPU_GRID_BUCKET_CAPACITY);
                        for(uint k = 0u; k < count; ++k){
                            const uint idxB = g_NwbPhysicsUintBuffers[gridSlot][base + 1u + k];
                            if(idxB <= idx)
                                continue;

                            bool seenInBucket = false;
                            for(uint p = 0u; p < k; ++p){
                                if(g_NwbPhysicsUintBuffers[gridSlot][base + 1u + p] == idxB){
                                    seenInBucket = true;
                                    break;
                                }
                            }
                            if(seenInBucket)
                                continue;

                            const NwbPhysicsGpuShapeProxy proxyB = g_NwbPhysicsShapeProxyBuffers[shapeSlot][idxB];
                            if(proxyB.aabbMinPad.w > 0.5f)
                                continue;
                            const int3 cMinB = int3(floor(proxyB.aabbMinPad.xyz * invCellSize));
                            const int3 canon = max(cMinA, cMinB);
                            if(cx != canon.x || cy != canon.y || cz != canon.z)
                                continue;
                            if(!nwbPhysicsShouldCollide(proxyA, proxyB))
                                continue;
                            if(!nwbPhysicsAabbOverlaps(minA, maxA, proxyB.aabbMinPad.xyz, proxyB.aabbMaxPad.xyz))
                                continue;
                            nwbPhysicsEmitBroadphasePair(pairSlot, counterSlot, maxPairs, idx, idxB, proxyA.bodyIndexTypeShape.x, proxyB.bodyIndexTypeShape.x);
                        }
                    }
                }
            }
        }

        const uint largeCount = min(g_NwbPhysicsUintBuffers[largeListSlot][0u], shapeCount);
        for(uint j = 0u; j < largeCount; ++j){
            const uint otherIdx = g_NwbPhysicsUintBuffers[largeListSlot][1u + j];
            if(otherIdx == idx)
                continue;
            if(isLargeA && otherIdx <= idx)
                continue;
            const NwbPhysicsGpuShapeProxy proxyB = g_NwbPhysicsShapeProxyBuffers[shapeSlot][otherIdx];
            if(!nwbPhysicsShouldCollide(proxyA, proxyB))
                continue;
            if(!nwbPhysicsAabbOverlaps(minA, maxA, proxyB.aabbMinPad.xyz, proxyB.aabbMaxPad.xyz))
                continue;
            const uint outA = idx < otherIdx ? idx : otherIdx;
            const uint outB = idx < otherIdx ? otherIdx : idx;
            const uint bodyOutA = idx < otherIdx ? proxyA.bodyIndexTypeShape.x : proxyB.bodyIndexTypeShape.x;
            const uint bodyOutB = idx < otherIdx ? proxyB.bodyIndexTypeShape.x : proxyA.bodyIndexTypeShape.x;
            nwbPhysicsEmitBroadphasePair(pairSlot, counterSlot, maxPairs, outA, outB, bodyOutA, bodyOutB);
        }
        return;
    }

    for(uint idxB = idx + 1u; idxB < shapeCount; ++idxB){
        const NwbPhysicsGpuShapeProxy proxyB = g_NwbPhysicsShapeProxyBuffers[shapeSlot][idxB];
        if(!nwbPhysicsShouldCollide(proxyA, proxyB))
            continue;
        if(!nwbPhysicsAabbOverlaps(minA, maxA, proxyB.aabbMinPad.xyz, proxyB.aabbMaxPad.xyz))
            continue;
        nwbPhysicsEmitBroadphasePair(pairSlot, counterSlot, maxPairs, idx, idxB, proxyA.bodyIndexTypeShape.x, proxyB.bodyIndexTypeShape.x);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

