// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "physics_common.slangi"
#include "solver_body_helpers.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_PHYSICS_WORKGROUP_SIZE, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint tid = dispatchThreadID.x;
    if(tid >= g_NwbPhysicsPushConstants.itemCount)
        return;

    const uint idx = g_NwbPhysicsPushConstants.itemOffset + tid;
    const uint mode = g_NwbPhysicsPushConstants.modeOrSecondaryCount;
    const uint bodySimSlot = g_NwbPhysicsPushConstants.slots0.x;
    const uint bodyStateSlot = g_NwbPhysicsPushConstants.slots0.y;
    const uint bodyOutSlot = g_NwbPhysicsPushConstants.slots0.z;
    const uint bodyPoseSlot = g_NwbPhysicsPushConstants.slots0.w;
    const uint poseOffset = g_NwbPhysicsPushConstants.slots1.x;

    if(mode == NWB_PHYSICS_RESIDENT_MODE_APPLY_BULLET_TOI){
        const uint toiManifoldSlot = g_NwbPhysicsPushConstants.slots0.w;
        const uint bulletMetaSlot = g_NwbPhysicsPushConstants.slots1.x;
        const uint shapeMatSlot = g_NwbPhysicsPushConstants.slots1.y;
        const float dt = g_NwbPhysicsPushConstants.deltaTime;
        const uint resSlot = g_NwbPhysicsUintBuffers[bulletMetaSlot][idx * 4u + 0u];
        const uint pairStart = g_NwbPhysicsUintBuffers[bulletMetaSlot][idx * 4u + 1u];
        const uint pairCount = g_NwbPhysicsUintBuffers[bulletMetaSlot][idx * 4u + 2u];
        const uint shapeAIdx = g_NwbPhysicsUintBuffers[bulletMetaSlot][idx * 4u + 3u];

        float bestT = 1.0f;
        float3 bestN = float3(0.0f, 1.0f, 0.0f);
        uint bestShapeB = NWB_PHYSICS_INVALID_INDEX;
        bool found = false;
        [loop] for(uint pi = 0u; pi < pairCount; ++pi){
            const NwbPhysicsGpuNarrowphaseManifold gm = g_NwbPhysicsNarrowphaseManifoldBuffers[toiManifoldSlot][pairStart + pi];
            if(gm.pairInfo.z > 0u && gm.pairInfo.w == 0u && gm.normalPad.w > 1.0e-5f && gm.normalPad.w < bestT){
                bestT = gm.normalPad.w;
                bestN = nwbPhysicsSafeNormal(gm.normalPad.xyz, float3(0.0f, 1.0f, 0.0f));
                bestShapeB = gm.pairInfo.y;
                found = true;
            }
        }
        if(!found)
            return;

        NwbPhysicsGpuBodyOut boToi = g_NwbPhysicsBodyOutBuffers[bodyOutSlot][resSlot];
        const uint toiShapeSlot = g_NwbPhysicsPushConstants.slots1.z;
        if(toiShapeSlot != NWB_PHYSICS_INVALID_INDEX && bestShapeB != NWB_PHYSICS_INVALID_INDEX)
            bestShapeB = g_NwbPhysicsShapeProxyBuffers[toiShapeSlot][bestShapeB].bodyIndexTypeShape.z;
        float restitution = 0.0f;
        if(shapeMatSlot != NWB_PHYSICS_INVALID_INDEX && shapeAIdx != NWB_PHYSICS_INVALID_INDEX && bestShapeB != NWB_PHYSICS_INVALID_INDEX){
            const float restA = g_NwbPhysicsShapeMaterialBuffers[shapeMatSlot][shapeAIdx].fricRestStatRoll.y;
            const float restB = g_NwbPhysicsShapeMaterialBuffers[shapeMatSlot][bestShapeB].fricRestStatRoll.y;
            restitution = max(max(restA, restB), 0.0f);
        }
        float3 vToi = boToi.linearVelocityFlags.xyz;
        const float3 wToi = boToi.angularVelocityPad.xyz;
        const float3 c0 = boToi.positionSleepTime.xyz - vToi * dt;
        const float4 q1Raw = boToi.rotation;
        const float4 q0 = nwbPhysicsIntegrateDeltaQuat(q1Raw, -wToi, dt);
        const float4 q1 = (dot(q0, q1Raw) < 0.0f) ? -q1Raw : q1Raw;
        const float3 cHit = lerp(c0, boToi.positionSleepTime.xyz, bestT);
        const float4 qHit = nwbPhysicsQuatNormalize(lerp(q0, q1, bestT));
        const float vn = dot(vToi, bestN);
        if(vn > 0.0f)
            vToi -= (1.0f + restitution) * vn * bestN;
        const float remDt = max((1.0f - bestT) * dt, 0.0f);
        const float3 cNew = cHit + vToi * remDt;
        const float4 qNew = nwbPhysicsIntegrateDeltaQuat(qHit, wToi, remDt);
        boToi.positionSleepTime = float4(cNew, 0.0f);
        boToi.rotation = qNew;
        boToi.linearVelocityFlags.xyz = vToi;
        g_NwbPhysicsBodyOutBuffers[bodyOutSlot][resSlot] = boToi;
        g_NwbPhysicsBodySimBuffers[bodySimSlot][resSlot].positionInvMass.xyz = cNew;
        g_NwbPhysicsBodySimBuffers[bodySimSlot][resSlot].rotation = qNew;
        g_NwbPhysicsBodyStateBuffers[bodyStateSlot][resSlot].linearVelocityPad.xyz = vToi;
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
    }

    uint bodyFlags = asuint(bo.linearVelocityFlags.w);
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
        if(eventRingSlot != NWB_PHYSICS_INVALID_INDEX && prepCounterSlot != NWB_PHYSICS_INVALID_INDEX && maxEvents > 0u){
            uint evIdx = 0u;
            InterlockedAdd(g_NwbPhysicsUintBuffers[prepCounterSlot][1u], 1u, evIdx);
            if(evIdx < maxEvents){
                NwbPhysicsGpuEvent ev;
                ev.header = uint4(NWB_PHYSICS_GPU_EVENT_BODY_SLEEP, idx, 0u, 0u);
                ev.pointImpulse = float4(bo.positionSleepTime.xyz, bo.positionSleepTime.w);
                ev.normalApproach = float4(0.0f, 1.0f, 0.0f, 0.0f);
                g_NwbPhysicsEventBuffers[eventRingSlot][evIdx] = ev;
            }
        }
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

