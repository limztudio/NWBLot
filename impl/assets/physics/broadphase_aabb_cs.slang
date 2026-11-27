// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "broadphase_helpers.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


uint nwbPhysicsHashCell(const int3 cell, const uint tableMask){
    const uint hx = asuint(cell.x) * 73856093u;
    const uint hy = asuint(cell.y) * 19349663u;
    const uint hz = asuint(cell.z) * 83492791u;
    return (hx ^ hy ^ hz) & tableMask;
}

groupshared uint s_LargeIdx[NWB_PHYSICS_WORKGROUP_SIZE];
groupshared float3 s_LargeMin[NWB_PHYSICS_WORKGROUP_SIZE];
groupshared float3 s_LargeMax[NWB_PHYSICS_WORKGROUP_SIZE];
groupshared uint4 s_LargeMeta[NWB_PHYSICS_WORKGROUP_SIZE];
groupshared uint4 s_LargeFilter[NWB_PHYSICS_WORKGROUP_SIZE];


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
    const uint surfacePairSlot = g_NwbPhysicsPushConstants.slots0.w;
    const uint pairSlot = g_NwbPhysicsPushConstants.slots1.x;
    const uint counterSlot = g_NwbPhysicsPushConstants.slots1.y;
    const uint gridSlot = g_NwbPhysicsPushConstants.slots1.z;
    const uint largeListSlot = g_NwbPhysicsPushConstants.slots1.w;

    if(mode == NWB_PHYSICS_BROADPHASE_MODE_GRID_CLEAR){
        if(tid == 0u){
            g_NwbPhysicsUintBuffers[largeListSlot][0u] = 0u;
            g_NwbPhysicsUintBuffers[largeListSlot][1u] = 0u;
        }
        if(tid >= itemCount)
            return;
        g_NwbPhysicsUintBuffers[gridSlot][tid] = 0u;
        return;
    }

    if(mode == NWB_PHYSICS_BROADPHASE_MODE_FINALIZE_INDIRECT){
        if(tid == 0u){
            const uint maxPairs = g_NwbPhysicsPushConstants.flags;
            const uint rawPrim = g_NwbPhysicsUintBuffers[counterSlot][0u];
            const uint rawComplex = g_NwbPhysicsUintBuffers[counterSlot][1u];
            const uint rawMesh = g_NwbPhysicsUintBuffers[counterSlot][5u];
            const uint rawTerrain = g_NwbPhysicsUintBuffers[counterSlot][6u];
            const bool stackOverflow = g_NwbPhysicsUintBuffers[counterSlot][4u] != 0u;
            const bool primOverflow = (rawPrim > maxPairs) || (rawComplex > (maxPairs - rawPrim));
            const bool surfOverflow = (rawMesh > maxPairs) || (rawTerrain > (maxPairs - rawMesh));
            const bool overflow = stackOverflow || primOverflow || surfOverflow;
            const uint totalPairs = rawPrim + rawComplex + rawMesh + rawTerrain;
            const uint clampedTotal = stackOverflow ? 0u : (overflow ? (maxPairs * 2u) : totalPairs);
            const uint primGroups = (overflow || rawPrim == 0u) ? 0u : ((rawPrim + NWB_PHYSICS_WORKGROUP_SIZE - 1u) / NWB_PHYSICS_WORKGROUP_SIZE);
            const uint complexGroups = (overflow || rawComplex == 0u) ? 0u : ((rawComplex + NWB_PHYSICS_WORKGROUP_SIZE - 1u) / NWB_PHYSICS_WORKGROUP_SIZE);
            const uint meshGroups = (overflow || rawMesh == 0u) ? 0u : ((rawMesh + NWB_PHYSICS_WORKGROUP_SIZE - 1u) / NWB_PHYSICS_WORKGROUP_SIZE);
            const uint terrainGroups = (overflow || rawTerrain == 0u) ? 0u : ((rawTerrain + NWB_PHYSICS_WORKGROUP_SIZE - 1u) / NWB_PHYSICS_WORKGROUP_SIZE);
            const uint indirectSlot = g_NwbPhysicsPushConstants.slots1.z;
            g_NwbPhysicsUintBuffers[counterSlot][3u] = clampedTotal;
            g_NwbPhysicsUintBuffers[indirectSlot][0u] = primGroups;
            g_NwbPhysicsUintBuffers[indirectSlot][1u] = 1u;
            g_NwbPhysicsUintBuffers[indirectSlot][2u] = 1u;
            g_NwbPhysicsUintBuffers[indirectSlot][3u] = complexGroups;
            g_NwbPhysicsUintBuffers[indirectSlot][4u] = 1u;
            g_NwbPhysicsUintBuffers[indirectSlot][5u] = 1u;
            g_NwbPhysicsUintBuffers[indirectSlot][9u] = meshGroups;
            g_NwbPhysicsUintBuffers[indirectSlot][10u] = 1u;
            g_NwbPhysicsUintBuffers[indirectSlot][11u] = 1u;
            g_NwbPhysicsUintBuffers[indirectSlot][12u] = terrainGroups;
            g_NwbPhysicsUintBuffers[indirectSlot][13u] = 1u;
            g_NwbPhysicsUintBuffers[indirectSlot][14u] = 1u;
        }
        return;
    }

    if(mode == NWB_PHYSICS_BROADPHASE_MODE_FINALIZE_LARGE_INDIRECT){
        if(tid == 0u){
            const uint awakeLargeCount = min(g_NwbPhysicsUintBuffers[largeListSlot][1u], itemOffset);
            const uint largeGroups = (awakeLargeCount > 0u) ? ((itemCount + NWB_PHYSICS_WORKGROUP_SIZE - 1u) / NWB_PHYSICS_WORKGROUP_SIZE) : 0u;
            const uint indirectSlot = g_NwbPhysicsPushConstants.slots1.z;
            g_NwbPhysicsUintBuffers[indirectSlot][6u] = largeGroups;
            g_NwbPhysicsUintBuffers[indirectSlot][7u] = 1u;
            g_NwbPhysicsUintBuffers[indirectSlot][8u] = 1u;
        }
        return;
    }

    if(mode == NWB_PHYSICS_BROADPHASE_MODE_UPDATE_AABBS){
        if(tid >= itemCount)
            return;
        const uint shapeIdx = itemOffset + tid;
        const uint4 bodyMeta = g_NwbPhysicsShapeProxyBuffers[shapeSlot][shapeIdx].bodyIndexTypeShape;
        const float4 localCenterRadius = g_NwbPhysicsShapeProxyBuffers[shapeSlot][shapeIdx].localCenterRadius;
        const float4 localExtentOrP1 = g_NwbPhysicsShapeProxyBuffers[shapeSlot][shapeIdx].localExtentOrP1;

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
        else if(shapeType == NWB_PHYSICS_SHAPE_TYPE_CAPSULE || shapeType == NWB_PHYSICS_SHAPE_TYPE_SEGMENT){
            const float3 p0 = bodyOrigin + nwbPhysicsQuatRotate(localCenterRadius.xyz, bodyQ);
            const float3 p1 = bodyOrigin + nwbPhysicsQuatRotate(localExtentOrP1.xyz, bodyQ);
            const float effR = (shapeType == NWB_PHYSICS_SHAPE_TYPE_SEGMENT) ? max(radius, NWB_PHYSICS_LINEAR_SLOP) : radius;
            const float r = effR + margin;
            minOut = min(p0, p1) - float3(r, r, r);
            maxOut = max(p0, p1) + float3(r, r, r);
        }
        else if(shapeType == NWB_PHYSICS_SHAPE_TYPE_TAPERED_CAPSULE){
            const float4 deltaRadii = g_NwbPhysicsShapeProxyBuffers[shapeSlot][shapeIdx].localRotationOrMeta;
            const float3 p0 = bodyOrigin + nwbPhysicsQuatRotate(localCenterRadius.xyz, bodyQ);
            const float3 p1 = bodyOrigin + nwbPhysicsQuatRotate(localExtentOrP1.xyz, bodyQ);
            const float r0 = radius + deltaRadii.x + margin;
            const float r1 = radius + deltaRadii.y + margin;
            minOut = min(p0 - float3(r0, r0, r0), p1 - float3(r1, r1, r1));
            maxOut = max(p0 + float3(r0, r0, r0), p1 + float3(r1, r1, r1));
        }
        else if(shapeType == NWB_PHYSICS_SHAPE_TYPE_CYLINDER || shapeType == NWB_PHYSICS_SHAPE_TYPE_CONE){
            const float4 localRotQ = g_NwbPhysicsShapeProxyBuffers[shapeSlot][shapeIdx].localRotationOrMeta;
            const float4 worldQ = (localRotQ.w < 0.999999f)
                ? nwbPhysicsQuatNormalize(nwbPhysicsQuatMul(bodyQ, localRotQ))
                : bodyQ;
            const float3 worldCenter = bodyOrigin + nwbPhysicsQuatRotate(localCenterRadius.xyz, bodyQ);
            const float3 ax = nwbPhysicsQuatRotate(float3(0.0f, 1.0f, 0.0f), worldQ);
            const float3 radProj = sqrt(max(float3(0.0f, 0.0f, 0.0f), float3(1.0f, 1.0f, 1.0f) - ax * ax)) * localExtentOrP1.x;
            const float padR = radius + margin;
            const float3 padVec = float3(padR, padR, padR);
            if(shapeType == NWB_PHYSICS_SHAPE_TYPE_CYLINDER){
                const float3 ext = abs(ax) * localExtentOrP1.y + radProj + padVec;
                minOut = worldCenter - ext;
                maxOut = worldCenter + ext;
            }
            else{
                const float3 tipOff = ax * localExtentOrP1.y;
                minOut = worldCenter + min(tipOff, -tipOff - radProj) - padVec;
                maxOut = worldCenter + max(tipOff, -tipOff + radProj) + padVec;
            }
        }
        else if(shapeType == NWB_PHYSICS_SHAPE_TYPE_PLANE){
            const float4 planeQ = g_NwbPhysicsShapeProxyBuffers[shapeSlot][shapeIdx].localRotationOrMeta;
            const float4 worldQ = nwbPhysicsQuatNormalize(nwbPhysicsQuatMul(bodyQ, planeQ));
            const float3 worldCenter = bodyOrigin + nwbPhysicsQuatRotate(localCenterRadius.xyz, bodyQ);
            float3 wt1;
            float3 wt2;
            float3 wn;
            nwbPhysicsQuatToAxes(worldQ, wt1, wt2, wn);
            const bool isFin = (localExtentOrP1.x > 0.0f) && (localExtentOrP1.z > 0.0f);
            const float hx = isFin ? localExtentOrP1.x : NWB_PHYSICS_PLANE_HALF_EXTENT_DEFAULT;
            const float hz = isFin ? localExtentOrP1.z : NWB_PHYSICS_PLANE_HALF_EXTENT_DEFAULT;
            const float3 ext = abs(wt1) * hx + abs(wt2) * hz + abs(wn) * 0.05f + float3(margin, margin, margin);
            minOut = worldCenter - ext;
            maxOut = worldCenter + ext;
        }
        else{
            const NwbPhysicsGpuShapeProxy shapeProxy = g_NwbPhysicsShapeProxyBuffers[shapeSlot][shapeIdx];
            float3 effOrigin = bodyOrigin;
            float4 effQ = bodyQ;
            nwbPhysicsResolveShapeWorldPose(shapeProxy, bodyOrigin, bodyQ, effOrigin, effQ);
            const float4 localRotQ = ((bodyMeta.w & NWB_PHYSICS_SHAPE_FLAG_IS_BOX) != 0u)
                ? shapeProxy.localRotationOrMeta
                : float4(0.0f, 0.0f, 0.0f, 1.0f);
            const float4 worldQ = (localRotQ.w < 0.999999f)
                ? nwbPhysicsQuatNormalize(nwbPhysicsQuatMul(effQ, localRotQ))
                : effQ;
            const float3 worldCenter = effOrigin + nwbPhysicsQuatRotate(localCenterRadius.xyz, effQ);
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

        if((asuint(bodyPose.sweepDispFlags.w) & NWB_PHYSICS_SHAPE_FLAG_ENLARGE_AABB) != 0u){
            minOut += min(sweepDisp, float3(0.0f, 0.0f, 0.0f));
            maxOut += max(sweepDisp, float3(0.0f, 0.0f, 0.0f));
        }

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
        const uint gridTableSize = asuint(g_NwbPhysicsPushConstants.gravitySpeculative.w);
        const uint gridTableMask = gridTableSize - 1u;
        const uint awakeOffset = itemOffset;
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
                        const uint bucket = nwbPhysicsHashCell(int3(cx, cy, cz), gridTableMask);
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
                        if(g_NwbPhysicsUintBuffers[gridSlot][bucket] >= NWB_PHYSICS_GPU_GRID_BUCKET_CAPACITY){
                            isLarge = true;
                            break;
                        }
                        uint slotIdx = 0u;
                        InterlockedAdd(g_NwbPhysicsUintBuffers[gridSlot][bucket], 1u, slotIdx);
                        if(slotIdx < NWB_PHYSICS_GPU_GRID_BUCKET_CAPACITY){
                            g_NwbPhysicsUintBuffers[gridSlot][gridTableSize + bucket * NWB_PHYSICS_GPU_GRID_BUCKET_CAPACITY + slotIdx] = tid;
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

        const bool isAwakeLarge = isLarge && (tid >= awakeOffset);
        const uint largeOffset = WavePrefixCountBits(isLarge);
        const uint largeCount = WaveActiveCountBits(isLarge);
        const uint awakeLargeOffset = WavePrefixCountBits(isAwakeLarge);
        const uint awakeLargeCount = WaveActiveCountBits(isAwakeLarge);
        uint largeBase = 0u;
        uint awakeLargeBase = 0u;
        if(WaveIsFirstLane()){
            if(largeCount > 0u)
                InterlockedAdd(g_NwbPhysicsUintBuffers[largeListSlot][0u], largeCount, largeBase);
            if(awakeLargeCount > 0u)
                InterlockedAdd(g_NwbPhysicsUintBuffers[largeListSlot][1u], awakeLargeCount, awakeLargeBase);
        }
        largeBase = WaveReadLaneFirst(largeBase);
        awakeLargeBase = WaveReadLaneFirst(awakeLargeBase);
        if(isLarge){
            const uint largePos = largeBase + largeOffset;
            if(largePos < itemCount)
                g_NwbPhysicsUintBuffers[largeListSlot][2u + largePos] = tid;
            if(isAwakeLarge){
                const uint awakeLargePos = awakeLargeBase + awakeLargeOffset;
                if(awakeLargePos < (itemCount - awakeOffset))
                    g_NwbPhysicsUintBuffers[largeListSlot][2u + itemCount + awakeLargePos] = tid;
            }
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
        const uint gridTableSize = asuint(g_NwbPhysicsPushConstants.gravitySpeculative.w);
        const uint gridTableMask = gridTableSize - 1u;
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
                                const uint bucket = nwbPhysicsHashCell(int3(cx, cy, cz), gridTableMask);
                                const uint entryBase = gridTableSize + bucket * NWB_PHYSICS_GPU_GRID_BUCKET_CAPACITY;
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

                                    if(((metaA.w & metaB.w) & NWB_PHYSICS_SHAPE_FLAG_HAS_NONCOLLIDE_JOINT) != 0u && ((metaA.w | metaB.w) & NWB_PHYSICS_SHAPE_FLAG_SENSOR) == 0u && nwbPhysicsIsNonCollidingBodyPair(nonCollideSlot, nonCollideMask, metaA.x, metaB.x))
                                        continue;

                                    const uint outA = idx < idxB ? idx : idxB;
                                    const uint outB = idx < idxB ? idxB : idx;
                                    const uint bodyOutA = idx < idxB ? metaA.x : metaB.x;
                                    const uint bodyOutB = idx < idxB ? metaB.x : metaA.x;
                                    nwbPhysicsEmitBroadphasePair(pairSlot, surfacePairSlot, counterSlot, maxPairs, outA, outB, bodyOutA, bodyOutB, nwbPhysicsBroadphasePairBucket(metaA, metaB));
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
                const uint lIdx = g_NwbPhysicsUintBuffers[largeListSlot][2u + listPos];
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
                    if(((metaA.w & metaB.w) & NWB_PHYSICS_SHAPE_FLAG_HAS_NONCOLLIDE_JOINT) != 0u && ((metaA.w | metaB.w) & NWB_PHYSICS_SHAPE_FLAG_SENSOR) == 0u && nwbPhysicsIsNonCollidingBodyPair(nonCollideSlot, nonCollideMask, metaA.x, metaB.x))
                        continue;
                    const uint outA = idx < otherIdx ? idx : otherIdx;
                    const uint outB = idx < otherIdx ? otherIdx : idx;
                    const uint bodyOutA = idx < otherIdx ? metaA.x : metaB.x;
                    const uint bodyOutB = idx < otherIdx ? metaB.x : metaA.x;
                    nwbPhysicsEmitBroadphasePair(pairSlot, surfacePairSlot, counterSlot, maxPairs, outA, outB, bodyOutA, bodyOutB, nwbPhysicsBroadphasePairBucket(metaA, metaB));
                }
            }
            GroupMemoryBarrierWithGroupSync();
        }
        return;
    }

    if(mode == NWB_PHYSICS_BROADPHASE_MODE_GRID_EMIT_LARGE_PAIRS){
        const uint awakeLargeCount = min(g_NwbPhysicsUintBuffers[largeListSlot][1u], awakeCount);
        if(awakeLargeCount == 0u)
            return;

        const uint otherIdx = tid;
        const bool active = (otherIdx < totalShapeCount);
        float3 minB = float3(0.0f, 0.0f, 0.0f);
        float3 maxB = float3(0.0f, 0.0f, 0.0f);
        uint bodyB = NWB_PHYSICS_INVALID_INDEX;
        bool loadedB = false;
        uint4 metaB = uint4(0u, 0u, 0u, 0u);
        uint4 filterB = uint4(0u, 0u, 0u, 0u);
        if(active){
            const float4 aabb0B = g_NwbPhysicsFloat4Buffers[shapeAabbSlot][otherIdx * 2u + 0u];
            const float4 aabb1B = g_NwbPhysicsFloat4Buffers[shapeAabbSlot][otherIdx * 2u + 1u];
            minB = aabb0B.xyz;
            maxB = aabb1B.xyz;
            bodyB = asuint(aabb1B.w);
        }

        for(uint tileBase = 0u; tileBase < awakeLargeCount; tileBase += NWB_PHYSICS_WORKGROUP_SIZE){
            const uint listPos = tileBase + groupThreadID.x;
            if(listPos < awakeLargeCount){
                const uint lIdx = g_NwbPhysicsUintBuffers[largeListSlot][2u + totalShapeCount + listPos];
                s_LargeIdx[groupThreadID.x] = lIdx;
                s_LargeMin[groupThreadID.x] = g_NwbPhysicsFloat4Buffers[shapeAabbSlot][lIdx * 2u + 0u].xyz;
                s_LargeMax[groupThreadID.x] = g_NwbPhysicsFloat4Buffers[shapeAabbSlot][lIdx * 2u + 1u].xyz;
                s_LargeMeta[groupThreadID.x] = g_NwbPhysicsShapeProxyBuffers[shapeSlot][lIdx].bodyIndexTypeShape;
                s_LargeFilter[groupThreadID.x] = g_NwbPhysicsShapeProxyBuffers[shapeSlot][lIdx].filterData;
            }
            GroupMemoryBarrierWithGroupSync();

            if(active){
                const uint tileEnd = min(NWB_PHYSICS_WORKGROUP_SIZE, awakeLargeCount - tileBase);
                for(uint j = 0u; j < tileEnd; ++j){
                    const uint lIdx = s_LargeIdx[j];
                    if(otherIdx >= awakeOffset && otherIdx <= lIdx)
                        continue;
                    if(!nwbPhysicsAabbOverlaps(minB, maxB, s_LargeMin[j], s_LargeMax[j]))
                        continue;
                    const uint4 metaA = s_LargeMeta[j];
                    if(metaA.x == bodyB)
                        continue;
                    if(!loadedB){
                        metaB = g_NwbPhysicsShapeProxyBuffers[shapeSlot][otherIdx].bodyIndexTypeShape;
                        filterB = g_NwbPhysicsShapeProxyBuffers[shapeSlot][otherIdx].filterData;
                        loadedB = true;
                    }
                    const uint4 filterA = s_LargeFilter[j];
                    if(!nwbPhysicsShouldCollideFields(metaA, filterA, metaB, filterB))
                        continue;
                    if(((metaA.w & metaB.w) & NWB_PHYSICS_SHAPE_FLAG_HAS_NONCOLLIDE_JOINT) != 0u && ((metaA.w | metaB.w) & NWB_PHYSICS_SHAPE_FLAG_SENSOR) == 0u && nwbPhysicsIsNonCollidingBodyPair(nonCollideSlot, nonCollideMask, metaA.x, metaB.x))
                        continue;
                    const uint outA = lIdx < otherIdx ? lIdx : otherIdx;
                    const uint outB = lIdx < otherIdx ? otherIdx : lIdx;
                    const uint bodyOutA = lIdx < otherIdx ? metaA.x : metaB.x;
                    const uint bodyOutB = lIdx < otherIdx ? metaB.x : metaA.x;
                    nwbPhysicsEmitBroadphasePair(pairSlot, surfacePairSlot, counterSlot, maxPairs, outA, outB, bodyOutA, bodyOutB, nwbPhysicsBroadphasePairBucket(metaA, metaB));
                }
            }
            GroupMemoryBarrierWithGroupSync();
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
            if(((metaA.w & metaB.w) & NWB_PHYSICS_SHAPE_FLAG_HAS_NONCOLLIDE_JOINT) != 0u && ((metaA.w | metaB.w) & NWB_PHYSICS_SHAPE_FLAG_SENSOR) == 0u && nwbPhysicsIsNonCollidingBodyPair(nonCollideSlot, nonCollideMask, metaA.x, metaB.x))
                continue;
            nwbPhysicsEmitBroadphasePair(pairSlot, surfacePairSlot, counterSlot, maxPairs, idxB, idx, metaB.x, metaA.x, nwbPhysicsBroadphasePairBucket(metaA, metaB));
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
            if(((metaA.w & metaB.w) & NWB_PHYSICS_SHAPE_FLAG_HAS_NONCOLLIDE_JOINT) != 0u && ((metaA.w | metaB.w) & NWB_PHYSICS_SHAPE_FLAG_SENSOR) == 0u && nwbPhysicsIsNonCollidingBodyPair(nonCollideSlot, nonCollideMask, metaA.x, metaB.x))
                continue;
            nwbPhysicsEmitBroadphasePair(pairSlot, surfacePairSlot, counterSlot, maxPairs, idx, idxB, metaA.x, metaB.x, nwbPhysicsBroadphasePairBucket(metaA, metaB));
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

