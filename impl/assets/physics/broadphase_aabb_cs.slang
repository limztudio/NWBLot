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


bool nwbPhysicsIsComplexPair(const uint4 metaA, const uint4 metaB){
    const uint typeA = metaA.y;
    const uint typeB = metaB.y;
    const bool isBoxA = (typeA == NWB_PHYSICS_SHAPE_TYPE_HULL) && ((metaA.w & NWB_PHYSICS_SHAPE_FLAG_IS_BOX) != 0u);
    const bool isBoxB = (typeB == NWB_PHYSICS_SHAPE_TYPE_HULL) && ((metaB.w & NWB_PHYSICS_SHAPE_FLAG_IS_BOX) != 0u);
    return (typeA == NWB_PHYSICS_SHAPE_TYPE_MESH)
        || (typeB == NWB_PHYSICS_SHAPE_TYPE_MESH)
        || (typeA == NWB_PHYSICS_SHAPE_TYPE_HULL && !isBoxA)
        || (typeB == NWB_PHYSICS_SHAPE_TYPE_HULL && !isBoxB);
}

uint nwbPhysicsHashCell(const int3 cell){
    const uint hx = asuint(cell.x) * 73856093u;
    const uint hy = asuint(cell.y) * 19349663u;
    const uint hz = asuint(cell.z) * 83492791u;
    return (hx ^ hy ^ hz) & (NWB_PHYSICS_GPU_GRID_TABLE_SIZE - 1u);
}

groupshared uint s_LargeIdx[NWB_PHYSICS_WORKGROUP_SIZE];
groupshared float3 s_LargeMin[NWB_PHYSICS_WORKGROUP_SIZE];
groupshared float3 s_LargeMax[NWB_PHYSICS_WORKGROUP_SIZE];
groupshared uint4 s_LargeMeta[NWB_PHYSICS_WORKGROUP_SIZE];
groupshared uint4 s_LargeFilter[NWB_PHYSICS_WORKGROUP_SIZE];

