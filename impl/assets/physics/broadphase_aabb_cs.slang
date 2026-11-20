// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "physics_common.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool nwbPhysicsShouldCollideFields(const uint4 metaA, const uint4 filterA, const uint4 metaB, const uint4 filterB){
    const uint bodyIdxA = metaA.x;
    const uint bodyIdxB = metaB.x;
    if(bodyIdxA == bodyIdxB)
        return false;

    const uint flagsUnion = metaA.w | metaB.w;
    const bool anyAwake = (flagsUnion & NWB_PHYSICS_SHAPE_FLAG_AWAKE_BODY) != 0u;
    if(!anyAwake)
        return false;

    const bool anyDynamic = (flagsUnion & NWB_PHYSICS_SHAPE_FLAG_DYNAMIC_BODY) != 0u;
    const bool anySensor = (flagsUnion & NWB_PHYSICS_SHAPE_FLAG_SENSOR) != 0u;
    const bool anyKinematic = (flagsUnion & NWB_PHYSICS_SHAPE_FLAG_KINEMATIC_BODY) != 0u;
    if(!anyDynamic && !(anySensor && anyKinematic))
        return false;

    const int groupA = asint(filterA.z);
    const int groupB = asint(filterB.z);
    if(groupA == groupB && groupA != 0)
        return groupA > 0;

    return ((filterA.x & filterB.y) != 0u) && ((filterB.x & filterA.y) != 0u);
}

