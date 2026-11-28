// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "physics_common.slangi"
#include "solver_body_helpers.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_PHYSICS_RES_TOI_MAX_PAIRS_PER_BULLET 256u
#define NWB_PHYSICS_RES_TOI_MAX_CACHED_BULLET_SHAPES 16u

groupshared uint s_ToiPairCount;
groupshared uint s_ToiPairBase;
groupshared uint3 s_ToiPairs[NWB_PHYSICS_RES_TOI_MAX_PAIRS_PER_BULLET];
groupshared uint s_BulletShapeIdx[NWB_PHYSICS_RES_TOI_MAX_CACHED_BULLET_SHAPES];
groupshared uint4 s_BulletShapeMeta[NWB_PHYSICS_RES_TOI_MAX_CACHED_BULLET_SHAPES];
groupshared uint4 s_BulletShapeFilter[NWB_PHYSICS_RES_TOI_MAX_CACHED_BULLET_SHAPES];
groupshared uint s_LbvhWorkCount;
groupshared uint s_LbvhWorkNodes[NWB_PHYSICS_WORKGROUP_SIZE];
groupshared float s_ToiRedT[NWB_PHYSICS_WORKGROUP_SIZE];
groupshared uint s_ToiRedPi[NWB_PHYSICS_WORKGROUP_SIZE];
groupshared uint s_ToiRedFallback[NWB_PHYSICS_WORKGROUP_SIZE];
groupshared uint s_ToiRedFbShapeA[NWB_PHYSICS_WORKGROUP_SIZE];
groupshared uint s_ToiRedFbShapeB[NWB_PHYSICS_WORKGROUP_SIZE];

bool nwbPhysicsResToiShouldCollide(
    const uint4 metaA,
    const uint4 filterA,
    const uint bodySlotA,
    const uint4 metaB,
    const uint4 filterB,
    const uint bodySlotB,
    const uint nonCollideSlot,
    const uint nonCollideMask
){
    if(bodySlotB == bodySlotA)
        return false;
    if((metaB.w & NWB_PHYSICS_SHAPE_FLAG_SENSOR) != 0u)
        return false;
    if(!nwbPhysicsFiltersMatch(filterA.x, filterA.y, asint(filterA.z), filterB.x, filterB.y, asint(filterB.z)))
        return false;
    if(
        ((metaA.w & metaB.w) & NWB_PHYSICS_SHAPE_FLAG_HAS_NONCOLLIDE_JOINT) != 0u
        && nwbPhysicsIsNonCollidingBodyPair(nonCollideSlot, nonCollideMask, bodySlotA, bodySlotB)
    )
        return false;
    return true;
}

void nwbPhysicsResToiTestCandidateShape(
    const uint shapeIdxB,
    const float3 queryMin,
    const float3 queryMax,
    const uint bodySlotA,
    const uint shStart,
    const uint shCount,
    const uint cachedBulletShapeCount,
    const uint awakeShapeOffset,
    const uint awakeBodyOffset,
    const float speculativeDist,
    const uint shapeSlot,
    const uint shapeAabbSlot,
    const uint toiBodySimSlot,
    const uint bodyMaxRadiusSlot,
    const uint bulletShapeIdxSlot,
    const uint nonCollideSlot,
    const uint nonCollideMask
){
    if(shapeIdxB < awakeShapeOffset){
        const float3 bMin = g_NwbPhysicsFloat4Buffers[shapeAabbSlot][shapeIdxB * 2u + 0u].xyz;
        const float3 bMax = g_NwbPhysicsFloat4Buffers[shapeAabbSlot][shapeIdxB * 2u + 1u].xyz;
        if(any(bMax < queryMin) || any(queryMax < bMin))
            return;
    }

    const uint4 metaB = g_NwbPhysicsShapeProxyBuffers[shapeSlot][shapeIdxB].bodyIndexTypeShape;
    const uint bodySlotB = metaB.x;
    if(bodySlotB == bodySlotA || (metaB.w & NWB_PHYSICS_SHAPE_FLAG_SENSOR) != 0u)
        return;

    if(shapeIdxB >= awakeShapeOffset){
        if(bodySlotB >= awakeBodyOffset){
            const float3 c0B = g_NwbPhysicsBodySimBuffers[toiBodySimSlot][bodySlotB].forceLinearDamping.xyz;
            const float3 c1B = g_NwbPhysicsBodySimBuffers[toiBodySimSlot][bodySlotB].positionInvMass.xyz;
            const float extB = g_NwbPhysicsFloat4Buffers[bodyMaxRadiusSlot][bodySlotB - awakeBodyOffset].y + speculativeDist;
            const float3 bMin = min(c0B, c1B) - float3(extB, extB, extB);
            const float3 bMax = max(c0B, c1B) + float3(extB, extB, extB);
            if(any(bMax < queryMin) || any(queryMax < bMin))
                return;
        }
        else{
            const float3 bMin = g_NwbPhysicsFloat4Buffers[shapeAabbSlot][shapeIdxB * 2u + 0u].xyz;
            const float3 bMax = g_NwbPhysicsFloat4Buffers[shapeAabbSlot][shapeIdxB * 2u + 1u].xyz;
            if(any(bMax < queryMin) || any(queryMax < bMin))
                return;
        }
    }

    const uint4 filterB = g_NwbPhysicsShapeProxyBuffers[shapeSlot][shapeIdxB].filterData;
    [loop] for(uint si = 0u; si < shCount; ++si){
        uint shapeIdxA = 0u;
        uint4 metaA = uint4(0u, 0u, 0u, 0u);
        uint4 filterA = uint4(0u, 0u, 0u, 0u);
        if(si < cachedBulletShapeCount){
            shapeIdxA = s_BulletShapeIdx[si];
            metaA = s_BulletShapeMeta[si];
            filterA = s_BulletShapeFilter[si];
        }
        else{
            shapeIdxA = g_NwbPhysicsUintBuffers[bulletShapeIdxSlot][shStart + si];
            metaA = g_NwbPhysicsShapeProxyBuffers[shapeSlot][shapeIdxA].bodyIndexTypeShape;
            filterA = g_NwbPhysicsShapeProxyBuffers[shapeSlot][shapeIdxA].filterData;
        }
        if(!nwbPhysicsResToiShouldCollide(metaA, filterA, bodySlotA, metaB, filterB, bodySlotB, nonCollideSlot, nonCollideMask))
            continue;
        uint loc = 0u;
        InterlockedAdd(s_ToiPairCount, 1u, loc);
        if(loc < NWB_PHYSICS_RES_TOI_MAX_PAIRS_PER_BULLET)
            s_ToiPairs[loc] = uint3(shapeIdxA, shapeIdxB, bodySlotB);
    }
}


void nwbPhysicsWriteResidentBodyPose(
    const uint bodyPoseSlot,
    const uint poseOffset,
    const uint idx,
    const NwbPhysicsGpuBodyOut bo,
    const float3 localCenter,
    const float stepDt
){
    if(bodyPoseSlot == NWB_PHYSICS_INVALID_INDEX)
        return;
    const float3 origin = bo.positionSleepTime.xyz - nwbPhysicsQuatRotate(localCenter, bo.rotation);
    const float existingPoseFlags = g_NwbPhysicsBodyPoseBuffers[bodyPoseSlot][poseOffset + idx].sweepDispFlags.w;
    NwbPhysicsGpuBodyPose pose;
    pose.originPad = float4(origin, bo.positionSleepTime.w);
    pose.rotation = bo.rotation;
    pose.sweepDispFlags = float4(bo.linearVelocityFlags.xyz * stepDt, existingPoseFlags);
    g_NwbPhysicsBodyPoseBuffers[bodyPoseSlot][poseOffset + idx] = pose;
}

