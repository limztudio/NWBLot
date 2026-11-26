// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "physics_common.slangi"
#include "solver_body_helpers.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void nwbPhysicsResEmitEvent(
    const uint prepCountersSlot,
    const uint eventRingSlot,
    const uint maxEvents,
    const uint type,
    const uint dataA,
    const uint dataB,
    const uint extraFlags,
    const float4 pointImpulse,
    const float4 normalApproach
){
    if(eventRingSlot == NWB_PHYSICS_INVALID_INDEX || prepCountersSlot == NWB_PHYSICS_INVALID_INDEX || maxEvents == 0u)
        return;
    uint evIdx = 0u;
    InterlockedAdd(g_NwbPhysicsUintBuffers[prepCountersSlot][1u], 1u, evIdx);
    if(evIdx < maxEvents){
        NwbPhysicsGpuEvent ev;
        ev.header = uint4(type, dataA, dataB, extraFlags);
        ev.pointImpulse = pointImpulse;
        ev.normalApproach = normalApproach;
        g_NwbPhysicsEventBuffers[eventRingSlot][evIdx] = ev;
    }
}

uint nwbPhysicsResResolveCombineMode(const uint modeA, const uint modeB, const uint defaultMode){
    if(modeA == modeB)
        return modeA;
    if(modeA == defaultMode)
        return modeB;
    if(modeB == defaultMode)
        return modeA;
    return (modeA >= modeB) ? modeA : modeB;
}