void nwbPhysicsEmitBroadphasePair(
    const uint pairSlot,
    const uint counterSlot,
    const uint maxPairs,
    const uint idxA,
    const uint idxB,
    const uint bodyA,
    const uint bodyB,
    const bool isComplex
){
    const uint primOffset = WavePrefixCountBits(!isComplex);
    const uint primCount = WaveActiveCountBits(!isComplex);
    const uint cpxOffset = WavePrefixCountBits(isComplex);
    const uint cpxCount = WaveActiveCountBits(isComplex);
    uint primBase = 0u;
    uint cpxBase = 0u;
    if(WaveIsFirstLane()){
        if(primCount > 0u)
            InterlockedAdd(g_NwbPhysicsUintBuffers[counterSlot][0u], primCount, primBase);
        if(cpxCount > 0u)
            InterlockedAdd(g_NwbPhysicsUintBuffers[counterSlot][1u], cpxCount, cpxBase);
    }
    primBase = WaveReadLaneFirst(primBase);
    cpxBase = WaveReadLaneFirst(cpxBase);
    const uint writeIndex = isComplex ? (cpxBase + cpxOffset) : (primBase + primOffset);
    if(writeIndex < maxPairs){
        NwbPhysicsGpuBroadphasePair pairOut;
        pairOut.indices = uint4(idxA, idxB, bodyA, bodyB);
        const uint dstIdx = isComplex ? (maxPairs - 1u - writeIndex) : writeIndex;
        g_NwbPhysicsBroadphasePairBuffers[pairSlot][dstIdx] = pairOut;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[require(spvGroupNonUniform, spvGroupNonUniformBallot)]
[numthreads(NWB_PHYSICS_WORKGROUP_SIZE, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID, uint3 groupThreadID : SV_GroupThreadID){
    const uint tid = dispatchThreadID.x;
    const uint mode = g_NwbPhysicsPushConstants.modeOrSecondaryCount;
    const uint itemCount = g_NwbPhysicsPushConstants.itemCount;
    const uint itemOffset = g_NwbPhysicsPushConstants.itemOffset;
    const uint bodyPoseSlot = g_NwbPhysicsPushConstants.slots0.x;
    const uint shapeAabbSlot = g_NwbPhysicsPushConstants.slots0.y;
    const uint shapeSlot = g_NwbPhysicsPushConstants.slots0.z;
    const uint pairSlot = g_NwbPhysicsPushConstants.slots1.x;
    const uint counterSlot = g_NwbPhysicsPushConstants.slots1.y;
    const uint gridSlot = g_NwbPhysicsPushConstants.slots1.z;
    const uint largeListSlot = g_NwbPhysicsPushConstants.slots1.w;

    if(mode == NWB_PHYSICS_BROADPHASE_MODE_GRID_CLEAR){
        if(tid >= NWB_PHYSICS_GPU_GRID_TABLE_SIZE)
            return;
        g_NwbPhysicsUintBuffers[gridSlot][tid] = 0u;
        if(tid == 0u)
            g_NwbPhysicsUintBuffers[largeListSlot][0u] = 0u;
        return;
    }

    if(mode == NWB_PHYSICS_BROADPHASE_MODE_FINALIZE_INDIRECT){
        if(tid == 0u){
            const uint maxPairs = g_NwbPhysicsPushConstants.flags;
            const uint rawPrim = g_NwbPhysicsUintBuffers[counterSlot][0u];
            const uint rawComplex = g_NwbPhysicsUintBuffers[counterSlot][1u];
            const bool overflow = (rawPrim > maxPairs) || (rawComplex > (maxPairs - rawPrim));
            const uint clampedTotal = overflow ? maxPairs : (rawPrim + rawComplex);
            const uint primGroups = (overflow || rawPrim == 0u) ? 0u : ((rawPrim + NWB_PHYSICS_WORKGROUP_SIZE - 1u) / NWB_PHYSICS_WORKGROUP_SIZE);
            const uint complexGroups = (overflow || rawComplex == 0u) ? 0u : ((rawComplex + NWB_PHYSICS_WORKGROUP_SIZE - 1u) / NWB_PHYSICS_WORKGROUP_SIZE);
            const uint indirectSlot = g_NwbPhysicsPushConstants.slots1.z;
            g_NwbPhysicsUintBuffers[counterSlot][3u] = clampedTotal;
            g_NwbPhysicsUintBuffers[indirectSlot][0u] = primGroups;
            g_NwbPhysicsUintBuffers[indirectSlot][1u] = 1u;
            g_NwbPhysicsUintBuffers[indirectSlot][2u] = 1u;
            g_NwbPhysicsUintBuffers[indirectSlot][3u] = complexGroups;
            g_NwbPhysicsUintBuffers[indirectSlot][4u] = 1u;
            g_NwbPhysicsUintBuffers[indirectSlot][5u] = 1u;
        }
        return;
    }

    if(mode == NWB_PHYSICS_BROADPHASE_MODE_UPDATE_AABBS){
        if(tid >= itemCount)
            return;
        const uint shapeIdx = itemOffset + tid;
        const NwbPhysicsGpuShapeProxy sp = g_NwbPhysicsShapeProxyBuffers[shapeSlot][shapeIdx];
        const uint4 bodyMeta = sp.bodyIndexTypeShape;
        const float4 localCenterRadius = sp.localCenterRadius;
        const float4 localExtentOrP1 = sp.localExtentOrP1;
        const float4 localRotationOrMeta = sp.localRotationOrMeta;

        const uint bodySlotIdx = bodyMeta.x;
        const NwbPhysicsGpuBodyPose bodyPose = g_NwbPhysicsBodyPoseBuffers[bodyPoseSlot][bodySlotIdx];
        const float3 bodyOrigin = bodyPose.originPad.xyz;
        const float4 bodyQ = bodyPose.rotation;
        const float3 sweepDisp = bodyPose.sweepDispFlags.xyz;

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
            const float4 worldQ = ((bodyMeta.w & NWB_PHYSICS_SHAPE_FLAG_IS_BOX) != 0u && localRotationOrMeta.w < 0.999999f)
                ? nwbPhysicsQuatNormalize(nwbPhysicsQuatMul(bodyQ, localRotationOrMeta))
                : bodyQ;
            const float3 worldCenter = bodyOrigin + nwbPhysicsQuatRotate(localCenterRadius.xyz, bodyQ);
            float3 axisX;
            float3 axisY;
            float3 axisZ;
            nwbPhysicsQuatToAxes(worldQ, axisX, axisY, axisZ);
            axisX = abs(axisX);
            axisY = abs(axisY);
            axisZ = abs(axisZ);
            const float3 h = localExtentOrP1.xyz;
            const float3 extent = axisX * h.x + axisY * h.y + axisZ * h.z + float3(radius + margin, radius + margin, radius + margin);
            minOut = worldCenter - extent;
            maxOut = worldCenter + extent;
        }

        minOut += min(sweepDisp, float3(0.0f, 0.0f, 0.0f));
        maxOut += max(sweepDisp, float3(0.0f, 0.0f, 0.0f));

        g_NwbPhysicsFloat4Buffers[shapeAabbSlot][shapeIdx * 2u + 0u] = float4(minOut, 0.0f);
        g_NwbPhysicsFloat4Buffers[shapeAabbSlot][shapeIdx * 2u + 1u] = float4(maxOut, asfloat(bodySlotIdx));
        return;
    }

    if(mode == NWB_PHYSICS_BROADPHASE_MODE_GRID_INSERT){
        if(tid >= itemCount)
            return;
        const float3 aabbMin = g_NwbPhysicsFloat4Buffers[shapeAabbSlot][tid * 2u + 0u].xyz;
        const float3 aabbMax = g_NwbPhysicsFloat4Buffers[shapeAabbSlot][tid * 2u + 1u].xyz;
        const float invCellSize = g_NwbPhysicsPushConstants.deltaTime;
        const float3 minScaled = aabbMin * invCellSize;
        const float3 maxScaled = aabbMax * invCellSize;
        const bool finiteBounds = all(isfinite(minScaled)) && all(isfinite(maxScaled)) && all(abs(minScaled) < 1.0e6f) && all(abs(maxScaled) < 1.0e6f);

        bool isLarge = !finiteBounds;
        bool maybeDup = false;
        int3 cMin = int3(0, 0, 0);
        int3 span = int3(0, 0, 0);
        if(!isLarge){
            cMin = int3(floor(minScaled));
            const int3 cMax = int3(floor(maxScaled));
            span = cMax - cMin + int3(1, 1, 1);
            isLarge = (
                span.x <= 0
                || span.y <= 0
                || span.z <= 0
                || span.x > int(NWB_PHYSICS_GPU_GRID_MAX_CELLS_PER_SHAPE)
                || span.y > int(NWB_PHYSICS_GPU_GRID_MAX_CELLS_PER_SHAPE)
                || span.z > int(NWB_PHYSICS_GPU_GRID_MAX_CELLS_PER_SHAPE)
            );
            if(!isLarge){
                const uint cellCount = uint(span.x) * uint(span.y) * uint(span.z);
                if(cellCount > NWB_PHYSICS_GPU_GRID_MAX_CELLS_PER_SHAPE)
                    isLarge = true;
            }
        }

        if(!isLarge){
            uint bloom0 = 0u;
            uint bloom1 = 0u;
            for(int dz = 0; dz < span.z; ++dz){
                const int cz = cMin.z + dz;
                for(int dy = 0; dy < span.y; ++dy){
                    const int cy = cMin.y + dy;
                    for(int dx = 0; dx < span.x; ++dx){
                        const int cx = cMin.x + dx;
                        const uint bucket = nwbPhysicsHashCell(int3(cx, cy, cz));
                        const uint bitIdx = bucket & 63u;
                        if(bitIdx < 32u){
                            const uint bitMask = 1u << bitIdx;
                            if((bloom0 & bitMask) != 0u)
                                maybeDup = true;
                            bloom0 |= bitMask;
                        }
                        else{
                            const uint bitMask = 1u << (bitIdx - 32u);
                            if((bloom1 & bitMask) != 0u)
                                maybeDup = true;
                            bloom1 |= bitMask;
                        }
                        uint slotIdx = 0u;
                        InterlockedAdd(g_NwbPhysicsUintBuffers[gridSlot][bucket], 1u, slotIdx);
                        if(slotIdx < NWB_PHYSICS_GPU_GRID_BUCKET_CAPACITY){
                            g_NwbPhysicsUintBuffers[gridSlot][NWB_PHYSICS_GPU_GRID_TABLE_SIZE + bucket * NWB_PHYSICS_GPU_GRID_BUCKET_CAPACITY + slotIdx] = tid;
                        }
                        else{
                            isLarge = true;
                            break;
                        }
                    }
                    if(isLarge)
                        break;
                }
                if(isLarge)
                    break;
            }
        }

        if(isLarge){
            uint largePos = 0u;
            InterlockedAdd(g_NwbPhysicsUintBuffers[largeListSlot][0u], 1u, largePos);
            if(largePos < itemCount)
                g_NwbPhysicsUintBuffers[largeListSlot][1u + largePos] = tid;
        }

        const uint aabbFlags = (isLarge ? 1u : 0u) | (maybeDup ? 2u : 0u);
        g_NwbPhysicsFloat4Buffers[shapeAabbSlot][tid * 2u + 0u].w = asfloat(aabbFlags);
        return;
    }

    const uint awakeOffset = itemOffset;
    const uint awakeCount = itemCount;
    const uint totalShapeCount = asuint(g_NwbPhysicsPushConstants.gravitySpeculative.x);
    const uint nonCollideSlot = asuint(g_NwbPhysicsPushConstants.gravitySpeculative.y);
    const uint nonCollideMask = asuint(g_NwbPhysicsPushConstants.gravitySpeculative.z);
    const uint maxPairs = g_NwbPhysicsPushConstants.flags;
    const uint idx = awakeOffset + tid;

    if(mode == NWB_PHYSICS_BROADPHASE_MODE_GRID_EMIT_PAIRS){
        const bool active = (tid < awakeCount);
        float3 minA = float3(0.0f, 0.0f, 0.0f);
        float3 maxA = float3(0.0f, 0.0f, 0.0f);
        uint bodyA = NWB_PHYSICS_INVALID_INDEX;
        uint4 metaA = uint4(0u, 0u, 0u, 0u);
        uint4 filterA = uint4(0u, 0u, 0u, 0u);
        bool isLargeA = false;

        if(active){
            const float4 aabb0A = g_NwbPhysicsFloat4Buffers[shapeAabbSlot][idx * 2u + 0u];
            const float4 aabb1A = g_NwbPhysicsFloat4Buffers[shapeAabbSlot][idx * 2u + 1u];
            minA = aabb0A.xyz;
            maxA = aabb1A.xyz;
            bodyA = asuint(aabb1A.w);
            isLargeA = ((asuint(aabb0A.w) & 1u) != 0u);
            metaA = g_NwbPhysicsShapeProxyBuffers[shapeSlot][idx].bodyIndexTypeShape;
            filterA = g_NwbPhysicsShapeProxyBuffers[shapeSlot][idx].filterData;
        }

        const float invCellSize = g_NwbPhysicsPushConstants.deltaTime;
        if(active && !isLargeA){
            const float3 minScaledA = minA * invCellSize;
            const float3 maxScaledA = maxA * invCellSize;
            const bool finiteA = all(isfinite(minScaledA)) && all(isfinite(maxScaledA)) && all(abs(minScaledA) < 1.0e6f) && all(abs(maxScaledA) < 1.0e6f);
            if(finiteA){
                const int3 cMinA = int3(floor(minScaledA));
                const int3 cMaxA = int3(floor(maxScaledA));
                const int3 spanA = cMaxA - cMinA + int3(1, 1, 1);
                if(spanA.x > 0 && spanA.y > 0 && spanA.z > 0 && spanA.x <= 27 && spanA.y <= 27 && spanA.z <= 27){
                    for(int dz = 0; dz < spanA.z; ++dz){
                        const int cz = cMinA.z + dz;
                        for(int dy = 0; dy < spanA.y; ++dy){
                            const int cy = cMinA.y + dy;
                            for(int dx = 0; dx < spanA.x; ++dx){
                                const int cx = cMinA.x + dx;
                                const uint bucket = nwbPhysicsHashCell(int3(cx, cy, cz));
                                const uint entryBase = NWB_PHYSICS_GPU_GRID_TABLE_SIZE + bucket * NWB_PHYSICS_GPU_GRID_BUCKET_CAPACITY;
                                const uint count = min(g_NwbPhysicsUintBuffers[gridSlot][bucket], NWB_PHYSICS_GPU_GRID_BUCKET_CAPACITY);

                                for(uint k = 0u; k < count; ++k){
                                    const uint idxB = g_NwbPhysicsUintBuffers[gridSlot][entryBase + k];
                                    if(idxB >= awakeOffset && idxB <= idx)
                                        continue;

                                    const float4 aabb0B = g_NwbPhysicsFloat4Buffers[shapeAabbSlot][idxB * 2u + 0u];
                                    const float4 aabb1B = g_NwbPhysicsFloat4Buffers[shapeAabbSlot][idxB * 2u + 1u];
                                    const uint flagsB = asuint(aabb0B.w);
                                    if((flagsB & 1u) != 0u || bodyA == asuint(aabb1B.w) || any(aabb0B.xyz > maxA) || any(minA > aabb1B.xyz))
                                        continue;

                                    const int3 cMinB = int3(floor(aabb0B.xyz * invCellSize));
                                    const int3 canon = max(cMinA, cMinB);
                                    if(cx != canon.x || cy != canon.y || cz != canon.z)
                                        continue;

                                    const uint4 metaB = g_NwbPhysicsShapeProxyBuffers[shapeSlot][idxB].bodyIndexTypeShape;
                                    const uint4 filterB = g_NwbPhysicsShapeProxyBuffers[shapeSlot][idxB].filterData;
                                    if(!nwbPhysicsShouldCollideFields(metaA, filterA, metaB, filterB))
                                        continue;
                                    if(((metaA.w | metaB.w) & NWB_PHYSICS_SHAPE_FLAG_SENSOR) == 0u && nwbPhysicsIsNonCollidingBodyPair(nonCollideSlot, nonCollideMask, metaA.x, metaB.x))
                                        continue;

                                    if((flagsB & 2u) != 0u){
                                        bool seenInBucket = false;
                                        for(uint p = 0u; p < k; ++p){
                                            if(g_NwbPhysicsUintBuffers[gridSlot][entryBase + p] == idxB){
                                                seenInBucket = true;
                                                break;
                                            }
                                        }
                                        if(seenInBucket)
                                            continue;
                                    }

                                    const uint outA = idx < idxB ? idx : idxB;
                                    const uint outB = idx < idxB ? idxB : idx;
                                    const uint bodyOutA = idx < idxB ? metaA.x : metaB.x;
                                    const uint bodyOutB = idx < idxB ? metaB.x : metaA.x;
                                    nwbPhysicsEmitBroadphasePair(pairSlot, counterSlot, maxPairs, outA, outB, bodyOutA, bodyOutB, nwbPhysicsIsComplexPair(metaA, metaB));
                                }
                            }
                        }
                    }
                }
            }
        }

        const uint largeCount = min(g_NwbPhysicsUintBuffers[largeListSlot][0u], totalShapeCount);
        for(uint tileBase = 0u; tileBase < largeCount; tileBase += NWB_PHYSICS_WORKGROUP_SIZE){
            const uint listPos = tileBase + groupThreadID.x;
            if(listPos < largeCount){
                const uint lIdx = g_NwbPhysicsUintBuffers[largeListSlot][1u + listPos];
                s_LargeIdx[groupThreadID.x] = lIdx;
                s_LargeMin[groupThreadID.x] = g_NwbPhysicsFloat4Buffers[shapeAabbSlot][lIdx * 2u + 0u].xyz;
                s_LargeMax[groupThreadID.x] = g_NwbPhysicsFloat4Buffers[shapeAabbSlot][lIdx * 2u + 1u].xyz;
                s_LargeMeta[groupThreadID.x] = g_NwbPhysicsShapeProxyBuffers[shapeSlot][lIdx].bodyIndexTypeShape;
                s_LargeFilter[groupThreadID.x] = g_NwbPhysicsShapeProxyBuffers[shapeSlot][lIdx].filterData;
            }
            GroupMemoryBarrierWithGroupSync();

            if(active && !isLargeA){
                const uint tileEnd = min(NWB_PHYSICS_WORKGROUP_SIZE, largeCount - tileBase);
                for(uint j = 0u; j < tileEnd; ++j){
                    const uint otherIdx = s_LargeIdx[j];
                    if(otherIdx >= awakeOffset && otherIdx <= idx)
                        continue;
                    if(!nwbPhysicsAabbOverlaps(minA, maxA, s_LargeMin[j], s_LargeMax[j]))
                        continue;
                    const uint4 metaB = s_LargeMeta[j];
                    const uint4 filterB = s_LargeFilter[j];
                    if(!nwbPhysicsShouldCollideFields(metaA, filterA, metaB, filterB))
                        continue;
                    if(((metaA.w | metaB.w) & NWB_PHYSICS_SHAPE_FLAG_SENSOR) == 0u && nwbPhysicsIsNonCollidingBodyPair(nonCollideSlot, nonCollideMask, metaA.x, metaB.x))
                        continue;
                    const uint outA = idx < otherIdx ? idx : otherIdx;
                    const uint outB = idx < otherIdx ? otherIdx : idx;
                    const uint bodyOutA = idx < otherIdx ? metaA.x : metaB.x;
                    const uint bodyOutB = idx < otherIdx ? metaB.x : metaA.x;
                    nwbPhysicsEmitBroadphasePair(pairSlot, counterSlot, maxPairs, outA, outB, bodyOutA, bodyOutB, nwbPhysicsIsComplexPair(metaA, metaB));
                }
            }
            GroupMemoryBarrierWithGroupSync();
        }

        if(active && isLargeA){
            for(uint otherIdx = 0u; otherIdx < awakeOffset; ++otherIdx){
                const float4 aabb0B = g_NwbPhysicsFloat4Buffers[shapeAabbSlot][otherIdx * 2u + 0u];
                const float4 aabb1B = g_NwbPhysicsFloat4Buffers[shapeAabbSlot][otherIdx * 2u + 1u];
                if(bodyA == asuint(aabb1B.w) || any(aabb0B.xyz > maxA) || any(minA > aabb1B.xyz))
                    continue;
                const uint4 metaB = g_NwbPhysicsShapeProxyBuffers[shapeSlot][otherIdx].bodyIndexTypeShape;
                const uint4 filterB = g_NwbPhysicsShapeProxyBuffers[shapeSlot][otherIdx].filterData;
                if(!nwbPhysicsShouldCollideFields(metaA, filterA, metaB, filterB))
                    continue;
                if(((metaA.w | metaB.w) & NWB_PHYSICS_SHAPE_FLAG_SENSOR) == 0u && nwbPhysicsIsNonCollidingBodyPair(nonCollideSlot, nonCollideMask, metaA.x, metaB.x))
                    continue;
                nwbPhysicsEmitBroadphasePair(pairSlot, counterSlot, maxPairs, otherIdx, idx, metaB.x, metaA.x, nwbPhysicsIsComplexPair(metaA, metaB));
            }
            for(uint otherIdx = idx + 1u; otherIdx < totalShapeCount; ++otherIdx){
                const float4 aabb0B = g_NwbPhysicsFloat4Buffers[shapeAabbSlot][otherIdx * 2u + 0u];
                const float4 aabb1B = g_NwbPhysicsFloat4Buffers[shapeAabbSlot][otherIdx * 2u + 1u];
                if(bodyA == asuint(aabb1B.w) || any(aabb0B.xyz > maxA) || any(minA > aabb1B.xyz))
                    continue;
                const uint4 metaB = g_NwbPhysicsShapeProxyBuffers[shapeSlot][otherIdx].bodyIndexTypeShape;
                const uint4 filterB = g_NwbPhysicsShapeProxyBuffers[shapeSlot][otherIdx].filterData;
                if(!nwbPhysicsShouldCollideFields(metaA, filterA, metaB, filterB))
                    continue;
                if(((metaA.w | metaB.w) & NWB_PHYSICS_SHAPE_FLAG_SENSOR) == 0u && nwbPhysicsIsNonCollidingBodyPair(nonCollideSlot, nonCollideMask, metaA.x, metaB.x))
                    continue;
                nwbPhysicsEmitBroadphasePair(pairSlot, counterSlot, maxPairs, idx, otherIdx, metaA.x, metaB.x, nwbPhysicsIsComplexPair(metaA, metaB));
            }
        }
        return;
    }

    if(mode == NWB_PHYSICS_BROADPHASE_MODE_EMIT_PAIRS){
        if(tid >= awakeCount)
            return;
        const float3 minA = g_NwbPhysicsFloat4Buffers[shapeAabbSlot][idx * 2u + 0u].xyz;
        const float4 aabb1A = g_NwbPhysicsFloat4Buffers[shapeAabbSlot][idx * 2u + 1u];
        const float3 maxA = aabb1A.xyz;
        const uint bodyA = asuint(aabb1A.w);
        const uint4 metaA = g_NwbPhysicsShapeProxyBuffers[shapeSlot][idx].bodyIndexTypeShape;
        const uint4 filterA = g_NwbPhysicsShapeProxyBuffers[shapeSlot][idx].filterData;

        for(uint idxB = 0u; idxB < awakeOffset; ++idxB){
            const float4 aabb0B = g_NwbPhysicsFloat4Buffers[shapeAabbSlot][idxB * 2u + 0u];
            const float4 aabb1B = g_NwbPhysicsFloat4Buffers[shapeAabbSlot][idxB * 2u + 1u];
            if(bodyA == asuint(aabb1B.w) || any(aabb0B.xyz > maxA) || any(minA > aabb1B.xyz))
                continue;
            const uint4 metaB = g_NwbPhysicsShapeProxyBuffers[shapeSlot][idxB].bodyIndexTypeShape;
            const uint4 filterB = g_NwbPhysicsShapeProxyBuffers[shapeSlot][idxB].filterData;
            if(!nwbPhysicsShouldCollideFields(metaA, filterA, metaB, filterB))
                continue;
            if(((metaA.w | metaB.w) & NWB_PHYSICS_SHAPE_FLAG_SENSOR) == 0u && nwbPhysicsIsNonCollidingBodyPair(nonCollideSlot, nonCollideMask, metaA.x, metaB.x))
                continue;
            nwbPhysicsEmitBroadphasePair(pairSlot, counterSlot, maxPairs, idxB, idx, metaB.x, metaA.x, nwbPhysicsIsComplexPair(metaA, metaB));
        }
        for(uint idxB = idx + 1u; idxB < totalShapeCount; ++idxB){
            const float4 aabb0B = g_NwbPhysicsFloat4Buffers[shapeAabbSlot][idxB * 2u + 0u];
            const float4 aabb1B = g_NwbPhysicsFloat4Buffers[shapeAabbSlot][idxB * 2u + 1u];
            if(bodyA == asuint(aabb1B.w) || any(aabb0B.xyz > maxA) || any(minA > aabb1B.xyz))
                continue;
            const uint4 metaB = g_NwbPhysicsShapeProxyBuffers[shapeSlot][idxB].bodyIndexTypeShape;
            const uint4 filterB = g_NwbPhysicsShapeProxyBuffers[shapeSlot][idxB].filterData;
            if(!nwbPhysicsShouldCollideFields(metaA, filterA, metaB, filterB))
                continue;
            if(((metaA.w | metaB.w) & NWB_PHYSICS_SHAPE_FLAG_SENSOR) == 0u && nwbPhysicsIsNonCollidingBodyPair(nonCollideSlot, nonCollideMask, metaA.x, metaB.x))
                continue;
            nwbPhysicsEmitBroadphasePair(pairSlot, counterSlot, maxPairs, idx, idxB, metaA.x, metaB.x, nwbPhysicsIsComplexPair(metaA, metaB));
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