[numthreads(NWB_PHYSICS_WORKGROUP_SIZE, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID, uint3 groupID : SV_GroupID, uint3 groupThreadID : SV_GroupThreadID){
    const uint tid = dispatchThreadID.x;
    const uint mode = g_NwbPhysicsPushConstants.modeOrSecondaryCount;

    if(mode == NWB_PHYSICS_RESIDENT_MODE_FINALIZE_TOI_INDIRECT){
        if(tid == 0u){
            const uint maxToiPairs = g_NwbPhysicsPushConstants.itemCount;
            const uint toiCounterSlot = g_NwbPhysicsPushConstants.slots0.x;
            const uint toiIndirectSlot = g_NwbPhysicsPushConstants.slots0.y;
            const uint clamped = min(g_NwbPhysicsUintBuffers[toiCounterSlot][0u], maxToiPairs);
            g_NwbPhysicsUintBuffers[toiCounterSlot][0u] = clamped;
            g_NwbPhysicsUintBuffers[toiIndirectSlot][0u] = (clamped > 0u) ? ((clamped + NWB_PHYSICS_WORKGROUP_SIZE - 1u) / NWB_PHYSICS_WORKGROUP_SIZE) : 0u;
            g_NwbPhysicsUintBuffers[toiIndirectSlot][1u] = 1u;
            g_NwbPhysicsUintBuffers[toiIndirectSlot][2u] = 1u;
        }
        return;
    }

    if(mode == NWB_PHYSICS_RESIDENT_MODE_EMIT_BULLET_TOI_PAIRS){
        const uint bulletIdx = groupID.x;
        const uint localTid = groupThreadID.x;
        const uint totalShapeCount = g_NwbPhysicsPushConstants.itemCount;
        const uint awakeShapeOffset = g_NwbPhysicsPushConstants.itemOffset;
        const uint awakeBodyOffset = g_NwbPhysicsPushConstants.flags;
        const uint maxToiPairs = asuint(g_NwbPhysicsPushConstants.deltaTime);
        const float linearSlop = g_NwbPhysicsPushConstants.linearSlop;
        const uint nonCollideMask = asuint(g_NwbPhysicsPushConstants.invDeltaTime);
        const uint nonCollideSlot = asuint(g_NwbPhysicsPushConstants.maxLinearVelocity);
        const uint bodyMaxRadiusSlot = asuint(g_NwbPhysicsPushConstants.gravitySpeculative.x);
        const uint toiBodySimSlot = asuint(g_NwbPhysicsPushConstants.gravitySpeculative.y);
        const uint bulletMetaOutSlot = asuint(g_NwbPhysicsPushConstants.gravitySpeculative.z);
        const float speculativeDist = g_NwbPhysicsPushConstants.gravitySpeculative.w;
        const uint shapeSlot = g_NwbPhysicsPushConstants.slots0.x;
        const uint shapeAabbSlot = g_NwbPhysicsPushConstants.slots0.y;
        const uint lbvhNodeSlot = g_NwbPhysicsPushConstants.slots0.z;
        const uint toiPairSlot = g_NwbPhysicsPushConstants.slots0.w;
        const uint bulletInputSlot = g_NwbPhysicsPushConstants.slots1.x;
        const uint bulletShapeIdxSlot = g_NwbPhysicsPushConstants.slots1.y;
        const uint toiCounterSlot = g_NwbPhysicsPushConstants.slots1.z;

        const uint resSlotA = g_NwbPhysicsUintBuffers[bulletInputSlot][bulletIdx * 4u + 0u];
        const uint shStart = g_NwbPhysicsUintBuffers[bulletInputSlot][bulletIdx * 4u + 1u];
        const uint shCount = g_NwbPhysicsUintBuffers[bulletInputSlot][bulletIdx * 4u + 2u];
        const uint firstShapeIdA = g_NwbPhysicsUintBuffers[bulletInputSlot][bulletIdx * 4u + 3u];
        const uint bodySlotA = awakeBodyOffset + resSlotA;

        if(localTid == 0u){
            s_ToiPairCount = 0u;
            s_ToiPairBase = 0u;
            s_LbvhWorkCount = 0u;
        }
        const uint cachedBulletShapeCount = min(shCount, NWB_PHYSICS_RES_TOI_MAX_CACHED_BULLET_SHAPES);
        if(localTid < cachedBulletShapeCount){
            const uint shapeIdxA = g_NwbPhysicsUintBuffers[bulletShapeIdxSlot][shStart + localTid];
            s_BulletShapeIdx[localTid] = shapeIdxA;
            s_BulletShapeMeta[localTid] = g_NwbPhysicsShapeProxyBuffers[shapeSlot][shapeIdxA].bodyIndexTypeShape;
            s_BulletShapeFilter[localTid] = g_NwbPhysicsShapeProxyBuffers[shapeSlot][shapeIdxA].filterData;
        }
        GroupMemoryBarrierWithGroupSync();

        if(shCount > 0u){
            const NwbPhysicsGpuBodySim simA = g_NwbPhysicsBodySimBuffers[toiBodySimSlot][bodySlotA];
            const float3 c0A = simA.forceLinearDamping.xyz;
            const float3 c1A = simA.positionInvMass.xyz;
            const float4 q0A = simA.torqueAngularDamping;
            const float4 q1A = simA.rotation;
            const float3 dispA = c1A - c0A;
            float4 qDeltaA = nwbPhysicsQuatMul(q1A, float4(-q0A.xyz, q0A.w));
            if(qDeltaA.w < 0.0f)
                qDeltaA = -qDeltaA;
            const float sinHalfA = length(qDeltaA.xyz);
            const float angDispA = 2.0f * atan2(sinHalfA, clamp(qDeltaA.w, -1.0f, 1.0f)) * simA.forceLinearDamping.w;
            const bool moved = (dot(dispA, dispA) > linearSlop * linearSlop) || (angDispA > linearSlop);
            if(moved){
                const float padA = g_NwbPhysicsFloat4Buffers[bodyMaxRadiusSlot][resSlotA].y + speculativeDist;
                const float3 queryMin = min(c0A, c1A) - float3(padA, padA, padA);
                const float3 queryMax = max(c0A, c1A) + float3(padA, padA, padA);
                const bool useStaticLbvh = (lbvhNodeSlot != NWB_PHYSICS_INVALID_INDEX) && (totalShapeCount > 256u) && (awakeShapeOffset > 0u);
                const uint scanStart = useStaticLbvh ? awakeShapeOffset : 0u;
                [loop] for(uint sB = scanStart + localTid; sB < totalShapeCount; sB += NWB_PHYSICS_WORKGROUP_SIZE){
                    nwbPhysicsResToiTestCandidateShape(
                        sB,
                        queryMin,
                        queryMax,
                        bodySlotA,
                        shStart,
                        shCount,
                        cachedBulletShapeCount,
                        awakeShapeOffset,
                        awakeBodyOffset,
                        speculativeDist,
                        shapeSlot,
                        shapeAabbSlot,
                        toiBodySimSlot,
                        bodyMaxRadiusSlot,
                        bulletShapeIdxSlot,
                        nonCollideSlot,
                        nonCollideMask
                    );
                }
                if(useStaticLbvh){
                    const uint leafStart = totalShapeCount - 1u;
                    if(localTid == 0u){
                        const NwbPhysicsGpuLbvhNode root = g_NwbPhysicsLbvhNodeReadBuffers[lbvhNodeSlot][0u];
                        if(all(root.minLeft.xyz <= queryMax) && all(queryMin <= root.maxRight.xyz)){
                            uint wl[NWB_PHYSICS_WORKGROUP_SIZE];
                            uint head = 0u;
                            uint tail = 0u;
                            wl[tail++] = 0u;
                            while(head < tail && (tail - head) < 32u){
                                const uint cur = wl[head & (NWB_PHYSICS_WORKGROUP_SIZE - 1u)];
                                if(cur >= leafStart)
                                    break;
                                ++head;
                                const NwbPhysicsGpuLbvhNode node = g_NwbPhysicsLbvhNodeReadBuffers[lbvhNodeSlot][cur];
                                const uint childIndices[2] = { asuint(node.minLeft.w), asuint(node.maxRight.w) };
                                [unroll]
                                for(uint ci = 0u; ci < 2u; ++ci){
                                    const uint cIdx = childIndices[ci];
                                    const NwbPhysicsGpuLbvhNode cNode = g_NwbPhysicsLbvhNodeReadBuffers[lbvhNodeSlot][cIdx];
                                    if(!all(cNode.minLeft.xyz <= queryMax) || !all(queryMin <= cNode.maxRight.xyz))
                                        continue;
                                    if(cIdx >= leafStart){
                                        const uint sB = asuint(cNode.minLeft.w);
                                        if(sB < awakeShapeOffset){
                                            nwbPhysicsResToiTestCandidateShape(
                                                sB,
                                                queryMin,
                                                queryMax,
                                                bodySlotA,
                                                shStart,
                                                shCount,
                                                cachedBulletShapeCount,
                                                awakeShapeOffset,
                                                awakeBodyOffset,
                                                speculativeDist,
                                                shapeSlot,
                                                shapeAabbSlot,
                                                toiBodySimSlot,
                                                bodyMaxRadiusSlot,
                                                bulletShapeIdxSlot,
                                                nonCollideSlot,
                                                nonCollideMask
                                            );
                                        }
                                    }
                                    else{
                                        wl[tail & (NWB_PHYSICS_WORKGROUP_SIZE - 1u)] = cIdx;
                                        ++tail;
                                    }
                                }
                            }
                            uint wc = 0u;
                            while(head < tail && wc < NWB_PHYSICS_WORKGROUP_SIZE){
                                s_LbvhWorkNodes[wc++] = wl[head & (NWB_PHYSICS_WORKGROUP_SIZE - 1u)];
                                ++head;
                            }
                            s_LbvhWorkCount = wc;
                        }
                    }
                    GroupMemoryBarrierWithGroupSync();
                    const uint wc = s_LbvhWorkCount;
                    [loop] for(uint wi = localTid; wi < wc; wi += NWB_PHYSICS_WORKGROUP_SIZE){
                        uint stack[64];
                        stack[0] = s_LbvhWorkNodes[wi];
                        uint sp = 1u;
                        while(sp > 0u){
                            const uint nodeIdx = stack[--sp];
                            const NwbPhysicsGpuLbvhNode node = g_NwbPhysicsLbvhNodeReadBuffers[lbvhNodeSlot][nodeIdx];
                            const uint childIndices[2] = { asuint(node.minLeft.w), asuint(node.maxRight.w) };
                            [unroll]
                            for(uint ci = 0u; ci < 2u; ++ci){
                                const uint cIdx = childIndices[ci];
                                const NwbPhysicsGpuLbvhNode cNode = g_NwbPhysicsLbvhNodeReadBuffers[lbvhNodeSlot][cIdx];
                                if(!all(cNode.minLeft.xyz <= queryMax) || !all(queryMin <= cNode.maxRight.xyz))
                                    continue;
                                if(cIdx >= leafStart){
                                    const uint sB = asuint(cNode.minLeft.w);
                                    if(sB < awakeShapeOffset){
                                        nwbPhysicsResToiTestCandidateShape(
                                            sB,
                                            queryMin,
                                            queryMax,
                                            bodySlotA,
                                            shStart,
                                            shCount,
                                            cachedBulletShapeCount,
                                            awakeShapeOffset,
                                            awakeBodyOffset,
                                            speculativeDist,
                                            shapeSlot,
                                            shapeAabbSlot,
                                            toiBodySimSlot,
                                            bodyMaxRadiusSlot,
                                            bulletShapeIdxSlot,
                                            nonCollideSlot,
                                            nonCollideMask
                                        );
                                    }
                                }
                                else if(sp < 64u){
                                    stack[sp++] = cIdx;
                                }
                            }
                        }
                    }
                }
            }
        }
        GroupMemoryBarrierWithGroupSync();

        if(localTid == 0u){
            const uint foundCnt = min(s_ToiPairCount, NWB_PHYSICS_RES_TOI_MAX_PAIRS_PER_BULLET);
            uint baseIdx = 0u;
            uint emitCnt = 0u;
            if(foundCnt > 0u){
                InterlockedAdd(g_NwbPhysicsUintBuffers[toiCounterSlot][0u], foundCnt, baseIdx);
                if(baseIdx < maxToiPairs)
                    emitCnt = min(foundCnt, maxToiPairs - baseIdx);
            }
            s_ToiPairBase = baseIdx;
            s_ToiPairCount = emitCnt;
            g_NwbPhysicsUintBuffers[bulletMetaOutSlot][bulletIdx * 4u + 0u] = resSlotA;
            g_NwbPhysicsUintBuffers[bulletMetaOutSlot][bulletIdx * 4u + 1u] = baseIdx;
            g_NwbPhysicsUintBuffers[bulletMetaOutSlot][bulletIdx * 4u + 2u] = emitCnt;
            g_NwbPhysicsUintBuffers[bulletMetaOutSlot][bulletIdx * 4u + 3u] = firstShapeIdA;
        }
        GroupMemoryBarrierWithGroupSync();

        const uint emitCnt = s_ToiPairCount;
        const uint dstBase = s_ToiPairBase;
        [loop] for(uint p = localTid; p < emitCnt; p += NWB_PHYSICS_WORKGROUP_SIZE){
            const uint3 tp = s_ToiPairs[p];
            g_NwbPhysicsBroadphasePairBuffers[toiPairSlot][dstBase + p].indices = uint4(tp.x, tp.y, bodySlotA, tp.z);
        }
        return;
    }

    if(mode == NWB_PHYSICS_RESIDENT_MODE_APPLY_BULLET_TOI){
        if(groupID.x != 0u)
            return;

        const uint lane = groupThreadID.x;
        const uint bulletCount = g_NwbPhysicsPushConstants.itemCount;
        const uint bulletBaseOffset = g_NwbPhysicsPushConstants.itemOffset;
        const uint bodySimSlot = g_NwbPhysicsPushConstants.slots0.x;
        const uint bodyStateSlot = g_NwbPhysicsPushConstants.slots0.y;
        const uint bodyOutSlot = g_NwbPhysicsPushConstants.slots0.z;
        const uint toiManifoldSlot = g_NwbPhysicsPushConstants.slots0.w;
        const uint bulletMetaSlot = g_NwbPhysicsPushConstants.slots1.x;
        const uint shapeMatSlot = g_NwbPhysicsPushConstants.slots1.y;
        const uint toiShapeSlot = g_NwbPhysicsPushConstants.slots1.z;
        const uint prepCounterSlot = g_NwbPhysicsPushConstants.slots1.w;
        const uint eventRingSlot = asuint(g_NwbPhysicsPushConstants.gravitySpeculative.x);
        const uint maxEvents = asuint(g_NwbPhysicsPushConstants.gravitySpeculative.y);
        const float hitThresh = g_NwbPhysicsPushConstants.gravitySpeculative.z;
        const float dt = g_NwbPhysicsPushConstants.deltaTime;
        const uint resBodyCountLimit = g_NwbPhysicsPushConstants.flags;

        [loop] for(uint bIdx = 0u; bIdx < bulletCount; ++bIdx){
            const uint metaIdx = bulletBaseOffset + bIdx;
            const uint resSlot = g_NwbPhysicsUintBuffers[bulletMetaSlot][metaIdx * 4u + 0u];
            const uint pairStart = g_NwbPhysicsUintBuffers[bulletMetaSlot][metaIdx * 4u + 1u];
            const uint pairCount = g_NwbPhysicsUintBuffers[bulletMetaSlot][metaIdx * 4u + 2u];
            const uint defaultShapeA = g_NwbPhysicsUintBuffers[bulletMetaSlot][metaIdx * 4u + 3u];

            float localBestT = 1.0f;
            uint localBestPi = NWB_PHYSICS_INVALID_INDEX;
            uint localHasFb = 0u;
            uint localFbShapeA = NWB_PHYSICS_INVALID_INDEX;
            uint localFbShapeB = NWB_PHYSICS_INVALID_INDEX;
            [loop] for(uint pi = lane; pi < pairCount; pi += NWB_PHYSICS_WORKGROUP_SIZE){
                const NwbPhysicsGpuNarrowphaseManifold candM = g_NwbPhysicsNarrowphaseManifoldBuffers[toiManifoldSlot][pairStart + pi];
                const uint4 pairInfo = candM.pairInfo;
                if(pairInfo.w == 2u || (pairInfo.w == 1u && pairInfo.z == 0u)){
                    if(localHasFb == 0u){
                        localHasFb = 1u;
                        localFbShapeA = pairInfo.x;
                        localFbShapeB = pairInfo.y;
                    }
                }
                else if(pairInfo.z > 0u && (pairInfo.w == 0u || pairInfo.w == 1u)){
                    const float candT = (pairInfo.w == 1u) ? 0.0f : candM.normalPad.w;
                    const bool validT = (pairInfo.w == 1u) || (candT > 1.0e-5f);
                    if(validT && candT < localBestT){
                        localBestT = candT;
                        localBestPi = pi;
                    }
                }
            }
            s_ToiRedT[lane] = localBestT;
            s_ToiRedPi[lane] = localBestPi;
            s_ToiRedFallback[lane] = localHasFb;
            s_ToiRedFbShapeA[lane] = localFbShapeA;
            s_ToiRedFbShapeB[lane] = localFbShapeB;
            GroupMemoryBarrierWithGroupSync();

            [unroll] for(uint stride = (NWB_PHYSICS_WORKGROUP_SIZE >> 1u); stride > 0u; stride >>= 1u){
                if(lane < stride){
                    const float otherT = s_ToiRedT[lane + stride];
                    const uint otherPi = s_ToiRedPi[lane + stride];
                    if(otherPi != NWB_PHYSICS_INVALID_INDEX && (s_ToiRedPi[lane] == NWB_PHYSICS_INVALID_INDEX || otherT < s_ToiRedT[lane] || (otherT == s_ToiRedT[lane] && otherPi < s_ToiRedPi[lane]))){
                        s_ToiRedT[lane] = otherT;
                        s_ToiRedPi[lane] = otherPi;
                    }
                    if(s_ToiRedFallback[lane] == 0u && s_ToiRedFallback[lane + stride] != 0u){
                        s_ToiRedFallback[lane] = 1u;
                        s_ToiRedFbShapeA[lane] = s_ToiRedFbShapeA[lane + stride];
                        s_ToiRedFbShapeB[lane] = s_ToiRedFbShapeB[lane + stride];
                    }
                }
                GroupMemoryBarrierWithGroupSync();
            }

            if(lane == 0u){
                if(s_ToiRedFallback[0] != 0u){
                    uint fbSA = s_ToiRedFbShapeA[0];
                    uint fbSB = s_ToiRedFbShapeB[0];
                    if(toiShapeSlot != NWB_PHYSICS_INVALID_INDEX){
                        if(fbSA != NWB_PHYSICS_INVALID_INDEX)
                            fbSA = g_NwbPhysicsShapeProxyBuffers[toiShapeSlot][fbSA].bodyIndexTypeShape.z;
                        if(fbSB != NWB_PHYSICS_INVALID_INDEX)
                            fbSB = g_NwbPhysicsShapeProxyBuffers[toiShapeSlot][fbSB].bodyIndexTypeShape.z;
                    }
                    if(fbSA == NWB_PHYSICS_INVALID_INDEX)
                        fbSA = defaultShapeA;
                    nwbPhysicsEmitEvent(
                        prepCounterSlot,
                        eventRingSlot,
                        maxEvents,
                        NWB_PHYSICS_GPU_EVENT_CPU_FALLBACK,
                        fbSA,
                        fbSB,
                        resSlot,
                        float4(0.0f, 0.0f, 0.0f, 0.0f),
                        float4(0.0f, 1.0f, 0.0f, 0.0f),
                        uint4(resSlot, bIdx, fbSA, fbSB)
                    );
                }

                const uint winPi = s_ToiRedPi[0];
                if(winPi != NWB_PHYSICS_INVALID_INDEX){
                    const NwbPhysicsGpuNarrowphaseManifold winM = g_NwbPhysicsNarrowphaseManifoldBuffers[toiManifoldSlot][pairStart + winPi];
                    const float bestT = s_ToiRedT[0];
                    const float3 bestN = nwbPhysicsSafeNormal(winM.normalPad.xyz, float3(0.0f, 1.0f, 0.0f));
                    const float3 bestPoint = winM.anchorAFeature[0].xyz;
                    uint bestShapeA = winM.pairInfo.x;
                    uint bestShapeB = winM.pairInfo.y;
                    uint shapeAIdx = defaultShapeA;

                    if(toiShapeSlot != NWB_PHYSICS_INVALID_INDEX){
                        if(bestShapeA != NWB_PHYSICS_INVALID_INDEX)
                            shapeAIdx = g_NwbPhysicsShapeProxyBuffers[toiShapeSlot][bestShapeA].bodyIndexTypeShape.z;
                        if(bestShapeB != NWB_PHYSICS_INVALID_INDEX)
                            bestShapeB = g_NwbPhysicsShapeProxyBuffers[toiShapeSlot][bestShapeB].bodyIndexTypeShape.z;
                    }

                    NwbPhysicsGpuBodyOut boA = g_NwbPhysicsBodyOutBuffers[bodyOutSlot][resSlot];
                    NwbPhysicsGpuBodySim simA = g_NwbPhysicsBodySimBuffers[bodySimSlot][resSlot];

                    float restitution = 0.0f;
                    float friction = 0.6f;
                    float staticFriction = 0.6f;
                    float rollingResistance = 0.0f;
                    float tangentSpeed = 0.0f;
                    uint matFA = 0u;
                    uint matFB = 0u;
                    uint worldBodyIdA = NWB_PHYSICS_INVALID_INDEX;
                    uint worldBodyIdB = NWB_PHYSICS_INVALID_INDEX;
                    uint resSlotB = NWB_PHYSICS_INVALID_INDEX;
                    uint fricMode = 4u;
                    NwbPhysicsGpuShapeMaterial matA = (NwbPhysicsGpuShapeMaterial)0;
                    NwbPhysicsGpuShapeMaterial matB = (NwbPhysicsGpuShapeMaterial)0;
                    bool hasMatPair = false;
                    if(shapeMatSlot != NWB_PHYSICS_INVALID_INDEX && shapeAIdx != NWB_PHYSICS_INVALID_INDEX && bestShapeB != NWB_PHYSICS_INVALID_INDEX){
                        hasMatPair = true;
                        matA = g_NwbPhysicsShapeMaterialBuffers[shapeMatSlot][shapeAIdx];
                        matB = g_NwbPhysicsShapeMaterialBuffers[shapeMatSlot][bestShapeB];
                        matFA = asuint(matA.tanFlagsThreshSlot.y);
                        matFB = asuint(matB.tanFlagsThreshSlot.y);
                        fricMode = nwbPhysicsResolveCombineMode(matFA & 7u, matFB & 7u, 4u);
                        const uint restMode = nwbPhysicsResolveCombineMode((matFA >> 3u) & 7u, (matFB >> 3u) & 7u, 3u);
                        friction = max(nwbPhysicsCombineMaterialVal(max(matA.fricRestStatRoll.x, 0.0f), max(matB.fricRestStatRoll.x, 0.0f), fricMode), 0.0f);
                        staticFriction = max(nwbPhysicsCombineMaterialVal(max(matA.fricRestStatRoll.z, 0.0f), max(matB.fricRestStatRoll.z, 0.0f), fricMode), friction);
                        rollingResistance = max(matA.fricRestStatRoll.w, matB.fricRestStatRoll.w);
                        tangentSpeed = matA.tanFlagsThreshSlot.x + matB.tanFlagsThreshSlot.x;
                        restitution = max(nwbPhysicsCombineMaterialVal(matA.fricRestStatRoll.y, matB.fricRestStatRoll.y, restMode), 0.0f);
                        worldBodyIdA = asuint(matA.surfVelBodyId.w);
                        worldBodyIdB = asuint(matB.surfVelBodyId.w);
                        resSlotB = asuint(matB.tanFlagsThreshSlot.w);
                    }

                    const uint flagsA = asuint(boA.linearVelocityFlags.w);
                    float3 vA = boA.linearVelocityFlags.xyz;
                    float3 wA = boA.angularVelocityPad.xyz;
                    const float3 c1A = boA.positionSleepTime.xyz;
                    const float3 c0A = c1A - vA * dt;
                    const float4 q1RawA = boA.rotation;
                    const float4 q0A = nwbPhysicsIntegrateDeltaQuat(q1RawA, -wA, dt);
                    const float4 q1A = (dot(q0A, q1RawA) < 0.0f) ? -q1RawA : q1RawA;
                    const float3 cHitA = lerp(c0A, c1A, bestT);
                    const float4 qHitA = nwbPhysicsQuatNormalize(lerp(q0A, q1A, bestT));
                    const float invMassA = ((flagsA & NWB_PHYSICS_BODY_FLAG_DYNAMIC) != 0u && (flagsA & 0x07u) != 0x07u) ? simA.positionInvMass.w : 0.0f;
                    float4 rawIA0;
                    float2 rawIA1;
                    nwbPhysicsRotateSymMat3(qHitA, simA.invInertiaLocal0, simA.invInertiaLocal1.xy, rawIA0, rawIA1);
                    float4 maskedIA0;
                    float2 maskedIA1;
                    nwbPhysicsMaskInvInertia(rawIA0, rawIA1, flagsA, maskedIA0, maskedIA1);
                    const float4 invIA1 = float4(maskedIA1.x, maskedIA1.y, 0.0f, 0.0f);

                    bool hasBodyB = false;
                    bool hasDynB = false;
                    NwbPhysicsGpuBodyOut boB = (NwbPhysicsGpuBodyOut)0;
                    NwbPhysicsGpuBodySim simB = (NwbPhysicsGpuBodySim)0;
                    uint flagsB = 0u;
                    float3 vB = float3(0.0f, 0.0f, 0.0f);
                    float3 wB = float3(0.0f, 0.0f, 0.0f);
                    float3 cHitB = float3(0.0f, 0.0f, 0.0f);
                    float4 qHitB = float4(0.0f, 0.0f, 0.0f, 1.0f);
                    float invMassB = 0.0f;
                    float4 maskedIB0 = float4(0.0f, 0.0f, 0.0f, 0.0f);
                    float2 maskedIB1 = float2(0.0f, 0.0f);
                    float4 invIB1 = float4(0.0f, 0.0f, 0.0f, 0.0f);
                    if(resSlotB != NWB_PHYSICS_INVALID_INDEX && resSlotB < resBodyCountLimit && resSlotB != resSlot){
                        hasBodyB = true;
                        boB = g_NwbPhysicsBodyOutBuffers[bodyOutSlot][resSlotB];
                        simB = g_NwbPhysicsBodySimBuffers[bodySimSlot][resSlotB];
                        flagsB = asuint(boB.linearVelocityFlags.w);
                        vB = boB.linearVelocityFlags.xyz;
                        wB = boB.angularVelocityPad.xyz;
                        const float3 c1B = boB.positionSleepTime.xyz;
                        const float3 c0B = c1B - vB * dt;
                        const float4 q1RawB = boB.rotation;
                        const float4 q0B = nwbPhysicsIntegrateDeltaQuat(q1RawB, -wB, dt);
                        const float4 q1B = (dot(q0B, q1RawB) < 0.0f) ? -q1RawB : q1RawB;
                        cHitB = lerp(c0B, c1B, bestT);
                        qHitB = nwbPhysicsQuatNormalize(lerp(q0B, q1B, bestT));
                        if((flagsB & NWB_PHYSICS_BODY_FLAG_DYNAMIC) != 0u){
                            hasDynB = true;
                            invMassB = ((flagsB & 0x07u) != 0x07u) ? simB.positionInvMass.w : 0.0f;
                            float4 rawIB0;
                            float2 rawIB1;
                            nwbPhysicsRotateSymMat3(qHitB, simB.invInertiaLocal0, simB.invInertiaLocal1.xy, rawIB0, rawIB1);
                            nwbPhysicsMaskInvInertia(rawIB0, rawIB1, flagsB, maskedIB0, maskedIB1);
                            invIB1 = float4(maskedIB1.x, maskedIB1.y, 0.0f, 0.0f);
                        }
                    }

                    const float3 rA = bestPoint - cHitA;
                    const float3 rB = hasBodyB ? (bestPoint - cHitB) : float3(0.0f, 0.0f, 0.0f);
                    const float3 velPtA = vA + cross(wA, rA);
                    const float3 velPtB = hasBodyB ? (vB + cross(wB, rB)) : float3(0.0f, 0.0f, 0.0f);
                    const float relVelN = dot(velPtA - velPtB, bestN);
                    float jn = 0.0f;
                    if(relVelN > 0.0f){
                        const float3 rnA = cross(rA, bestN);
                        const float3 iRnA = nwbPhysicsMulSymmetric3(maskedIA0, invIA1, rnA);
                        const float3 rnB = cross(rB, bestN);
                        const float3 iRnB = hasDynB ? nwbPhysicsMulSymmetric3(maskedIB0, invIB1, rnB) : float3(0.0f, 0.0f, 0.0f);
                        const float kN = invMassA + (hasDynB ? invMassB : 0.0f) + dot(rnA, iRnA) + dot(rnB, iRnB);
                        if(kN > 1.0e-12f)
                            jn = (1.0f + restitution) * relVelN / kN;
                        if(jn > 0.0f){
                            vA = nwbPhysicsApplyLinearLocks(vA - (invMassA * jn) * bestN, flagsA);
                            wA = nwbPhysicsApplyAngularLocks(wA - iRnA * jn, flagsA);
                            if(hasDynB){
                                vB = nwbPhysicsApplyLinearLocks(vB + (invMassB * jn) * bestN, flagsB);
                                wB = nwbPhysicsApplyAngularLocks(wB + iRnB * jn, flagsB);
                            }
                            if(friction > 0.0f || staticFriction > 0.0f){
                                float3 t1;
                                float3 t2;
                                nwbPhysicsComputeTangentBasis(bestN, t1, t2);
                                float auxScale1 = 1.0f;
                                float auxScale2 = 1.0f;
                                float3 svTotal = float3(0.0f, 0.0f, 0.0f);
                                if(hasMatPair){
                                    const bool hasAnisoA = (matFA & NWB_PHYSICS_MAT_FLAG_HAS_ANISO_FRICTION) != 0u;
                                    const bool hasAnisoB = (matFB & NWB_PHYSICS_MAT_FLAG_HAS_ANISO_FRICTION) != 0u;
                                    if(hasAnisoA || hasAnisoB){
                                        const float3 afA = hasAnisoA ? max(float3(matA.fricDirAniso0.w, matA.aniso1RestPad.x, matA.aniso1RestPad.y), float3(0.0f, 0.0f, 0.0f)) : float3(1.0f, 1.0f, 1.0f);
                                        const float3 afB = hasAnisoB ? max(float3(matB.fricDirAniso0.w, matB.aniso1RestPad.x, matB.aniso1RestPad.y), float3(0.0f, 0.0f, 0.0f)) : float3(1.0f, 1.0f, 1.0f);
                                        const float ax = nwbPhysicsCombineMaterialVal(afA.x, afB.x, fricMode);
                                        const float ay = nwbPhysicsCombineMaterialVal(afA.y, afB.y, fricMode);
                                        const float az = nwbPhysicsCombineMaterialVal(afA.z, afB.z, fricMode);
                                        float3 fDir = float3(1.0f, 0.0f, 0.0f);
                                        if(hasAnisoA && hasAnisoB){
                                            const float3 dA = nwbPhysicsSafeNormal(((matFA & NWB_PHYSICS_MAT_FLAG_WORLD_SPACE_SURFACE) != 0u) ? matA.fricDirAniso0.xyz : nwbPhysicsQuatRotate(matA.fricDirAniso0.xyz, qHitA), fDir);
                                            float3 dB = nwbPhysicsSafeNormal(((matFB & NWB_PHYSICS_MAT_FLAG_WORLD_SPACE_SURFACE) != 0u) ? matB.fricDirAniso0.xyz : nwbPhysicsQuatRotate(matB.fricDirAniso0.xyz, qHitB), fDir);
                                            if(dot(dA, dB) < 0.0f)
                                                dB = -dB;
                                            float fWA = abs(afA.x - afA.y) + abs(afA.x - afA.z);
                                            float fWB = abs(afB.x - afB.y) + abs(afB.x - afB.z);
                                            if(fWA + fWB <= 1.0e-6f){
                                                fWA = 0.5f;
                                                fWB = 0.5f;
                                            }
                                            fDir = nwbPhysicsSafeNormal(dA * fWA + dB * fWB, dA);
                                        }
                                        else if(hasAnisoA){
                                            fDir = nwbPhysicsSafeNormal(((matFA & NWB_PHYSICS_MAT_FLAG_WORLD_SPACE_SURFACE) != 0u) ? matA.fricDirAniso0.xyz : nwbPhysicsQuatRotate(matA.fricDirAniso0.xyz, qHitA), fDir);
                                        }
                                        else{
                                            fDir = nwbPhysicsSafeNormal(((matFB & NWB_PHYSICS_MAT_FLAG_WORLD_SPACE_SURFACE) != 0u) ? matB.fricDirAniso0.xyz : nwbPhysicsQuatRotate(matB.fricDirAniso0.xyz, qHitB), fDir);
                                        }
                                        const float3 proj = fDir - bestN * dot(fDir, bestN);
                                        const bool hasTangentProj = dot(proj, proj) > 1.0e-10f;
                                        if(hasTangentProj){
                                            t1 = nwbPhysicsSafeNormal(proj, t1);
                                            t2 = nwbPhysicsSafeNormal(cross(bestN, t1), t2);
                                        }
                                        if(abs(az - 1.0f) <= 1.0e-6f && hasTangentProj){
                                            auxScale1 = ax;
                                            auxScale2 = ay;
                                        }
                                        else{
                                            const float3 e0 = nwbPhysicsSafeNormal(fDir, float3(1.0f, 0.0f, 0.0f));
                                            const float3 refUp = (abs(e0.y) > 0.99f) ? float3(0.0f, 0.0f, 1.0f) : float3(0.0f, 1.0f, 0.0f);
                                            const float3 e2 = nwbPhysicsSafeNormal(cross(e0, refUp), t2);
                                            const float3 e1 = nwbPhysicsSafeNormal(cross(e2, e0), t1);
                                            const float c10 = dot(t1, e0);
                                            const float c11 = dot(t1, e1);
                                            const float c12 = dot(t1, e2);
                                            const float c20 = dot(t2, e0);
                                            const float c21 = dot(t2, e1);
                                            const float c22 = dot(t2, e2);
                                            auxScale1 = sqrt(ax * ax * c10 * c10 + ay * ay * c11 * c11 + az * az * c12 * c12);
                                            auxScale2 = sqrt(ax * ax * c20 * c20 + ay * ay * c21 * c21 + az * az * c22 * c22);
                                        }
                                    }
                                    if((matFA & NWB_PHYSICS_MAT_FLAG_HAS_SURFACE_VELOCITY) != 0u)
                                        svTotal += ((matFA & NWB_PHYSICS_MAT_FLAG_WORLD_SPACE_SURFACE) != 0u) ? matA.surfVelBodyId.xyz : nwbPhysicsQuatRotate(matA.surfVelBodyId.xyz, qHitA);
                                    if((matFB & NWB_PHYSICS_MAT_FLAG_HAS_SURFACE_VELOCITY) != 0u)
                                        svTotal -= ((matFB & NWB_PHYSICS_MAT_FLAG_WORLD_SPACE_SURFACE) != 0u) ? matB.surfVelBodyId.xyz : nwbPhysicsQuatRotate(matB.surfVelBodyId.xyz, qHitB);
                                }
                                const float tanVel1 = tangentSpeed + dot(svTotal, t1);
                                const float tanVel2 = dot(svTotal, t2);
                                const float3 vRelPost = (hasBodyB ? (vB + cross(wB, rB)) : float3(0.0f, 0.0f, 0.0f)) - (vA + cross(wA, rA));
                                const float vt1 = dot(vRelPost, t1) - tanVel1;
                                const float vt2 = dot(vRelPost, t2) - tanVel2;
                                const float3 rt1A = cross(rA, t1);
                                const float3 iRt1A = nwbPhysicsMulSymmetric3(maskedIA0, invIA1, rt1A);
                                const float3 rt2A = cross(rA, t2);
                                const float3 iRt2A = nwbPhysicsMulSymmetric3(maskedIA0, invIA1, rt2A);
                                const float3 rt1B = cross(rB, t1);
                                const float3 iRt1B = hasDynB ? nwbPhysicsMulSymmetric3(maskedIB0, invIB1, rt1B) : float3(0.0f, 0.0f, 0.0f);
                                const float3 rt2B = cross(rB, t2);
                                const float3 iRt2B = hasDynB ? nwbPhysicsMulSymmetric3(maskedIB0, invIB1, rt2B) : float3(0.0f, 0.0f, 0.0f);
                                const float invMassSum = invMassA + (hasDynB ? invMassB : 0.0f);
                                const float k11 = invMassSum + dot(rt1A, iRt1A) + dot(rt1B, iRt1B);
                                const float k22 = invMassSum + dot(rt2A, iRt2A) + dot(rt2B, iRt2B);
                                const float k12 = dot(rt1A, iRt2A) + dot(rt1B, iRt2B);
                                const float detK = k11 * k22 - k12 * k12;
                                float m00 = 0.0f;
                                float m01 = 0.0f;
                                float m11 = 0.0f;
                                if(detK > 1.0e-12f){
                                    const float invDetK = 1.0f / detK;
                                    m00 = k22 * invDetK;
                                    m01 = -k12 * invDetK;
                                    m11 = k11 * invDetK;
                                }
                                else{
                                    m00 = (k11 > 1.0e-12f) ? (1.0f / k11) : 0.0f;
                                    m11 = (k22 > 1.0e-12f) ? (1.0f / k22) : 0.0f;
                                }
                                float2 newF = float2(m00 * (-vt1) + m01 * (-vt2), m01 * (-vt1) + m11 * (-vt2));
                                const float sx = max(auxScale1, 0.0f);
                                const float sy = max(auxScale2, 0.0f);
                                float nx = 0.0f;
                                float ny = 0.0f;
                                if(sx > 1.0e-6f)
                                    nx = newF.x / sx;
                                else
                                    newF.x = 0.0f;
                                if(sy > 1.0e-6f)
                                    ny = newF.y / sy;
                                else
                                    newF.y = 0.0f;
                                const float normSq = nx * nx + ny * ny;
                                const float maxDyn = max(friction, 0.0f) * jn;
                                const float maxStat = max(max(staticFriction, friction), 0.0f) * jn;
                                if(maxStat <= 0.0f){
                                    newF = float2(0.0f, 0.0f);
                                }
                                else if(normSq > maxStat * maxStat && normSq > 1.0e-18f){
                                    const float invLen = maxDyn * rsqrt(normSq);
                                    newF.x = nx * invLen * sx;
                                    newF.y = ny * invLen * sy;
                                }
                                if(newF.x != 0.0f || newF.y != 0.0f){
                                    const float3 P = newF.x * t1 + newF.y * t2;
                                    const float3 torquePA = cross(rA, P);
                                    vA = nwbPhysicsApplyLinearLocks(vA - invMassA * P, flagsA);
                                    wA = nwbPhysicsApplyAngularLocks(wA - nwbPhysicsMulSymmetric3(maskedIA0, invIA1, torquePA), flagsA);
                                    if(hasDynB){
                                        const float3 torquePB = cross(rB, P);
                                        vB = nwbPhysicsApplyLinearLocks(vB + invMassB * P, flagsB);
                                        wB = nwbPhysicsApplyAngularLocks(wB + nwbPhysicsMulSymmetric3(maskedIB0, invIB1, torquePB), flagsB);
                                    }
                                }
                            }
                            if(rollingResistance > 0.0f){
                                const float4 sumI0 = maskedIA0 + (hasDynB ? maskedIB0 : float4(0.0f, 0.0f, 0.0f, 0.0f));
                                const float2 sumI1 = maskedIA1 + (hasDynB ? maskedIB1 : float2(0.0f, 0.0f));
                                float4 rollM0 = float4(0.0f, 0.0f, 0.0f, 0.0f);
                                float2 rollM1 = float2(0.0f, 0.0f);
                                nwbPhysicsInvertSymmetric3(sumI0, sumI1, rollM0, rollM1);
                                const float3 dw = (hasBodyB ? wB : float3(0.0f, 0.0f, 0.0f)) - wA;
                                float3 newR = -nwbPhysicsMulSymmetric3(rollM0, rollM1, dw);
                                const float maxR = rollingResistance * jn;
                                const float rLenSq = dot(newR, newR);
                                if(rLenSq > maxR * maxR && rLenSq > 1.0e-18f)
                                    newR *= maxR * rsqrt(rLenSq);
                                if(any(newR != float3(0.0f, 0.0f, 0.0f))){
                                    wA = nwbPhysicsApplyAngularLocks(wA - nwbPhysicsMulSymmetric3(maskedIA0, invIA1, newR), flagsA);
                                    if(hasDynB)
                                        wB = nwbPhysicsApplyAngularLocks(wB + nwbPhysicsMulSymmetric3(maskedIB0, invIB1, newR), flagsB);
                                }
                            }
                        }
                    }

                    const float remDt = max((1.0f - bestT) * dt, 0.0f);
                    const float3 cNewA = cHitA + vA * remDt;
                    const float4 qNewA = nwbPhysicsIntegrateDeltaQuat(qHitA, wA, remDt);
                    float4 newWIA0;
                    float2 newWIA1;
                    nwbPhysicsRotateSymMat3(qNewA, simA.invInertiaLocal0, simA.invInertiaLocal1.xy, newWIA0, newWIA1);
                    boA.positionSleepTime = float4(cNewA, 0.0f);
                    boA.rotation = qNewA;
                    boA.linearVelocityFlags.xyz = vA;
                    boA.angularVelocityPad.xyz = wA;
                    g_NwbPhysicsBodyOutBuffers[bodyOutSlot][resSlot] = boA;
                    g_NwbPhysicsBodySimBuffers[bodySimSlot][resSlot].positionInvMass.xyz = cNewA;
                    g_NwbPhysicsBodySimBuffers[bodySimSlot][resSlot].rotation = qNewA;
                    g_NwbPhysicsBodySimBuffers[bodySimSlot][resSlot].invInertiaWorld0 = newWIA0;
                    g_NwbPhysicsBodySimBuffers[bodySimSlot][resSlot].invInertiaWorld1 = float4(newWIA1.x, newWIA1.y, simA.invInertiaWorld1.z, 0.0f);
                    g_NwbPhysicsBodyStateBuffers[bodyStateSlot][resSlot].linearVelocityPad.xyz = vA;
                    g_NwbPhysicsBodyStateBuffers[bodyStateSlot][resSlot].angularVelocityPad.xyz = wA;

                    if(hasDynB){
                        const bool wakeB = (flagsB & NWB_PHYSICS_BODY_FLAG_SLEEPING) != 0u;
                        const uint newFlagsB = flagsB & ~NWB_PHYSICS_BODY_FLAG_SLEEPING;
                        const float3 cNewB = cHitB + vB * remDt;
                        const float4 qNewB = nwbPhysicsIntegrateDeltaQuat(qHitB, wB, remDt);
                        float4 newWIB0;
                        float2 newWIB1;
                        nwbPhysicsRotateSymMat3(qNewB, simB.invInertiaLocal0, simB.invInertiaLocal1.xy, newWIB0, newWIB1);
                        boB.positionSleepTime = float4(cNewB, 0.0f);
                        boB.rotation = qNewB;
                        boB.linearVelocityFlags = float4(vB, asfloat(newFlagsB));
                        boB.angularVelocityPad.xyz = wB;
                        g_NwbPhysicsBodyOutBuffers[bodyOutSlot][resSlotB] = boB;
                        g_NwbPhysicsBodySimBuffers[bodySimSlot][resSlotB].positionInvMass.xyz = cNewB;
                        g_NwbPhysicsBodySimBuffers[bodySimSlot][resSlotB].rotation = qNewB;
                        g_NwbPhysicsBodySimBuffers[bodySimSlot][resSlotB].invInertiaWorld0 = newWIB0;
                        g_NwbPhysicsBodySimBuffers[bodySimSlot][resSlotB].invInertiaWorld1 = float4(newWIB1.x, newWIB1.y, simB.invInertiaWorld1.z, 0.0f);
                        nwbPhysicsWriteBodyFlags(bodyStateSlot, resSlotB, newFlagsB);
                        g_NwbPhysicsBodyStateBuffers[bodyStateSlot][resSlotB].linearVelocityPad.xyz = vB;
                        g_NwbPhysicsBodyStateBuffers[bodyStateSlot][resSlotB].angularVelocityPad.xyz = wB;
                        if(wakeB)
                            nwbPhysicsEmitEvent(prepCounterSlot, eventRingSlot, maxEvents, NWB_PHYSICS_GPU_EVENT_BODY_WAKE, resSlotB, 0u, 0u, float4(cNewB, 0.0f), float4(0.0f, 1.0f, 0.0f, 0.0f), uint4(resSlotB, NWB_PHYSICS_INVALID_INDEX, 0u, 0u));
                    }
                    else if((matFB & NWB_PHYSICS_MAT_FLAG_CPU_SLEEPING) != 0u && worldBodyIdB != NWB_PHYSICS_INVALID_INDEX){
                        nwbPhysicsEmitEvent(prepCounterSlot, eventRingSlot, maxEvents, NWB_PHYSICS_GPU_EVENT_BODY_WAKE, worldBodyIdB, worldBodyIdA, NWB_PHYSICS_EVENT_FLAG_CPU_SLEEP_WAKE, float4(bestPoint, 0.0f), float4(-bestN, 0.0f), uint4(worldBodyIdB, worldBodyIdA, resSlot, 0u));
                    }

                    if((((matFA | matFB) & NWB_PHYSICS_MAT_FLAG_ENABLE_HIT_EVENTS) != 0u) && relVelN >= hitThresh && jn > 0.0f){
                        nwbPhysicsEmitEvent(
                            prepCounterSlot,
                            eventRingSlot,
                            maxEvents,
                            NWB_PHYSICS_GPU_EVENT_CONTACT_HIT,
                            shapeAIdx,
                            bestShapeB,
                            (resSlot & 0xffffu) | ((resSlotB & 0xffffu) << 16u),
                            float4(bestPoint, jn),
                            float4(bestN, relVelN),
                            uint4(worldBodyIdA, worldBodyIdB, resSlot, resSlotB)
                        );
                    }
                }
            }
            GroupMemoryBarrierWithGroupSync();
        }
        return;
    }

    if(tid >= g_NwbPhysicsPushConstants.itemCount)
        return;

    const uint idx = g_NwbPhysicsPushConstants.itemOffset + tid;
    const uint bodySimSlot = g_NwbPhysicsPushConstants.slots0.x;
    const uint bodyStateSlot = g_NwbPhysicsPushConstants.slots0.y;
    const uint bodyOutSlot = g_NwbPhysicsPushConstants.slots0.z;
    const uint bodyPoseSlot = g_NwbPhysicsPushConstants.slots0.w;
    const uint poseOffset = g_NwbPhysicsPushConstants.slots1.x;

    if(mode == NWB_PHYSICS_RESIDENT_MODE_BUILD_TOI_BODY_SIMS){
        const uint awakeBodyOffset = g_NwbPhysicsPushConstants.itemOffset;
        const uint solverSimSlot = g_NwbPhysicsPushConstants.slots0.x;
        const uint resOutSlot = g_NwbPhysicsPushConstants.slots0.y;
        const uint colPoseSlot = g_NwbPhysicsPushConstants.slots0.z;
        const uint toiBodySimSlot = g_NwbPhysicsPushConstants.slots0.w;
        const uint bodyMaxRadiusSlot = g_NwbPhysicsPushConstants.slots1.x;
        const float dt = g_NwbPhysicsPushConstants.deltaTime;

        NwbPhysicsGpuBodySim gs;
        gs.invInertiaWorld0 = float4(0.0f, 0.0f, 0.0f, 0.0f);
        gs.invInertiaWorld1 = float4(0.0f, 0.0f, 1.0f, 0.0f);
        gs.invInertiaLocal0 = float4(0.0f, 0.0f, 0.0f, 0.0f);
        gs.invInertiaLocal1 = float4(0.0f, 0.0f, 0.0f, 0.0f);
        if(tid < awakeBodyOffset){
            const NwbPhysicsGpuBodyPose p = g_NwbPhysicsBodyPoseBuffers[colPoseSlot][tid];
            gs.positionInvMass = float4(p.originPad.xyz, 0.0f);
            gs.rotation = p.rotation;
            gs.forceLinearDamping = float4(p.originPad.xyz, 0.0f);
            gs.torqueAngularDamping = p.rotation;
            gs.localCenterMinExtent = float4(0.0f, 0.0f, 0.0f, 0.0f);
        }
        else{
            const uint resSlot = tid - awakeBodyOffset;
            const NwbPhysicsGpuBodyOut bo = g_NwbPhysicsBodyOutBuffers[resOutSlot][resSlot];
            const NwbPhysicsGpuBodySim sim = g_NwbPhysicsBodySimBuffers[solverSimSlot][resSlot];
            const float rotMaxR = g_NwbPhysicsFloat4Buffers[bodyMaxRadiusSlot][resSlot].x;
            const float3 c1 = bo.positionSleepTime.xyz;
            const float4 q1 = nwbPhysicsQuatNormalize(bo.rotation);
            const float3 v1 = bo.linearVelocityFlags.xyz;
            const float3 w1 = bo.angularVelocityPad.xyz;
            const float3 c0 = c1 - v1 * dt;
            const float4 q0 = nwbPhysicsIntegrateDeltaQuat(q1, -w1, dt);
            gs.positionInvMass = float4(c1, sim.positionInvMass.w);
            gs.rotation = q1;
            gs.forceLinearDamping = float4(c0, rotMaxR);
            gs.torqueAngularDamping = q0;
            gs.localCenterMinExtent = sim.localCenterMinExtent;
        }
        g_NwbPhysicsBodySimBuffers[toiBodySimSlot][tid] = gs;
        return;
    }

    if(mode == NWB_PHYSICS_RESIDENT_MODE_SYNC_POSES){
        if(bodyPoseSlot != NWB_PHYSICS_INVALID_INDEX){
            const NwbPhysicsGpuBodyOut boSync = g_NwbPhysicsBodyOutBuffers[bodyOutSlot][idx];
            const float3 localCenterSync = g_NwbPhysicsBodySimBuffers[bodySimSlot][idx].localCenterMinExtent.xyz;
            nwbPhysicsWriteResidentBodyPose(bodyPoseSlot, poseOffset, idx, boSync, localCenterSync, max(g_NwbPhysicsPushConstants.deltaTime, 0.0f));
        }
        return;
    }

    NwbPhysicsGpuBodyOut bo;
    NwbPhysicsGpuBodySim sim;
    if(mode == NWB_PHYSICS_RESIDENT_MODE_INTEGRATE_SUBSTEP){
        const uint subStepCount = max(g_NwbPhysicsPushConstants.flags, 1u);
        const float dt = g_NwbPhysicsPushConstants.deltaTime;
        const float h = dt / float(subStepCount);
        bo = nwbPhysicsSolveFreeBodyAndReturnOut(
            bodySimSlot,
            bodyStateSlot,
            bodyOutSlot,
            idx,
            dt,
            subStepCount,
            h,
            g_NwbPhysicsPushConstants.maxLinearVelocity,
            g_NwbPhysicsPushConstants.gravitySpeculative.xyz,
            sim
        );
    }
    else{
        bo = g_NwbPhysicsBodyOutBuffers[bodyOutSlot][idx];
        sim = g_NwbPhysicsBodySimBuffers[bodySimSlot][idx];
        const uint preFlags = asuint(bo.linearVelocityFlags.w);
        if((preFlags & NWB_PHYSICS_BODY_FLAG_SLEEPING) != 0u
            && all(bo.linearVelocityFlags.xyz == float3(0.0f, 0.0f, 0.0f))
            && all(bo.angularVelocityPad.xyz == float3(0.0f, 0.0f, 0.0f))
            && all(sim.positionInvMass.xyz == bo.positionSleepTime.xyz)
            && all(sim.rotation == bo.rotation)
            && all(sim.forceLinearDamping.xyz == float3(0.0f, 0.0f, 0.0f))
            && all(sim.torqueAngularDamping.xyz == float3(0.0f, 0.0f, 0.0f))
        ){
            const NwbPhysicsGpuBodyState prevState = g_NwbPhysicsBodyStateBuffers[bodyStateSlot][idx];
            if(asuint(prevState.linearVelocityPad.w) == preFlags
                && asuint(prevState.deltaPositionFlags.w) == preFlags
                && all(prevState.linearVelocityPad.xyz == float3(0.0f, 0.0f, 0.0f))
                && all(prevState.angularVelocityPad.xyz == float3(0.0f, 0.0f, 0.0f))
                && all(prevState.deltaPositionFlags.xyz == float3(0.0f, 0.0f, 0.0f))
                && all(prevState.deltaRotation == float4(0.0f, 0.0f, 0.0f, 1.0f))
            ){
                if(bodyPoseSlot == NWB_PHYSICS_INVALID_INDEX)
                    return;
                const NwbPhysicsGpuBodyPose prevPose = g_NwbPhysicsBodyPoseBuffers[bodyPoseSlot][poseOffset + idx];
                if(all(prevPose.rotation == bo.rotation)
                    && prevPose.originPad.w == sim.invInertiaWorld1.w
                    && all(prevPose.sweepDispFlags.xyz == float3(0.0f, 0.0f, 0.0f))
                ){
                    return;
                }
            }
        }
    }

    uint bodyFlags = asuint(bo.linearVelocityFlags.w);
    const uint bodyMaskWakeSlot = asuint(g_NwbPhysicsPushConstants.gravitySpeculative.x);
    const uint residentBodyCount = asuint(g_NwbPhysicsPushConstants.gravitySpeculative.y);
    if(bodyMaskWakeSlot != NWB_PHYSICS_INVALID_INDEX && idx < residentBodyCount && (bodyFlags & NWB_PHYSICS_BODY_FLAG_DYNAMIC) != 0u && (bodyFlags & NWB_PHYSICS_BODY_FLAG_SLEEPING) == 0u){
        const float coherentSleep = asfloat(g_NwbPhysicsUintBuffers[bodyMaskWakeSlot][residentBodyCount + idx]);
        if(coherentSleep < bo.positionSleepTime.w){
            bo.positionSleepTime.w = coherentSleep;
            g_NwbPhysicsBodyOutBuffers[bodyOutSlot][idx].positionSleepTime.w = coherentSleep;
        }
    }
    const float timeToSleep = g_NwbPhysicsPushConstants.linearSlop;
    if((bodyFlags & NWB_PHYSICS_BODY_FLAG_SLEEPING) != 0u){
        bo.linearVelocityFlags.xyz = float3(0.0f, 0.0f, 0.0f);
        bo.angularVelocityPad.xyz = float3(0.0f, 0.0f, 0.0f);
        bo.positionSleepTime.w = sim.invInertiaWorld1.w;
    }
    else if((bodyFlags & NWB_PHYSICS_BODY_FLAG_DYNAMIC) != 0u && (bodyFlags & NWB_PHYSICS_BODY_FLAG_ALLOW_SLEEP) != 0u && timeToSleep > 0.0f && bo.positionSleepTime.w >= timeToSleep){
        bodyFlags |= NWB_PHYSICS_BODY_FLAG_SLEEPING;
        bo.linearVelocityFlags = float4(0.0f, 0.0f, 0.0f, asfloat(bodyFlags));
        bo.angularVelocityPad = float4(0.0f, 0.0f, 0.0f, 0.0f);
        g_NwbPhysicsBodyOutBuffers[bodyOutSlot][idx] = bo;
        const uint eventRingSlot = g_NwbPhysicsPushConstants.slots1.y;
        const uint prepCounterSlot = g_NwbPhysicsPushConstants.slots1.z;
        const uint maxEvents = g_NwbPhysicsPushConstants.slots1.w;
        nwbPhysicsEmitEvent(
            prepCounterSlot,
            eventRingSlot,
            maxEvents,
            NWB_PHYSICS_GPU_EVENT_BODY_SLEEP,
            idx,
            0u,
            0u,
            float4(bo.positionSleepTime.xyz, bo.positionSleepTime.w),
            float4(0.0f, 1.0f, 0.0f, 0.0f),
            uint4(idx, NWB_PHYSICS_INVALID_INDEX, 0u, 0u)
        );
    }

    const bool rotChanged = any(bo.rotation != sim.rotation);
    float4 wI0 = sim.invInertiaWorld0;
    float2 wI1 = sim.invInertiaWorld1.xy;
    if(rotChanged)
        nwbPhysicsRotateSymMat3(bo.rotation, sim.invInertiaLocal0, sim.invInertiaLocal1.xy, wI0, wI1);

    g_NwbPhysicsBodySimBuffers[bodySimSlot][idx].positionInvMass = float4(bo.positionSleepTime.xyz, sim.positionInvMass.w);
    g_NwbPhysicsBodySimBuffers[bodySimSlot][idx].rotation = bo.rotation;
    g_NwbPhysicsBodySimBuffers[bodySimSlot][idx].forceLinearDamping = float4(0.0f, 0.0f, 0.0f, sim.forceLinearDamping.w);
    g_NwbPhysicsBodySimBuffers[bodySimSlot][idx].torqueAngularDamping = float4(0.0f, 0.0f, 0.0f, sim.torqueAngularDamping.w);
    if(rotChanged)
        g_NwbPhysicsBodySimBuffers[bodySimSlot][idx].invInertiaWorld0 = wI0;
    g_NwbPhysicsBodySimBuffers[bodySimSlot][idx].invInertiaWorld1 = float4(wI1.x, wI1.y, sim.invInertiaWorld1.z, bo.positionSleepTime.w);

    NwbPhysicsGpuBodyState nextState;
    nextState.linearVelocityPad = float4(bo.linearVelocityFlags.xyz, asfloat(bodyFlags));
    nextState.angularVelocityPad = float4(bo.angularVelocityPad.xyz, 0.0f);
    nextState.deltaPositionFlags = float4(0.0f, 0.0f, 0.0f, asfloat(bodyFlags));
    nextState.deltaRotation = float4(0.0f, 0.0f, 0.0f, 1.0f);
    g_NwbPhysicsBodyStateBuffers[bodyStateSlot][idx] = nextState;

    nwbPhysicsWriteResidentBodyPose(bodyPoseSlot, poseOffset, idx, bo, sim.localCenterMinExtent.xyz, max(g_NwbPhysicsPushConstants.deltaTime, 0.0f));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