float nwbPhysicsResCombineMaterialVal(const float a, const float b, const uint mode){
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

void nwbPhysicsResMakeSoftness(const float hertz, const float zeta, const float h, out float outBiasRate, out float outMassScale, out float outImpulseScale){
    if(hertz <= 0.0f){
        outBiasRate = 0.0f;
        outMassScale = 0.0f;
        outImpulseScale = 0.0f;
        return;
    }
    const float omega = 2.0f * 3.1415926535897932f * hertz;
    const float a1 = 2.0f * zeta + h * omega;
    const float a2 = h * omega * a1;
    const float a3 = 1.0f / (1.0f + a2);
    outBiasRate = omega / a1;
    outMassScale = a2 * a3;
    outImpulseScale = a3;
}

uint nwbPhysicsResHashShapePair(const uint shapeA, const uint shapeB, const uint mask){
    const uint lo = min(shapeA, shapeB);
    const uint hi = max(shapeA, shapeB);
    return ((lo * 73856093u) ^ (hi * 19349663u)) & mask;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


groupshared uint s_GroupRawCount;
groupshared uint s_GroupRawBase;
groupshared uint s_ColorCounts[NWB_PHYSICS_GRAPH_COLOR_COUNT];
groupshared uint s_ColorBases[NWB_PHYSICS_GRAPH_COLOR_COUNT];


[numthreads(NWB_PHYSICS_WORKGROUP_SIZE, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID, uint3 groupThreadID : SV_GroupThreadID){
    const uint tid = dispatchThreadID.x;
    const uint localTid = groupThreadID.x;
    const uint mode = g_NwbPhysicsPushConstants.modeOrSecondaryCount;
    const uint flags = g_NwbPhysicsPushConstants.flags;

    if(mode == NWB_PHYSICS_RES_PREP_MODE_CLEAR){
        const uint itemCount = g_NwbPhysicsPushConstants.itemCount;
        const uint residentBodyCount = g_NwbPhysicsPushConstants.itemOffset;
        const uint warmCap = flags;
        const uint prepCountersSlot = g_NwbPhysicsPushConstants.slots0.x;
        const uint colorMetaSlot = g_NwbPhysicsPushConstants.slots0.y;
        const uint colorIndirectSlot = g_NwbPhysicsPushConstants.slots0.z;
        const uint bodyMaskWakeSlot = g_NwbPhysicsPushConstants.slots0.w;
        const uint warmNextSlot = g_NwbPhysicsPushConstants.slots1.x;
        const uint counterSlot = g_NwbPhysicsPushConstants.slots1.y;
        const uint maxPairs = g_NwbPhysicsPushConstants.slots1.z;

        if(tid == 0u){
            [loop] for(uint k = 0u; k < 32u; ++k)
                g_NwbPhysicsUintBuffers[prepCountersSlot][k] = 0u;
            [loop] for(uint k = 0u; k < NWB_PHYSICS_RES_COLOR_META_UINT_COUNT; ++k)
                g_NwbPhysicsUintBuffers[colorMetaSlot][k] = 0u;
            [loop] for(uint c = 0u; c <= NWB_PHYSICS_RES_INDIRECT_BUILD_SLOT; ++c){
                const uint base = c * 3u;
                g_NwbPhysicsUintBuffers[colorIndirectSlot][base + 0u] = 0u;
                g_NwbPhysicsUintBuffers[colorIndirectSlot][base + 1u] = 1u;
                g_NwbPhysicsUintBuffers[colorIndirectSlot][base + 2u] = 1u;
            }
            const uint activeManifolds = (counterSlot != NWB_PHYSICS_INVALID_INDEX && maxPairs > 0u)
                ? min(g_NwbPhysicsUintBuffers[counterSlot][2u], maxPairs)
                : maxPairs;
            const uint buildGroups = (activeManifolds > 0u)
                ? ((activeManifolds + NWB_PHYSICS_WORKGROUP_SIZE - 1u) / NWB_PHYSICS_WORKGROUP_SIZE)
                : 0u;
            g_NwbPhysicsUintBuffers[colorIndirectSlot][NWB_PHYSICS_RES_INDIRECT_BUILD_SLOT * 3u + 0u] = buildGroups;
        }
        if(tid >= itemCount)
            return;
        if(tid < residentBodyCount && bodyMaskWakeSlot != NWB_PHYSICS_INVALID_INDEX){
            g_NwbPhysicsUintBuffers[bodyMaskWakeSlot][tid] = 0u;
            g_NwbPhysicsUintBuffers[bodyMaskWakeSlot][residentBodyCount + tid] = 0u;
        }
        if(tid < warmCap && warmNextSlot != NWB_PHYSICS_INVALID_INDEX){
            g_NwbPhysicsWarmCacheBuffers[warmNextSlot][tid].keyMeta = uint4(NWB_PHYSICS_INVALID_INDEX, NWB_PHYSICS_INVALID_INDEX, 0u, 0u);
        }
        return;
    }

    if(mode == NWB_PHYSICS_RES_PREP_MODE_PREFIX_COLORS){
        if(tid != 0u)
            return;
        const uint prepCountersSlot = g_NwbPhysicsPushConstants.slots0.x;
        const uint colorMetaSlot = g_NwbPhysicsPushConstants.slots0.y;
        const uint colorIndirectSlot = g_NwbPhysicsPushConstants.slots0.z;
        const uint maxConstraints = flags;

        uint runningOffset = 0u;
        [loop] for(uint c = 0u; c < NWB_PHYSICS_GRAPH_COLOR_COUNT; ++c){
            const uint rawCnt = g_NwbPhysicsUintBuffers[prepCountersSlot][8u + c];
            const uint avail = (maxConstraints > runningOffset) ? (maxConstraints - runningOffset) : 0u;
            const uint cnt = min(rawCnt, avail);
            g_NwbPhysicsUintBuffers[colorMetaSlot][c * 2u + 0u] = runningOffset;
            g_NwbPhysicsUintBuffers[colorMetaSlot][c * 2u + 1u] = cnt;
            runningOffset += cnt;
        }

        const uint fusedStartColor = (runningOffset <= NWB_PHYSICS_RES_FUSED_ALL_MAX_CONTACTS) ? 0u : NWB_PHYSICS_RES_WIDE_COLOR_COUNT;
        g_NwbPhysicsUintBuffers[colorMetaSlot][NWB_PHYSICS_GRAPH_COLOR_COUNT * 2u + 0u] = 0u;
        g_NwbPhysicsUintBuffers[colorMetaSlot][NWB_PHYSICS_GRAPH_COLOR_COUNT * 2u + 1u] = runningOffset;
        g_NwbPhysicsUintBuffers[colorMetaSlot][NWB_PHYSICS_GRAPH_COLOR_COUNT * 2u + 2u] = fusedStartColor;

        bool fusedHasWork = false;
        [loop] for(uint c = 0u; c < NWB_PHYSICS_GRAPH_COLOR_COUNT; ++c){
            const uint cnt = g_NwbPhysicsUintBuffers[colorMetaSlot][c * 2u + 1u];
            if(c < fusedStartColor){
                const uint groups = (cnt > 0u) ? ((cnt + NWB_PHYSICS_WORKGROUP_SIZE - 1u) / NWB_PHYSICS_WORKGROUP_SIZE) : 0u;
                g_NwbPhysicsUintBuffers[colorIndirectSlot][c * 3u + 0u] = groups;
                g_NwbPhysicsUintBuffers[colorIndirectSlot][c * 3u + 1u] = 1u;
                g_NwbPhysicsUintBuffers[colorIndirectSlot][c * 3u + 2u] = 1u;
            }
            else{
                if(cnt > 0u)
                    fusedHasWork = true;
                if(c < NWB_PHYSICS_RES_WIDE_COLOR_COUNT){
                    g_NwbPhysicsUintBuffers[colorIndirectSlot][c * 3u + 0u] = 0u;
                    g_NwbPhysicsUintBuffers[colorIndirectSlot][c * 3u + 1u] = 1u;
                    g_NwbPhysicsUintBuffers[colorIndirectSlot][c * 3u + 2u] = 1u;
                }
            }
        }
        g_NwbPhysicsUintBuffers[colorMetaSlot][NWB_PHYSICS_GRAPH_COLOR_COUNT * 2u + 3u] = fusedHasWork ? 1u : 0u;
        g_NwbPhysicsUintBuffers[colorIndirectSlot][NWB_PHYSICS_RES_INDIRECT_FUSED_SLOT * 3u + 0u] = fusedHasWork ? 1u : 0u;
        g_NwbPhysicsUintBuffers[colorIndirectSlot][NWB_PHYSICS_RES_INDIRECT_FUSED_SLOT * 3u + 1u] = 1u;
        g_NwbPhysicsUintBuffers[colorIndirectSlot][NWB_PHYSICS_RES_INDIRECT_FUSED_SLOT * 3u + 2u] = 1u;
        g_NwbPhysicsUintBuffers[prepCountersSlot][2u] = runningOffset;
        const uint allGroups = (runningOffset > 0u) ? ((runningOffset + NWB_PHYSICS_WORKGROUP_SIZE - 1u) / NWB_PHYSICS_WORKGROUP_SIZE) : 0u;
        g_NwbPhysicsUintBuffers[colorIndirectSlot][NWB_PHYSICS_RES_INDIRECT_ALL_SLOT * 3u + 0u] = allGroups;
        g_NwbPhysicsUintBuffers[colorIndirectSlot][NWB_PHYSICS_RES_INDIRECT_ALL_SLOT * 3u + 1u] = 1u;
        g_NwbPhysicsUintBuffers[colorIndirectSlot][NWB_PHYSICS_RES_INDIRECT_ALL_SLOT * 3u + 2u] = 1u;
        return;
    }

    if(mode == NWB_PHYSICS_RES_PREP_MODE_SCATTER_CONSTRAINTS){
        const uint prepCountersSlot = g_NwbPhysicsPushConstants.slots0.x;
        const uint colorMetaSlot = g_NwbPhysicsPushConstants.slots0.y;
        const uint rawConstraintSlot = g_NwbPhysicsPushConstants.slots0.z;
        const uint constraintSlot = g_NwbPhysicsPushConstants.slots0.w;
        const uint rawMetaFeatSlot = g_NwbPhysicsPushConstants.slots1.x;
        const uint sortedMetaFeatSlot = g_NwbPhysicsPushConstants.slots1.y;
        const uint contactTotalImpSlot = g_NwbPhysicsPushConstants.slots1.z;
        const uint totalCount = g_NwbPhysicsUintBuffers[prepCountersSlot][2u];
        if(tid >= totalCount)
            return;

        const uint4 rawMeta = asuint(g_NwbPhysicsFloat4Buffers[rawMetaFeatSlot][tid * 2u + 0u]);
        const uint4 rawFeat = asuint(g_NwbPhysicsFloat4Buffers[rawMetaFeatSlot][tid * 2u + 1u]);
        const uint colorIdx = rawMeta.x;
        const uint localIdx = rawMeta.y;
        const uint colorOffset = g_NwbPhysicsUintBuffers[colorMetaSlot][colorIdx * 2u + 0u];
        const uint colorCount = g_NwbPhysicsUintBuffers[colorMetaSlot][colorIdx * 2u + 1u];
        if(localIdx >= colorCount)
            return;
        const uint dstIdx = colorOffset + localIdx;
        g_NwbPhysicsContactConstraintBuffers[constraintSlot][dstIdx] = g_NwbPhysicsContactConstraintBuffers[rawConstraintSlot][tid];
        g_NwbPhysicsFloat4Buffers[sortedMetaFeatSlot][dstIdx * 2u + 0u] = asfloat(rawMeta);
        g_NwbPhysicsFloat4Buffers[sortedMetaFeatSlot][dstIdx * 2u + 1u] = asfloat(rawFeat);
        g_NwbPhysicsFloat4Buffers[contactTotalImpSlot][dstIdx] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    if(mode == NWB_PHYSICS_RES_PREP_MODE_STORE_WARM_START){
        const uint warmMask = g_NwbPhysicsPushConstants.itemCount;
        const uint maxEvents = (flags >> 1u) * 64u;
        const bool hasHitEvents = (flags & 1u) != 0u;
        const float hitThresh = g_NwbPhysicsPushConstants.linearSlop;
        const uint constraintSlot = g_NwbPhysicsPushConstants.slots0.x;
        const uint contactImpOutSlot = g_NwbPhysicsPushConstants.slots0.y;
        const uint sortedMetaFeatSlot = g_NwbPhysicsPushConstants.slots0.z;
        const uint warmNextSlot = g_NwbPhysicsPushConstants.slots0.w;
        const uint prepCountersSlot = g_NwbPhysicsPushConstants.slots1.x;
        const uint eventRingSlot = g_NwbPhysicsPushConstants.slots1.y;
        const uint shapeMatSlot = g_NwbPhysicsPushConstants.slots1.z;
        const uint totalCount = g_NwbPhysicsUintBuffers[prepCountersSlot][2u];
        if(tid >= totalCount)
            return;

        const uint4 meta = asuint(g_NwbPhysicsFloat4Buffers[sortedMetaFeatSlot][tid * 2u + 0u]);
        const uint4 feat = asuint(g_NwbPhysicsFloat4Buffers[sortedMetaFeatSlot][tid * 2u + 1u]);
        const NwbPhysicsGpuContactImpulseOut imp = g_NwbPhysicsContactImpulseOutBuffers[contactImpOutSlot][tid];
        const NwbPhysicsGpuContactConstraint gc = g_NwbPhysicsContactConstraintBuffers[constraintSlot][tid];
        const uint shapeA = meta.z;
        const uint shapeB = meta.w;
        const uint ptCount = min(gc.indicesCountFlags.z, NWB_PHYSICS_MAX_MANIFOLD_POINTS);

        if(warmNextSlot != NWB_PHYSICS_INVALID_INDEX && warmMask > 0u){
            const uint baseSlot = nwbPhysicsResHashShapePair(shapeA, shapeB, warmMask);
            [loop] for(uint p = 0u; p < NWB_PHYSICS_GPU_WARM_CACHE_MAX_PROBE; ++p){
                const uint slot = (baseSlot + p) & warmMask;
                uint prevKey = NWB_PHYSICS_INVALID_INDEX;
                InterlockedCompareExchange(g_NwbPhysicsWarmCacheBuffers[warmNextSlot][slot].keyMeta.x, NWB_PHYSICS_INVALID_INDEX, shapeA, prevKey);
                if(prevKey == NWB_PHYSICS_INVALID_INDEX){
                    g_NwbPhysicsWarmCacheBuffers[warmNextSlot][slot].keyMeta.y = shapeB;
                    g_NwbPhysicsWarmCacheBuffers[warmNextSlot][slot].keyMeta.z = ptCount;
                    g_NwbPhysicsWarmCacheBuffers[warmNextSlot][slot].keyMeta.w = 1u;
                    g_NwbPhysicsWarmCacheBuffers[warmNextSlot][slot].featureIds = feat;
                    g_NwbPhysicsWarmCacheBuffers[warmNextSlot][slot].normalImpulses = imp.normalImpulses;
                    g_NwbPhysicsWarmCacheBuffers[warmNextSlot][slot].fricTwist = float4(imp.frictionTwistRollX.x, imp.frictionTwistRollX.y, imp.frictionTwistRollX.z, 0.0f);
                    break;
                }
            }
        }

        if(hasHitEvents && shapeMatSlot != NWB_PHYSICS_INVALID_INDEX && imp.rollYZMaxApproach.z >= hitThresh){
            const uint matFA = asuint(g_NwbPhysicsShapeMaterialBuffers[shapeMatSlot][shapeA].tanFlagsThreshSlot.y);
            const uint matFB = asuint(g_NwbPhysicsShapeMaterialBuffers[shapeMatSlot][shapeB].tanFlagsThreshSlot.y);
            if(((matFA | matFB) & 128u) != 0u){
                const uint maxIdx = min(asuint(imp.rollYZMaxApproach.w), NWB_PHYSICS_MAX_MANIFOLD_POINTS - 1u);
                const float maxNormImp = imp.totalNormalImpulses[maxIdx];
                if(maxNormImp > 0.0f){
                    nwbPhysicsResEmitEvent(
                        prepCountersSlot,
                        eventRingSlot,
                        maxEvents,
                        NWB_PHYSICS_GPU_EVENT_CONTACT_HIT,
                        shapeA,
                        shapeB,
                        (gc.indicesCountFlags.x & 0xffffu) | ((gc.indicesCountFlags.y & 0xffffu) << 16u),
                        float4(gc.points[maxIdx].rABaseSep.xyz, maxNormImp),
                        float4(gc.normalFriction.xyz, imp.rollYZMaxApproach.z)
                    );
                }
            }
        }
        return;
    }

    if(mode == NWB_PHYSICS_RES_PREP_MODE_WAKE_BODIES){
        const uint residentBodyCount = g_NwbPhysicsPushConstants.itemCount;
        const uint wakeOffset = g_NwbPhysicsPushConstants.itemOffset;
        const uint maxEvents = flags;
        const uint bodySimSlot = g_NwbPhysicsPushConstants.slots0.x;
        const uint bodyStateSlot = g_NwbPhysicsPushConstants.slots0.y;
        const uint bodyMaskWakeSlot = g_NwbPhysicsPushConstants.slots0.z;
        const uint prepCountersSlot = g_NwbPhysicsPushConstants.slots0.w;
        const uint eventRingSlot = g_NwbPhysicsPushConstants.slots1.x;
        if(tid >= residentBodyCount)
            return;
        if(g_NwbPhysicsUintBuffers[bodyMaskWakeSlot][wakeOffset + tid] == 0u)
            return;
        const uint oldFlags = asuint(g_NwbPhysicsBodyStateBuffers[bodyStateSlot][tid].deltaPositionFlags.w);
        if((oldFlags & NWB_PHYSICS_BODY_FLAG_SLEEPING) != 0u){
            const uint newFlags = oldFlags & ~NWB_PHYSICS_BODY_FLAG_SLEEPING;
            nwbPhysicsWriteBodyFlags(bodyStateSlot, tid, newFlags);
            g_NwbPhysicsBodySimBuffers[bodySimSlot][tid].invInertiaWorld1.w = 0.0f;
            nwbPhysicsResEmitEvent(
                prepCountersSlot,
                eventRingSlot,
                maxEvents,
                NWB_PHYSICS_GPU_EVENT_BODY_WAKE,
                tid,
                0u,
                0u,
                float4(g_NwbPhysicsBodySimBuffers[bodySimSlot][tid].positionInvMass.xyz, 0.0f),
                float4(0.0f, 1.0f, 0.0f, 0.0f)
            );
        }
        return;
    }

    if(mode != NWB_PHYSICS_RES_PREP_MODE_BUILD_CONSTRAINTS)
        return;

    const bool enableSoftening = (flags & 2u) != 0u;
    const bool enableWarmStarting = (flags & 4u) != 0u;
    const uint warmPow2 = (flags >> 8u) & 0x1fu;
    const uint warmMask = (warmPow2 > 0u) ? ((1u << warmPow2) - 1u) : 0u;
    const uint maxEvents = (flags >> 16u) * 64u;
    const uint residentBodyCount = g_NwbPhysicsPushConstants.itemOffset;
    const uint maxPairs = asuint(g_NwbPhysicsPushConstants.gravitySpeculative.w);

    const uint manifoldSlot = g_NwbPhysicsPushConstants.slots0.x;
    const uint counterSlot = g_NwbPhysicsPushConstants.slots0.y;
    const uint shapeMatSlot = g_NwbPhysicsPushConstants.slots0.z;
    const uint bodySimSlot = g_NwbPhysicsPushConstants.slots0.w;
    const uint bodyStateSlot = g_NwbPhysicsPushConstants.slots1.x;
    const uint warmPrevSlot = g_NwbPhysicsPushConstants.slots1.y;
    const uint prepCountersSlot = g_NwbPhysicsPushConstants.slots1.z;
    const uint bodyMaskWakeSlot = g_NwbPhysicsPushConstants.slots1.w;
    const uint rawConstraintSlot = asuint(g_NwbPhysicsPushConstants.gravitySpeculative.x);
    const uint rawMetaFeatSlot = asuint(g_NwbPhysicsPushConstants.gravitySpeculative.y);
    const uint eventRingSlot = asuint(g_NwbPhysicsPushConstants.gravitySpeculative.z);

    const uint activeCount = min(g_NwbPhysicsUintBuffers[counterSlot][2u], maxPairs);

    bool emitConstraint = false;
    uint shapeA = 0u;
    uint shapeB = 0u;
    uint chosenColor = NWB_PHYSICS_OVERFLOW_COLOR_INDEX;
    uint featArr[4] = { 0u, 0u, 0u, 0u };
    NwbPhysicsGpuContactConstraint gc = (NwbPhysicsGpuContactConstraint)0;

    if(tid < activeCount){
        const uint pairIdx = tid;
        const NwbPhysicsGpuNarrowphaseManifold gm = g_NwbPhysicsNarrowphaseManifoldBuffers[manifoldSlot][pairIdx];
        shapeA = gm.pairInfo.x;
        shapeB = gm.pairInfo.y;
        const uint pointCount = min(gm.pairInfo.z, NWB_PHYSICS_MAX_MANIFOLD_POINTS);
        if(gm.pairInfo.w != 0u){
            nwbPhysicsResEmitEvent(prepCountersSlot, eventRingSlot, maxEvents, NWB_PHYSICS_GPU_EVENT_CPU_FALLBACK, shapeA, shapeB, pairIdx, float4(0.0f, 0.0f, 0.0f, 0.0f), gm.normalPad);
        }
        else if(pointCount > 0u){
            const NwbPhysicsGpuShapeMaterial matA = g_NwbPhysicsShapeMaterialBuffers[shapeMatSlot][shapeA];
            const NwbPhysicsGpuShapeMaterial matB = g_NwbPhysicsShapeMaterialBuffers[shapeMatSlot][shapeB];
            const uint matFA = asuint(matA.tanFlagsThreshSlot.y);
            const uint matFB = asuint(matB.tanFlagsThreshSlot.y);
            if(((matFA | matFB) & 64u) != 0u){
                if(((matFA | matFB) & 256u) != 0u)
                    nwbPhysicsResEmitEvent(prepCountersSlot, eventRingSlot, maxEvents, NWB_PHYSICS_GPU_EVENT_SENSOR_OVERLAP, shapeA, shapeB, pointCount, gm.anchorAFeature[0], gm.normalPad);
            }
            else{
                const uint slotA = asuint(matA.tanFlagsThreshSlot.w);
                const uint slotB = asuint(matB.tanFlagsThreshSlot.w);
                const bool hasA = (slotA != NWB_PHYSICS_INVALID_INDEX && slotA < residentBodyCount);
                const bool hasB = (slotB != NWB_PHYSICS_INVALID_INDEX && slotB < residentBodyCount);
                if(hasA || hasB){
                    float3 vA;
                    float3 wA;
                    uint flagsA;
                    nwbPhysicsLoadBodyVelocity(bodyStateSlot, hasA ? slotA : NWB_PHYSICS_INVALID_INDEX, vA, wA, flagsA);

                    float3 vB;
                    float3 wB;
                    uint flagsB;
                    nwbPhysicsLoadBodyVelocity(bodyStateSlot, hasB ? slotB : NWB_PHYSICS_INVALID_INDEX, vB, wB, flagsB);

                    const bool dynA = hasA && ((flagsA & NWB_PHYSICS_BODY_FLAG_DYNAMIC) != 0u);
                    const bool dynB = hasB && ((flagsB & NWB_PHYSICS_BODY_FLAG_DYNAMIC) != 0u);
                    const bool sleepingA = hasA && ((flagsA & NWB_PHYSICS_BODY_FLAG_SLEEPING) != 0u);
                    const bool sleepingB = hasB && ((flagsB & NWB_PHYSICS_BODY_FLAG_SLEEPING) != 0u);
                    if((dynA || dynB) && !((!hasA || sleepingA) && (!hasB || sleepingB))){
                        if(dynA && sleepingA && hasB && !sleepingB)
                            g_NwbPhysicsUintBuffers[bodyMaskWakeSlot][residentBodyCount + slotA] = 1u;
                        if(dynB && sleepingB && hasA && !sleepingA)
                            g_NwbPhysicsUintBuffers[bodyMaskWakeSlot][residentBodyCount + slotB] = 1u;

                        const float h = g_NwbPhysicsPushConstants.deltaTime;
                        const float contactDampingRatio = g_NwbPhysicsPushConstants.invDeltaTime;
                        const float contactHertz = g_NwbPhysicsPushConstants.maxLinearVelocity;
                        const float restThresh = g_NwbPhysicsPushConstants.linearSlop;

                        const uint fricMode = nwbPhysicsResResolveCombineMode(matFA & 7u, matFB & 7u, 4u);
                        const uint restMode = nwbPhysicsResResolveCombineMode((matFA >> 3u) & 7u, (matFB >> 3u) & 7u, 3u);
                        const float friction = nwbPhysicsResCombineMaterialVal(max(matA.fricRestStatRoll.x, 0.0f), max(matB.fricRestStatRoll.x, 0.0f), fricMode);
                        const float restitution = max(nwbPhysicsResCombineMaterialVal(matA.fricRestStatRoll.y, matB.fricRestStatRoll.y, restMode), 0.0f);
                        const float rollingResistance = max(matA.fricRestStatRoll.w, matB.fricRestStatRoll.w);
                        const float tangentSpeed = matA.tanFlagsThreshSlot.x + matB.tanFlagsThreshSlot.x;

                        float invMassA = 0.0f;
                        float4 invIA0 = float4(0.0f, 0.0f, 0.0f, 0.0f);
                        float4 invIA1 = float4(0.0f, 0.0f, 0.0f, 0.0f);
                        float3 originToComA = float3(0.0f, 0.0f, 0.0f);
                        if(hasA){
                            const NwbPhysicsGpuBodySim sA = g_NwbPhysicsBodySimBuffers[bodySimSlot][slotA];
                            originToComA = -nwbPhysicsQuatRotate(sA.localCenterMinExtent.xyz, sA.rotation);
                            if(dynA){
                                invMassA = ((flagsA & 0x07u) != 0x07u) ? sA.positionInvMass.w : 0.0f;
                                float4 maskedI0;
                                float2 maskedI1;
                                nwbPhysicsMaskInvInertia(sA.invInertiaWorld0, sA.invInertiaWorld1.xy, flagsA, maskedI0, maskedI1);
                                invIA0 = maskedI0;
                                invIA1 = float4(maskedI1.x, maskedI1.y, 0.0f, 0.0f);
                            }
                        }

                        float invMassB = 0.0f;
                        float4 invIB0 = float4(0.0f, 0.0f, 0.0f, 0.0f);
                        float4 invIB1 = float4(0.0f, 0.0f, 0.0f, 0.0f);
                        float3 originToComB = float3(0.0f, 0.0f, 0.0f);
                        if(hasB){
                            const NwbPhysicsGpuBodySim sB = g_NwbPhysicsBodySimBuffers[bodySimSlot][slotB];
                            originToComB = -nwbPhysicsQuatRotate(sB.localCenterMinExtent.xyz, sB.rotation);
                            if(dynB){
                                invMassB = ((flagsB & 0x07u) != 0x07u) ? sB.positionInvMass.w : 0.0f;
                                float4 maskedI0;
                                float2 maskedI1;
                                nwbPhysicsMaskInvInertia(sB.invInertiaWorld0, sB.invInertiaWorld1.xy, flagsB, maskedI0, maskedI1);
                                invIB0 = maskedI0;
                                invIB1 = float4(maskedI1.x, maskedI1.y, 0.0f, 0.0f);
                            }
                        }

                        float effHertz = contactHertz;
                        if(!dynA || !dynB || invMassA <= 0.0f || invMassB <= 0.0f){
                            effHertz = 2.0f * contactHertz;
                        }
                        else if(enableSoftening && contactHertz > 0.0f && h > 0.0f){
                            const float ratio = min(invMassA, invMassB) / max(invMassA, invMassB);
                            effHertz = contactHertz * (0.5f + 0.5f * sqrt(ratio));
                        }
                        float biasRate = 0.0f;
                        float massScale = 0.0f;
                        float impulseScale = 0.0f;
                        nwbPhysicsResMakeSoftness(effHertz, contactDampingRatio, h, biasRate, massScale, impulseScale);

                        const float3 n = nwbPhysicsSafeNormal(gm.normalPad.xyz, float3(0.0f, 1.0f, 0.0f));
                        const float3 upVec = (abs(n.y) > 0.9f) ? float3(1.0f, 0.0f, 0.0f) : float3(0.0f, 1.0f, 0.0f);
                        const float3 t1 = nwbPhysicsSafeNormal(cross(upVec, n), float3(1.0f, 0.0f, 0.0f));
                        const float3 t2 = cross(n, t1);

                        float4 cachedNorm = float4(0.0f, 0.0f, 0.0f, 0.0f);
                        float2 cachedFric = float2(0.0f, 0.0f);
                        float cachedTwist = 0.0f;
                        [loop] for(uint i = 0u; i < pointCount; ++i)
                            featArr[i] = asuint(gm.anchorAFeature[i].w);

                        if(enableWarmStarting && warmMask > 0u && warmPrevSlot != NWB_PHYSICS_INVALID_INDEX){
                            const uint baseSlot = nwbPhysicsResHashShapePair(shapeA, shapeB, warmMask);
                            [loop] for(uint p = 0u; p < NWB_PHYSICS_GPU_WARM_CACHE_MAX_PROBE; ++p){
                                const uint slot = (baseSlot + p) & warmMask;
                                const NwbPhysicsGpuWarmCacheEntry ce = g_NwbPhysicsWarmCacheBuffers[warmPrevSlot][slot];
                                if(ce.keyMeta.x == NWB_PHYSICS_INVALID_INDEX)
                                    break;
                                if((ce.keyMeta.x == shapeA && ce.keyMeta.y == shapeB) || (ce.keyMeta.x == shapeB && ce.keyMeta.y == shapeA)){
                                    const uint cCount = min(ce.keyMeta.z, NWB_PHYSICS_MAX_MANIFOLD_POINTS);
                                    [loop] for(uint i = 0u; i < pointCount; ++i){
                                        [loop] for(uint j = 0u; j < cCount; ++j){
                                            if(featArr[i] == ce.featureIds[j]){
                                                cachedNorm[i] = ce.normalImpulses[j];
                                                break;
                                            }
                                        }
                                    }
                                    const float signFlip = (ce.keyMeta.x == shapeA) ? 1.0f : -1.0f;
                                    cachedFric = ce.fricTwist.xy * signFlip;
                                    cachedTwist = ce.fricTwist.z * signFlip;
                                    break;
                                }
                            }
                        }

                        const float baseLinearKN = nwbPhysicsLinearInvMassDot(n, n, invMassA, flagsA) + nwbPhysicsLinearInvMassDot(n, n, invMassB, flagsB);
                        float4 normMassArr = float4(0.0f, 0.0f, 0.0f, 0.0f);
                        float3 sumCenterA = float3(0.0f, 0.0f, 0.0f);
                        float3 sumCenterB = float3(0.0f, 0.0f, 0.0f);
                        float totalWeight = 0.0f;

                        [loop] for(uint i = 0u; i < NWB_PHYSICS_MAX_MANIFOLD_POINTS; ++i){
                            gc.points[i].rABaseSep = float4(0.0f, 0.0f, 0.0f, 0.0f);
                            gc.points[i].rBRelVel = float4(0.0f, 0.0f, 0.0f, 0.0f);
                        }

                        [loop] for(uint i = 0u; i < pointCount; ++i){
                            const float3 rA = originToComA + gm.anchorAFeature[i].xyz;
                            const float3 rB = originToComB + gm.anchorBSeparation[i].xyz;
                            const float3 rnA = cross(rA, n);
                            const float3 rnB = cross(rB, n);
                            float kN = baseLinearKN;
                            if(hasA)
                                kN += dot(rnA, nwbPhysicsMulSymmetric3(invIA0, invIA1, rnA));
                            if(hasB)
                                kN += dot(rnB, nwbPhysicsMulSymmetric3(invIB0, invIB1, rnB));
                            normMassArr[i] = (kN > 1.0e-12f) ? (1.0f / kN) : 0.0f;

                            const float3 velA = hasA ? (vA + cross(wA, rA)) : float3(0.0f, 0.0f, 0.0f);
                            const float3 velB = hasB ? (vB + cross(wB, rB)) : float3(0.0f, 0.0f, 0.0f);
                            const float relVel = dot(velB - velA, n);
                            gc.points[i].rABaseSep = float4(rA, gm.anchorBSeparation[i].w);
                            gc.points[i].rBRelVel = float4(rB, relVel);

                            const float wPt = max(cachedNorm[i], 1.0e-3f);
                            sumCenterA += rA * wPt;
                            sumCenterB += rB * wPt;
                            totalWeight += wPt;
                        }

                        float3 centerA = float3(0.0f, 0.0f, 0.0f);
                        float3 centerB = float3(0.0f, 0.0f, 0.0f);
                        float maxLeverArm = 0.0f;
                        if(pointCount == 1u){
                            centerA = gc.points[0].rABaseSep.xyz;
                            centerB = gc.points[0].rBRelVel.xyz;
                        }
                        else{
                            if(totalWeight > 0.0f){
                                centerA = sumCenterA / totalWeight;
                                centerB = sumCenterB / totalWeight;
                            }
                            float maxLeverSq = 0.0f;
                            [loop] for(uint i = 0u; i < pointCount; ++i){
                                const float3 dA = gc.points[i].rABaseSep.xyz - centerA;
                                maxLeverSq = max(maxLeverSq, dot(dA, dA));
                            }
                            maxLeverArm = sqrt(maxLeverSq);
                        }

                        const float3 rt1A = cross(centerA, t1);
                        const float3 rt1B = cross(centerB, t1);
                        const float3 rt2A = cross(centerA, t2);
                        const float3 rt2B = cross(centerB, t2);
                        const float3 irt1A = hasA ? nwbPhysicsMulSymmetric3(invIA0, invIA1, rt1A) : float3(0.0f, 0.0f, 0.0f);
                        const float3 irt2A = hasA ? nwbPhysicsMulSymmetric3(invIA0, invIA1, rt2A) : float3(0.0f, 0.0f, 0.0f);
                        const float3 irt1B = hasB ? nwbPhysicsMulSymmetric3(invIB0, invIB1, rt1B) : float3(0.0f, 0.0f, 0.0f);
                        const float3 irt2B = hasB ? nwbPhysicsMulSymmetric3(invIB0, invIB1, rt2B) : float3(0.0f, 0.0f, 0.0f);

                        const float lin00 = nwbPhysicsLinearInvMassDot(t1, t1, invMassA, flagsA) + nwbPhysicsLinearInvMassDot(t1, t1, invMassB, flagsB);
                        const float lin01 = nwbPhysicsLinearInvMassDot(t1, t2, invMassA, flagsA) + nwbPhysicsLinearInvMassDot(t1, t2, invMassB, flagsB);
                        const float lin11 = nwbPhysicsLinearInvMassDot(t2, t2, invMassA, flagsA) + nwbPhysicsLinearInvMassDot(t2, t2, invMassB, flagsB);
                        const float k00 = lin00 + dot(rt1A, irt1A) + dot(rt1B, irt1B);
                        const float k01 = lin01 + dot(rt1A, irt2A) + dot(rt1B, irt2B);
                        const float k11 = lin11 + dot(rt2A, irt2A) + dot(rt2B, irt2B);
                        const float det = k00 * k11 - k01 * k01;
                        float tan00 = 0.0f;
                        float tan01 = 0.0f;
                        float tan11 = 0.0f;
                        if(isfinite(det) && abs(det) > 1.0e-18f){
                            const float invDet = 1.0f / det;
                            tan00 = k11 * invDet;
                            tan01 = -k01 * invDet;
                            tan11 = k00 * invDet;
                        }
                        else{
                            tan00 = (k00 > 1.0e-12f) ? (1.0f / k00) : 0.0f;
                            tan11 = (k11 > 1.0e-12f) ? (1.0f / k11) : 0.0f;
                        }

                        float twistMassVal = 0.0f;
                        if(maxLeverArm > 0.0f){
                            float kTwist = 0.0f;
                            if(hasA)
                                kTwist += dot(n, nwbPhysicsMulSymmetric3(invIA0, invIA1, n));
                            if(hasB)
                                kTwist += dot(n, nwbPhysicsMulSymmetric3(invIB0, invIB1, n));
                            if(kTwist > 1.0e-12f)
                                twistMassVal = 1.0f / kTwist;
                        }
                        const float clampedWarmTwist = (twistMassVal > 0.0f && maxLeverArm > 0.0f && friction > 0.0f) ? cachedTwist : 0.0f;

                        gc.indicesCountFlags = uint4(hasA ? slotA : NWB_PHYSICS_INVALID_INDEX, hasB ? slotB : NWB_PHYSICS_INVALID_INDEX, pointCount, NWB_PHYSICS_INVALID_INDEX);
                        gc.normalFriction = float4(n, friction);
                        gc.tangent1Restitution = float4(t1, restitution);
                        gc.centerATwistMass = float4(centerA, twistMassVal);
                        gc.centerBTwistImpulse = float4(centerB, clampedWarmTwist);
                        gc.softnessTangent00 = float4(biasRate, massScale, impulseScale, tan00);
                        gc.tangentMassFriction = float4(tan01, tan11, cachedFric.x, cachedFric.y);
                        gc.tangentVelRollMaxLever = float4(tangentSpeed, rollingResistance, maxLeverArm, 0.0f);
                        gc.rollingImpulsePad = float4(0.0f, 0.0f, 0.0f, restThresh * 0.0f);
                        gc.normalImpulses = cachedNorm;
                        gc.normalMasses = normMassArr;

                        const uint kParallelColorMask = (1u << NWB_PHYSICS_OVERFLOW_COLOR_INDEX) - 1u;
                        const bool needColorA = dynA;
                        const bool needColorB = dynB;
                        if(needColorA && needColorB && slotA != slotB){
                            const uint firstSlot = min(slotA, slotB);
                            const uint secondSlot = max(slotA, slotB);
                            uint triedMask = 0u;
                            [loop] for(uint attempt = 0u; attempt < NWB_PHYSICS_OVERFLOW_COLOR_INDEX; ++attempt){
                                const uint candMask = (~(g_NwbPhysicsUintBuffers[bodyMaskWakeSlot][firstSlot] | g_NwbPhysicsUintBuffers[bodyMaskWakeSlot][secondSlot] | triedMask)) & kParallelColorMask;
                                if(candMask == 0u)
                                    break;
                                const uint c = firstbitlow(candMask);
                                const uint bit = 1u << c;
                                triedMask |= bit;
                                uint prevFirst = 0u;
                                InterlockedOr(g_NwbPhysicsUintBuffers[bodyMaskWakeSlot][firstSlot], bit, prevFirst);
                                if((prevFirst & bit) != 0u)
                                    continue;
                                uint prevSecond = 0u;
                                InterlockedOr(g_NwbPhysicsUintBuffers[bodyMaskWakeSlot][secondSlot], bit, prevSecond);
                                if((prevSecond & bit) == 0u){
                                    chosenColor = c;
                                    break;
                                }
                                uint unusedPrev = 0u;
                                InterlockedAnd(g_NwbPhysicsUintBuffers[bodyMaskWakeSlot][firstSlot], ~bit, unusedPrev);
                            }
                        }
                        else{
                            const uint singleSlot = needColorA ? slotA : slotB;
                            uint knownUsed = g_NwbPhysicsUintBuffers[bodyMaskWakeSlot][singleSlot];
                            [loop] for(uint attempt = 0u; attempt < NWB_PHYSICS_OVERFLOW_COLOR_INDEX; ++attempt){
                                const uint candMask = (~knownUsed) & kParallelColorMask;
                                if(candMask == 0u)
                                    break;
                                const uint c = firstbitlow(candMask);
                                const uint bit = 1u << c;
                                uint prevS = 0u;
                                InterlockedOr(g_NwbPhysicsUintBuffers[bodyMaskWakeSlot][singleSlot], bit, prevS);
                                if((prevS & bit) == 0u){
                                    chosenColor = c;
                                    break;
                                }
                                knownUsed = prevS | bit;
                            }
                        }
                        emitConstraint = true;
                    }
                }
            }
        }
    }

    if(localTid == 0u){
        s_GroupRawCount = 0u;
        s_GroupRawBase = 0u;
    }
    if(localTid < NWB_PHYSICS_GRAPH_COLOR_COUNT){
        s_ColorCounts[localTid] = 0u;
        s_ColorBases[localTid] = 0u;
    }
    GroupMemoryBarrierWithGroupSync();

    uint localRawOff = 0u;
    if(emitConstraint)
        InterlockedAdd(s_GroupRawCount, 1u, localRawOff);
    GroupMemoryBarrierWithGroupSync();

    if(localTid == 0u && s_GroupRawCount > 0u)
        InterlockedAdd(g_NwbPhysicsUintBuffers[prepCountersSlot][0u], s_GroupRawCount, s_GroupRawBase);
    GroupMemoryBarrierWithGroupSync();

    const uint rawIdx = s_GroupRawBase + localRawOff;
    const bool keepWrite = emitConstraint && (rawIdx < maxPairs);
    uint localColorOff = 0u;
    if(keepWrite)
        InterlockedAdd(s_ColorCounts[chosenColor], 1u, localColorOff);
    GroupMemoryBarrierWithGroupSync();

    if(localTid < NWB_PHYSICS_GRAPH_COLOR_COUNT && s_ColorCounts[localTid] > 0u)
        InterlockedAdd(g_NwbPhysicsUintBuffers[prepCountersSlot][8u + localTid], s_ColorCounts[localTid], s_ColorBases[localTid]);
    GroupMemoryBarrierWithGroupSync();

    if(keepWrite){
        const uint colorLocalIdx = s_ColorBases[chosenColor] + localColorOff;
        g_NwbPhysicsContactConstraintBuffers[rawConstraintSlot][rawIdx] = gc;
        g_NwbPhysicsFloat4Buffers[rawMetaFeatSlot][rawIdx * 2u + 0u] = asfloat(uint4(chosenColor, colorLocalIdx, shapeA, shapeB));
        g_NwbPhysicsFloat4Buffers[rawMetaFeatSlot][rawIdx * 2u + 1u] = asfloat(uint4(featArr[0], featArr[1], featArr[2], featArr[3]));
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