bool nwbPhysicsShouldCollide(const NwbPhysicsGpuShapeProxy a, const NwbPhysicsGpuShapeProxy b){
    return nwbPhysicsShouldCollideFields(a.bodyIndexTypeShape, a.filterData, b.bodyIndexTypeShape, b.filterData);
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
        g_NwbPhysicsUintBuffers[gridSlot][idx] = 0u;
        if(idx == 0u)
            g_NwbPhysicsUintBuffers[largeListSlot][0u] = 0u;
        return;
    }

    if(mode == NWB_PHYSICS_BROADPHASE_MODE_FINALIZE_INDIRECT){
        if(idx == 0u){
            const uint rawPairs = g_NwbPhysicsUintBuffers[counterSlot][0u];
            const uint clampedPairs = min(rawPairs, g_NwbPhysicsPushConstants.flags);
            const uint groupsX = clampedPairs > 0u ? ((clampedPairs + NWB_PHYSICS_WORKGROUP_SIZE - 1u) / NWB_PHYSICS_WORKGROUP_SIZE) : 0u;
            const uint indirectSlot = g_NwbPhysicsPushConstants.slots1.z;
            g_NwbPhysicsUintBuffers[counterSlot][1u] = clampedPairs;
            g_NwbPhysicsUintBuffers[indirectSlot][0u] = groupsX;
            g_NwbPhysicsUintBuffers[indirectSlot][1u] = 1u;
            g_NwbPhysicsUintBuffers[indirectSlot][2u] = 1u;
        }
        return;
    }

    if(idx >= shapeCount)
        return;

    if(mode == NWB_PHYSICS_BROADPHASE_MODE_UPDATE_AABBS){
        const uint4 bodyMeta = g_NwbPhysicsShapeProxyBuffers[shapeSlot][idx].bodyIndexTypeShape;
        const float4 localCenterRadius = g_NwbPhysicsShapeProxyBuffers[shapeSlot][idx].localCenterRadius;
        const float4 localExtentOrP1 = g_NwbPhysicsShapeProxyBuffers[shapeSlot][idx].localExtentOrP1;
        const float4 localRotation = g_NwbPhysicsShapeProxyBuffers[shapeSlot][idx].localRotation;

        const uint bodySlotIdx = bodyMeta.x;
        const float3 bodyCenter = g_NwbPhysicsBodySimBuffers[bodySimSlot][bodySlotIdx].positionInvMass.xyz;
        const float4 bodyQ = g_NwbPhysicsBodySimBuffers[bodySimSlot][bodySlotIdx].rotation;
        const float3 bodyLocalCenter = g_NwbPhysicsBodySimBuffers[bodySimSlot][bodySlotIdx].localCenterMinExtent.xyz;
        const float3 bodyOrigin = bodyCenter - nwbPhysicsQuatRotate(bodyLocalCenter, bodyQ);

        const uint shapeType = bodyMeta.y;
        const float radius = localCenterRadius.w;
        const float margin = localExtentOrP1.w;

        float3 minOut = float3(0.0f, 0.0f, 0.0f);
        float3 maxOut = float3(0.0f, 0.0f, 0.0f);

        if(shapeType == NWB_PHYSICS_SHAPE_TYPE_SPHERE){
            const float3 worldCenter = bodyOrigin + nwbPhysicsQuatRotate(localCenterRadius.xyz, bodyQ);
            const float r = radius + margin;
            minOut = worldCenter - float3(r, r, r);
            maxOut = worldCenter + float3(r, r, r);
        }
        else if(shapeType == NWB_PHYSICS_SHAPE_TYPE_CAPSULE){
            const float3 p0 = bodyOrigin + nwbPhysicsQuatRotate(localCenterRadius.xyz, bodyQ);
            const float3 p1 = bodyOrigin + nwbPhysicsQuatRotate(localExtentOrP1.xyz, bodyQ);
            const float r = radius + margin;
            minOut = min(p0, p1) - float3(r, r, r);
            maxOut = max(p0, p1) + float3(r, r, r);
        }
        else{
            const float4 worldQ = nwbPhysicsQuatNormalize(nwbPhysicsQuatMul(bodyQ, localRotation));
            const float3 worldCenter = bodyOrigin + nwbPhysicsQuatRotate(localCenterRadius.xyz, bodyQ);
            const float3 axisX = abs(nwbPhysicsQuatRotate(float3(1.0f, 0.0f, 0.0f), worldQ));
            const float3 axisY = abs(nwbPhysicsQuatRotate(float3(0.0f, 1.0f, 0.0f), worldQ));
            const float3 axisZ = abs(nwbPhysicsQuatRotate(float3(0.0f, 0.0f, 1.0f), worldQ));
            const float3 h = localExtentOrP1.xyz;
            const float3 extent = axisX * h.x + axisY * h.y + axisZ * h.z + float3(radius + margin, radius + margin, radius + margin);
            minOut = worldCenter - extent;
            maxOut = worldCenter + extent;
        }

        g_NwbPhysicsShapeProxyBuffers[shapeSlot][idx].aabbMinPad = float4(minOut, 0.0f);
        g_NwbPhysicsShapeProxyBuffers[shapeSlot][idx].aabbMaxPad.xyz = maxOut;
        return;
    }

    if(mode == NWB_PHYSICS_BROADPHASE_MODE_GRID_INSERT){
        const float3 aabbMin = g_NwbPhysicsShapeProxyBuffers[shapeSlot][idx].aabbMinPad.xyz;
        const float3 aabbMax = g_NwbPhysicsShapeProxyBuffers[shapeSlot][idx].aabbMaxPad.xyz;
        const float invCellSize = g_NwbPhysicsPushConstants.deltaTime;
        const float3 minScaled = aabbMin * invCellSize;
        const float3 maxScaled = aabbMax * invCellSize;
        const bool finiteBounds = all(isfinite(minScaled)) && all(isfinite(maxScaled)) && all(abs(minScaled) < 1.0e6f) && all(abs(maxScaled) < 1.0e6f);

        bool isLarge = !finiteBounds;
        int3 cMin = int3(0, 0, 0);
        int3 span = int3(0, 0, 0);
        if(!isLarge){
            cMin = int3(floor(minScaled));
            const int3 cMax = int3(floor(maxScaled));
            span = cMax - cMin + int3(1, 1, 1);
            isLarge = (span.x <= 0 || span.y <= 0 || span.z <= 0 || span.x > 3 || span.y > 3 || span.z > 3);
            if(!isLarge){
                const uint cellCount = uint(span.x) * uint(span.y) * uint(span.z);
                if(cellCount > NWB_PHYSICS_GPU_GRID_MAX_CELLS_PER_SHAPE)
                    isLarge = true;
            }
        }

        if(!isLarge){
            for(int dz = 0; dz < span.z; ++dz){
                const int cz = cMin.z + dz;
                for(int dy = 0; dy < span.y; ++dy){
                    const int cy = cMin.y + dy;
                    for(int dx = 0; dx < span.x; ++dx){
                        const int cx = cMin.x + dx;
                        const uint bucket = nwbPhysicsHashCell(int3(cx, cy, cz));
                        uint slotIdx = 0u;
                        InterlockedAdd(g_NwbPhysicsUintBuffers[gridSlot][bucket], 1u, slotIdx);
                        if(slotIdx < NWB_PHYSICS_GPU_GRID_BUCKET_CAPACITY)
                            g_NwbPhysicsUintBuffers[gridSlot][NWB_PHYSICS_GPU_GRID_TABLE_SIZE + bucket * NWB_PHYSICS_GPU_GRID_BUCKET_CAPACITY + slotIdx] = idx;
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

        g_NwbPhysicsShapeProxyBuffers[shapeSlot][idx].aabbMinPad.w = isLarge ? 1.0f : 0.0f;
        return;
    }

    const uint maxPairs = g_NwbPhysicsPushConstants.flags;
    const float4 minPadA = g_NwbPhysicsShapeProxyBuffers[shapeSlot][idx].aabbMinPad;
    const float3 minA = minPadA.xyz;
    const float3 maxA = g_NwbPhysicsShapeProxyBuffers[shapeSlot][idx].aabbMaxPad.xyz;
    const uint4 metaA = g_NwbPhysicsShapeProxyBuffers[shapeSlot][idx].bodyIndexTypeShape;
    const uint4 filterA = g_NwbPhysicsShapeProxyBuffers[shapeSlot][idx].filterData;

    if(mode == NWB_PHYSICS_BROADPHASE_MODE_GRID_EMIT_PAIRS){
        if((metaA.w & NWB_PHYSICS_SHAPE_FLAG_AWAKE_BODY) == 0u)
            return;

        const float invCellSize = g_NwbPhysicsPushConstants.deltaTime;
        const bool isLargeA = (minPadA.w > 0.5f);

        if(!isLargeA){
            const float3 minScaledA = minA * invCellSize;
            const float3 maxScaledA = maxA * invCellSize;
            const bool finiteA = all(isfinite(minScaledA)) && all(isfinite(maxScaledA)) && all(abs(minScaledA) < 1.0e6f) && all(abs(maxScaledA) < 1.0e6f);
            if(finiteA){
                const int3 cMinA = int3(floor(minScaledA));
                const int3 cMaxA = int3(floor(maxScaledA));
                const int3 spanA = cMaxA - cMinA + int3(1, 1, 1);
                if(spanA.x > 0 && spanA.y > 0 && spanA.z > 0 && spanA.x <= 3 && spanA.y <= 3 && spanA.z <= 3){
                    uint bucketEntries[NWB_PHYSICS_GPU_GRID_BUCKET_CAPACITY];
                    for(int dz = 0; dz < spanA.z; ++dz){
                        const int cz = cMinA.z + dz;
                        for(int dy = 0; dy < spanA.y; ++dy){
                            const int cy = cMinA.y + dy;
                            for(int dx = 0; dx < spanA.x; ++dx){
                                const int cx = cMinA.x + dx;
                                const uint bucket = nwbPhysicsHashCell(int3(cx, cy, cz));
                                const uint entryBase = NWB_PHYSICS_GPU_GRID_TABLE_SIZE + bucket * NWB_PHYSICS_GPU_GRID_BUCKET_CAPACITY;
                                const uint count = min(g_NwbPhysicsUintBuffers[gridSlot][bucket], NWB_PHYSICS_GPU_GRID_BUCKET_CAPACITY);
                                for(uint k = 0u; k < count; ++k)
                                    bucketEntries[k] = g_NwbPhysicsUintBuffers[gridSlot][entryBase + k];

                                for(uint k = 0u; k < count; ++k){
                                    const uint idxB = bucketEntries[k];
                                    if(idxB == idx)
                                        continue;

                                    bool seenInBucket = false;
                                    for(uint p = 0u; p < k; ++p){
                                        if(bucketEntries[p] == idxB){
                                            seenInBucket = true;
                                            break;
                                        }
                                    }
                                    if(seenInBucket)
                                        continue;

                                    const float4 minPadB = g_NwbPhysicsShapeProxyBuffers[shapeSlot][idxB].aabbMinPad;
                                    if(minPadB.w > 0.5f)
                                        continue;
                                    const uint4 metaB = g_NwbPhysicsShapeProxyBuffers[shapeSlot][idxB].bodyIndexTypeShape;
                                    const bool awakeB = (metaB.w & NWB_PHYSICS_SHAPE_FLAG_AWAKE_BODY) != 0u;
                                    if(awakeB && idxB <= idx)
                                        continue;
                                    const float3 minScaledB = minPadB.xyz * invCellSize;
                                    if(!all(isfinite(minScaledB)) || any(abs(minScaledB) >= 1.0e6f))
                                        continue;
                                    const int3 cMinB = int3(floor(minScaledB));
                                    const int3 canon = max(cMinA, cMinB);
                                    if(cx != canon.x || cy != canon.y || cz != canon.z)
                                        continue;
                                    const uint4 filterB = g_NwbPhysicsShapeProxyBuffers[shapeSlot][idxB].filterData;
                                    if(!nwbPhysicsShouldCollideFields(metaA, filterA, metaB, filterB))
                                        continue;
                                    const float3 maxB = g_NwbPhysicsShapeProxyBuffers[shapeSlot][idxB].aabbMaxPad.xyz;
                                    if(!nwbPhysicsAabbOverlaps(minA, maxA, minPadB.xyz, maxB))
                                        continue;
                                    const uint outA = idx < idxB ? idx : idxB;
                                    const uint outB = idx < idxB ? idxB : idx;
                                    const uint bodyOutA = idx < idxB ? metaA.x : metaB.x;
                                    const uint bodyOutB = idx < idxB ? metaB.x : metaA.x;
                                    nwbPhysicsEmitBroadphasePair(pairSlot, counterSlot, maxPairs, outA, outB, bodyOutA, bodyOutB);
                                }
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
                const uint4 metaB = g_NwbPhysicsShapeProxyBuffers[shapeSlot][otherIdx].bodyIndexTypeShape;
                const uint4 filterB = g_NwbPhysicsShapeProxyBuffers[shapeSlot][otherIdx].filterData;
                if(!nwbPhysicsShouldCollideFields(metaA, filterA, metaB, filterB))
                    continue;
                const float3 minB = g_NwbPhysicsShapeProxyBuffers[shapeSlot][otherIdx].aabbMinPad.xyz;
                const float3 maxB = g_NwbPhysicsShapeProxyBuffers[shapeSlot][otherIdx].aabbMaxPad.xyz;
                if(!nwbPhysicsAabbOverlaps(minA, maxA, minB, maxB))
                    continue;
                const uint outA = idx < otherIdx ? idx : otherIdx;
                const uint outB = idx < otherIdx ? otherIdx : idx;
                const uint bodyOutA = idx < otherIdx ? metaA.x : metaB.x;
                const uint bodyOutB = idx < otherIdx ? metaB.x : metaA.x;
                nwbPhysicsEmitBroadphasePair(pairSlot, counterSlot, maxPairs, outA, outB, bodyOutA, bodyOutB);
            }
        }
        else{
            for(uint otherIdx = 0u; otherIdx < shapeCount; ++otherIdx){
                if(otherIdx == idx)
                    continue;
                const float4 minPadB = g_NwbPhysicsShapeProxyBuffers[shapeSlot][otherIdx].aabbMinPad;
                const uint4 metaB = g_NwbPhysicsShapeProxyBuffers[shapeSlot][otherIdx].bodyIndexTypeShape;
                const bool awakeB = (metaB.w & NWB_PHYSICS_SHAPE_FLAG_AWAKE_BODY) != 0u;
                const bool isLargeB = (minPadB.w > 0.5f);
                if(awakeB && (!isLargeB || otherIdx <= idx))
                    continue;
                const uint4 filterB = g_NwbPhysicsShapeProxyBuffers[shapeSlot][otherIdx].filterData;
                if(!nwbPhysicsShouldCollideFields(metaA, filterA, metaB, filterB))
                    continue;
                const float3 maxB = g_NwbPhysicsShapeProxyBuffers[shapeSlot][otherIdx].aabbMaxPad.xyz;
                if(!nwbPhysicsAabbOverlaps(minA, maxA, minPadB.xyz, maxB))
                    continue;
                const uint outA = idx < otherIdx ? idx : otherIdx;
                const uint outB = idx < otherIdx ? otherIdx : idx;
                const uint bodyOutA = idx < otherIdx ? metaA.x : metaB.x;
                const uint bodyOutB = idx < otherIdx ? metaB.x : metaA.x;
                nwbPhysicsEmitBroadphasePair(pairSlot, counterSlot, maxPairs, outA, outB, bodyOutA, bodyOutB);
            }
        }
        return;
    }

    if(mode == NWB_PHYSICS_BROADPHASE_MODE_EMIT_PAIRS){
        for(uint idxB = idx + 1u; idxB < shapeCount; ++idxB){
            const uint4 metaB = g_NwbPhysicsShapeProxyBuffers[shapeSlot][idxB].bodyIndexTypeShape;
            const uint4 filterB = g_NwbPhysicsShapeProxyBuffers[shapeSlot][idxB].filterData;
            if(!nwbPhysicsShouldCollideFields(metaA, filterA, metaB, filterB))
                continue;
            const float3 minB = g_NwbPhysicsShapeProxyBuffers[shapeSlot][idxB].aabbMinPad.xyz;
            const float3 maxB = g_NwbPhysicsShapeProxyBuffers[shapeSlot][idxB].aabbMaxPad.xyz;
            if(!nwbPhysicsAabbOverlaps(minA, maxA, minB, maxB))
                continue;
            nwbPhysicsEmitBroadphasePair(pairSlot, counterSlot, maxPairs, idx, idxB, metaA.x, metaB.x);
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

