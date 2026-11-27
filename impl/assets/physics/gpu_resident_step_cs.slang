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
    const int groupA = asint(filterA.z);
    const int groupB = asint(filterB.z);
    if(groupA == groupB && groupA != 0){
        if(groupA <= 0)
            return false;
    }
    else if(((filterA.x & filterB.y) == 0u) || ((filterB.x & filterA.y) == 0u)){
        return false;
    }
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


uint nwbPhysicsStepResolveCombineMode(const uint modeA, const uint modeB, const uint defaultMode){
    if(modeA == modeB)
        return modeA;
    if(modeA == defaultMode)
        return modeB;
    if(modeB == defaultMode)
        return modeA;
    return (modeA >= modeB) ? modeA : modeB;
}

float nwbPhysicsStepCombineVal(const float a, const float b, const uint mode){
    if(mode == 0u)
        return 0.5f * (a + b);
    if(mode == 1u)
        return min(a, b);
    if(mode == 2u)
        return a * b;
    if(mode == 3u)
        return max(a, b);
    return sqrt(max(a, 0.0f) * max(b, 0.0f));
}

void nwbPhysicsStepEmitEvent(
    const uint prepCounterSlot,
    const uint eventRingSlot,
    const uint maxEvents,
    const uint type,
    const uint dataA,
    const uint dataB,
    const uint extraFlags,
    const float4 pointImpulse,
    const float4 normalApproach,
    const uint4 bodyIds
){
    if(eventRingSlot == NWB_PHYSICS_INVALID_INDEX || prepCounterSlot == NWB_PHYSICS_INVALID_INDEX || maxEvents == 0u)
        return;
    uint evIdx = 0u;
    InterlockedAdd(g_NwbPhysicsUintBuffers[prepCounterSlot][1u], 1u, evIdx);
    if(evIdx < maxEvents){
        NwbPhysicsGpuEvent ev;
        ev.header = uint4(type, dataA, dataB, extraFlags);
        ev.pointImpulse = pointImpulse;
        ev.normalApproach = normalApproach;
        ev.bodyIds = bodyIds;
        g_NwbPhysicsEventBuffers[eventRingSlot][evIdx] = ev;
    }
    else{
        InterlockedAdd(g_NwbPhysicsUintBuffers[prepCounterSlot][5u], 1u);
    }
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

    if(mode == NWB_PHYSICS_RESIDENT_MODE_APPLY_BULLET_TOI){
        const uint toiManifoldSlot = g_NwbPhysicsPushConstants.slots0.w;
        const uint bulletMetaSlot = g_NwbPhysicsPushConstants.slots1.x;
        const uint shapeMatSlot = g_NwbPhysicsPushConstants.slots1.y;
        const uint toiShapeSlot = g_NwbPhysicsPushConstants.slots1.z;
        const uint prepCounterSlot = g_NwbPhysicsPushConstants.slots1.w;
        const uint eventRingSlot = asuint(g_NwbPhysicsPushConstants.gravitySpeculative.x);
        const uint maxEvents = asuint(g_NwbPhysicsPushConstants.gravitySpeculative.y);
        const float hitThresh = g_NwbPhysicsPushConstants.gravitySpeculative.z;
        const float dt = g_NwbPhysicsPushConstants.deltaTime;
        const uint resSlot = g_NwbPhysicsUintBuffers[bulletMetaSlot][idx * 4u + 0u];
        const uint pairStart = g_NwbPhysicsUintBuffers[bulletMetaSlot][idx * 4u + 1u];
        const uint pairCount = g_NwbPhysicsUintBuffers[bulletMetaSlot][idx * 4u + 2u];
        uint shapeAIdx = g_NwbPhysicsUintBuffers[bulletMetaSlot][idx * 4u + 3u];

        float bestT = 1.0f;
        float3 bestN = float3(0.0f, 1.0f, 0.0f);
        float3 bestPoint = float3(0.0f, 0.0f, 0.0f);
        uint bestShapeA = NWB_PHYSICS_INVALID_INDEX;
        uint bestShapeB = NWB_PHYSICS_INVALID_INDEX;
        bool found = false;
        [loop] for(uint pi = 0u; pi < pairCount; ++pi){
            const uint4 pairInfo = g_NwbPhysicsNarrowphaseManifoldBuffers[toiManifoldSlot][pairStart + pi].pairInfo;
            if(pairInfo.z > 0u && pairInfo.w == 0u){
                const float4 normalPad = g_NwbPhysicsNarrowphaseManifoldBuffers[toiManifoldSlot][pairStart + pi].normalPad;
                if(normalPad.w > 1.0e-5f && normalPad.w < bestT){
                    bestT = normalPad.w;
                    bestN = nwbPhysicsSafeNormal(normalPad.xyz, float3(0.0f, 1.0f, 0.0f));
                    bestPoint = g_NwbPhysicsNarrowphaseManifoldBuffers[toiManifoldSlot][pairStart + pi].anchorAFeature[0].xyz;
                    bestShapeA = pairInfo.x;
                    bestShapeB = pairInfo.y;
                    found = true;
                }
            }
        }
        if(!found)
            return;

        NwbPhysicsGpuBodyOut boA = g_NwbPhysicsBodyOutBuffers[bodyOutSlot][resSlot];
        NwbPhysicsGpuBodySim simA = g_NwbPhysicsBodySimBuffers[bodySimSlot][resSlot];
        if(toiShapeSlot != NWB_PHYSICS_INVALID_INDEX){
            if(bestShapeA != NWB_PHYSICS_INVALID_INDEX)
                shapeAIdx = g_NwbPhysicsShapeProxyBuffers[toiShapeSlot][bestShapeA].bodyIndexTypeShape.z;
            if(bestShapeB != NWB_PHYSICS_INVALID_INDEX)
                bestShapeB = g_NwbPhysicsShapeProxyBuffers[toiShapeSlot][bestShapeB].bodyIndexTypeShape.z;
        }

        float restitution = 0.0f;
        float friction = 0.6f;
        uint matFA = 0u;
        uint matFB = 0u;
        uint worldBodyIdA = NWB_PHYSICS_INVALID_INDEX;
        uint worldBodyIdB = NWB_PHYSICS_INVALID_INDEX;
        uint resSlotB = NWB_PHYSICS_INVALID_INDEX;
        if(shapeMatSlot != NWB_PHYSICS_INVALID_INDEX && shapeAIdx != NWB_PHYSICS_INVALID_INDEX && bestShapeB != NWB_PHYSICS_INVALID_INDEX){
            const NwbPhysicsGpuShapeMaterial matA = g_NwbPhysicsShapeMaterialBuffers[shapeMatSlot][shapeAIdx];
            const NwbPhysicsGpuShapeMaterial matB = g_NwbPhysicsShapeMaterialBuffers[shapeMatSlot][bestShapeB];
            matFA = asuint(matA.tanFlagsThreshSlot.y);
            matFB = asuint(matB.tanFlagsThreshSlot.y);
            const uint fricMode = nwbPhysicsStepResolveCombineMode(matFA & 7u, matFB & 7u, 4u);
            const uint restMode = nwbPhysicsStepResolveCombineMode((matFA >> 3u) & 7u, (matFB >> 3u) & 7u, 3u);
            friction = max(nwbPhysicsStepCombineVal(max(matA.fricRestStatRoll.x, 0.0f), max(matB.fricRestStatRoll.x, 0.0f), fricMode), 0.0f);
            restitution = max(nwbPhysicsStepCombineVal(matA.fricRestStatRoll.y, matB.fricRestStatRoll.y, restMode), 0.0f);
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

        bool hasDynB = false;
        NwbPhysicsGpuBodyOut boB;
        NwbPhysicsGpuBodySim simB;
        uint flagsB = 0u;
        float3 vB = float3(0.0f, 0.0f, 0.0f);
        float3 wB = float3(0.0f, 0.0f, 0.0f);
        float3 cHitB = float3(0.0f, 0.0f, 0.0f);
        float4 qHitB = float4(0.0f, 0.0f, 0.0f, 1.0f);
        float invMassB = 0.0f;
        float4 maskedIB0 = float4(0.0f, 0.0f, 0.0f, 0.0f);
        float4 invIB1 = float4(0.0f, 0.0f, 0.0f, 0.0f);
        const uint resBodyCountLimit = g_NwbPhysicsPushConstants.flags;
        if(resSlotB != NWB_PHYSICS_INVALID_INDEX && resSlotB < resBodyCountLimit && resSlotB != resSlot){
            boB = g_NwbPhysicsBodyOutBuffers[bodyOutSlot][resSlotB];
            simB = g_NwbPhysicsBodySimBuffers[bodySimSlot][resSlotB];
            flagsB = asuint(boB.linearVelocityFlags.w);
            if((flagsB & NWB_PHYSICS_BODY_FLAG_DYNAMIC) != 0u){
                hasDynB = true;
                vB = boB.linearVelocityFlags.xyz;
                wB = boB.angularVelocityPad.xyz;
                const float3 c1B = boB.positionSleepTime.xyz;
                const float3 c0B = c1B - vB * dt;
                const float4 q1RawB = boB.rotation;
                const float4 q0B = nwbPhysicsIntegrateDeltaQuat(q1RawB, -wB, dt);
                const float4 q1B = (dot(q0B, q1RawB) < 0.0f) ? -q1RawB : q1RawB;
                cHitB = lerp(c0B, c1B, bestT);
                qHitB = nwbPhysicsQuatNormalize(lerp(q0B, q1B, bestT));
                invMassB = ((flagsB & 0x07u) != 0x07u) ? simB.positionInvMass.w : 0.0f;
                float4 rawIB0;
                float2 rawIB1;
                nwbPhysicsRotateSymMat3(qHitB, simB.invInertiaLocal0, simB.invInertiaLocal1.xy, rawIB0, rawIB1);
                float2 maskedIB1;
                nwbPhysicsMaskInvInertia(rawIB0, rawIB1, flagsB, maskedIB0, maskedIB1);
                invIB1 = float4(maskedIB1.x, maskedIB1.y, 0.0f, 0.0f);
            }
        }

        const float3 rA = bestPoint - cHitA;
        const float3 rB = hasDynB ? (bestPoint - cHitB) : float3(0.0f, 0.0f, 0.0f);
        const float3 velPtA = vA + cross(wA, rA);
        const float3 velPtB = hasDynB ? (vB + cross(wB, rB)) : float3(0.0f, 0.0f, 0.0f);
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
                if(friction > 0.0f){
                    const float3 vRelPost = (vA + cross(wA, rA)) - (hasDynB ? (vB + cross(wB, rB)) : float3(0.0f, 0.0f, 0.0f));
                    const float3 vt = vRelPost - dot(vRelPost, bestN) * bestN;
                    const float vtLen = length(vt);
                    if(vtLen > 1.0e-6f){
                        const float3 tDir = vt * (1.0f / vtLen);
                        const float3 rtA = cross(rA, tDir);
                        const float3 iRtA = nwbPhysicsMulSymmetric3(maskedIA0, invIA1, rtA);
                        const float3 rtB = cross(rB, tDir);
                        const float3 iRtB = hasDynB ? nwbPhysicsMulSymmetric3(maskedIB0, invIB1, rtB) : float3(0.0f, 0.0f, 0.0f);
                        const float kT = invMassA + (hasDynB ? invMassB : 0.0f) + dot(rtA, iRtA) + dot(rtB, iRtB);
                        const float jt = (kT > 1.0e-12f) ? min(vtLen / kT, friction * jn) : 0.0f;
                        if(jt > 0.0f){
                            vA = nwbPhysicsApplyLinearLocks(vA - (invMassA * jt) * tDir, flagsA);
                            wA = nwbPhysicsApplyAngularLocks(wA - iRtA * jt, flagsA);
                            if(hasDynB){
                                vB = nwbPhysicsApplyLinearLocks(vB + (invMassB * jt) * tDir, flagsB);
                                wB = nwbPhysicsApplyAngularLocks(wB + iRtB * jt, flagsB);
                            }
                        }
                    }
                }
            }
        }

        const float remDt = max((1.0f - bestT) * dt, 0.0f);
        const float3 cNewA = cHitA + vA * remDt;
        const float4 qNewA = nwbPhysicsIntegrateDeltaQuat(qHitA, wA, remDt);
        boA.positionSleepTime = float4(cNewA, 0.0f);
        boA.rotation = qNewA;
        boA.linearVelocityFlags.xyz = vA;
        boA.angularVelocityPad.xyz = wA;
        g_NwbPhysicsBodyOutBuffers[bodyOutSlot][resSlot] = boA;
        g_NwbPhysicsBodySimBuffers[bodySimSlot][resSlot].positionInvMass.xyz = cNewA;
        g_NwbPhysicsBodySimBuffers[bodySimSlot][resSlot].rotation = qNewA;
        g_NwbPhysicsBodySimBuffers[bodySimSlot][resSlot].invInertiaWorld1.w = 0.0f;
        g_NwbPhysicsBodyStateBuffers[bodyStateSlot][resSlot].linearVelocityPad.xyz = vA;
        g_NwbPhysicsBodyStateBuffers[bodyStateSlot][resSlot].angularVelocityPad.xyz = wA;

        if(hasDynB){
            const bool wakeB = (flagsB & NWB_PHYSICS_BODY_FLAG_SLEEPING) != 0u;
            const uint newFlagsB = flagsB & ~NWB_PHYSICS_BODY_FLAG_SLEEPING;
            const float3 cNewB = cHitB + vB * remDt;
            const float4 qNewB = nwbPhysicsIntegrateDeltaQuat(qHitB, wB, remDt);
            boB.positionSleepTime = float4(cNewB, 0.0f);
            boB.rotation = qNewB;
            boB.linearVelocityFlags = float4(vB, asfloat(newFlagsB));
            boB.angularVelocityPad.xyz = wB;
            g_NwbPhysicsBodyOutBuffers[bodyOutSlot][resSlotB] = boB;
            g_NwbPhysicsBodySimBuffers[bodySimSlot][resSlotB].positionInvMass.xyz = cNewB;
            g_NwbPhysicsBodySimBuffers[bodySimSlot][resSlotB].rotation = qNewB;
            g_NwbPhysicsBodySimBuffers[bodySimSlot][resSlotB].invInertiaWorld1.w = 0.0f;
            nwbPhysicsWriteBodyFlags(bodyStateSlot, resSlotB, newFlagsB);
            g_NwbPhysicsBodyStateBuffers[bodyStateSlot][resSlotB].linearVelocityPad.xyz = vB;
            g_NwbPhysicsBodyStateBuffers[bodyStateSlot][resSlotB].angularVelocityPad.xyz = wB;
            if(wakeB)
                nwbPhysicsStepEmitEvent(prepCounterSlot, eventRingSlot, maxEvents, NWB_PHYSICS_GPU_EVENT_BODY_WAKE, resSlotB, 0u, 0u, float4(cNewB, 0.0f), float4(0.0f, 1.0f, 0.0f, 0.0f), uint4(resSlotB, NWB_PHYSICS_INVALID_INDEX, 0u, 0u));
        }
        else if((matFB & NWB_PHYSICS_MAT_FLAG_CPU_SLEEPING) != 0u && worldBodyIdB != NWB_PHYSICS_INVALID_INDEX){
            nwbPhysicsStepEmitEvent(prepCounterSlot, eventRingSlot, maxEvents, NWB_PHYSICS_GPU_EVENT_BODY_WAKE, worldBodyIdB, worldBodyIdA, NWB_PHYSICS_EVENT_FLAG_CPU_SLEEP_WAKE, float4(bestPoint, 0.0f), float4(-bestN, 0.0f), uint4(worldBodyIdB, worldBodyIdA, resSlot, 0u));
        }

        if((((matFA | matFB) & NWB_PHYSICS_MAT_FLAG_ENABLE_HIT_EVENTS) != 0u) && relVelN >= hitThresh && jn > 0.0f){
            nwbPhysicsStepEmitEvent(
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
        return;
    }

    if(mode == NWB_PHYSICS_RESIDENT_MODE_SYNC_POSES){
        if(bodyPoseSlot != NWB_PHYSICS_INVALID_INDEX){
            const NwbPhysicsGpuBodyOut boSync = g_NwbPhysicsBodyOutBuffers[bodyOutSlot][idx];
            const float3 localCenterSync = g_NwbPhysicsBodySimBuffers[bodySimSlot][idx].localCenterMinExtent.xyz;
            const float3 originSync = boSync.positionSleepTime.xyz - nwbPhysicsQuatRotate(localCenterSync, boSync.rotation);
            const float existingPoseFlags = g_NwbPhysicsBodyPoseBuffers[bodyPoseSlot][poseOffset + idx].sweepDispFlags.w;
            NwbPhysicsGpuBodyPose poseSync;
            poseSync.originPad = float4(originSync, boSync.positionSleepTime.w);
            poseSync.rotation = boSync.rotation;
            poseSync.sweepDispFlags = float4(boSync.linearVelocityFlags.xyz * max(g_NwbPhysicsPushConstants.deltaTime, 0.0f), existingPoseFlags);
            g_NwbPhysicsBodyPoseBuffers[bodyPoseSlot][poseOffset + idx] = poseSync;
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
        nwbPhysicsStepEmitEvent(
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

    if(bodyPoseSlot != NWB_PHYSICS_INVALID_INDEX){
        const float3 originUpd = bo.positionSleepTime.xyz - nwbPhysicsQuatRotate(sim.localCenterMinExtent.xyz, bo.rotation);
        const float stepDt = max(g_NwbPhysicsPushConstants.deltaTime, 0.0f);
        const float existingPoseFlags = g_NwbPhysicsBodyPoseBuffers[bodyPoseSlot][poseOffset + idx].sweepDispFlags.w;
        NwbPhysicsGpuBodyPose poseUpd;
        poseUpd.originPad = float4(originUpd, bo.positionSleepTime.w);
        poseUpd.rotation = bo.rotation;
        poseUpd.sweepDispFlags = float4(bo.linearVelocityFlags.xyz * stepDt, existingPoseFlags);
        g_NwbPhysicsBodyPoseBuffers[bodyPoseSlot][poseOffset + idx] = poseUpd;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

