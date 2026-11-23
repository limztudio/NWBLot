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

    if(mode == NWB_PHYSICS_RESIDENT_MODE_INTEGRATE_SUBSTEP){
        const uint subStepCount = max(g_NwbPhysicsPushConstants.flags, 1u);
        const float dt = g_NwbPhysicsPushConstants.deltaTime;
        const float h = dt / float(subStepCount);
        nwbPhysicsSolveFreeBodyToOut(
            bodySimSlot,
            bodyStateSlot,
            bodyOutSlot,
            idx,
            dt,
            subStepCount,
            h,
            g_NwbPhysicsPushConstants.maxLinearVelocity,
            g_NwbPhysicsPushConstants.gravitySpeculative.xyz
        );
    }

    const NwbPhysicsGpuBodyOut bo = g_NwbPhysicsBodyOutBuffers[bodyOutSlot][idx];
    NwbPhysicsGpuBodySim sim = g_NwbPhysicsBodySimBuffers[bodySimSlot][idx];
    sim.positionInvMass.xyz = bo.positionSleepTime.xyz;
    sim.rotation = bo.rotation;
    sim.forceLinearDamping.xyz = float3(0.0f, 0.0f, 0.0f);
    sim.torqueAngularDamping.xyz = float3(0.0f, 0.0f, 0.0f);

    float4 wI0 = sim.invInertiaWorld0;
    float2 wI1 = sim.invInertiaWorld1.xy;
    nwbPhysicsRotateSymMat3(bo.rotation, sim.invInertiaLocal0, sim.invInertiaLocal1.xy, wI0, wI1);
    sim.invInertiaWorld0 = wI0;
    sim.invInertiaWorld1 = float4(wI1.x, wI1.y, sim.invInertiaWorld1.z, bo.positionSleepTime.w);
    g_NwbPhysicsBodySimBuffers[bodySimSlot][idx] = sim;

    NwbPhysicsGpuBodyState nextState;
    nextState.linearVelocityPad = float4(bo.linearVelocityFlags.xyz, bo.linearVelocityFlags.w);
    nextState.angularVelocityPad = float4(bo.angularVelocityPad.xyz, 0.0f);
    nextState.deltaPositionFlags = float4(0.0f, 0.0f, 0.0f, bo.linearVelocityFlags.w);
    nextState.deltaRotation = float4(0.0f, 0.0f, 0.0f, 1.0f);
    g_NwbPhysicsBodyStateBuffers[bodyStateSlot][idx] = nextState;

    if(bodyPoseSlot != NWB_PHYSICS_INVALID_INDEX){
        NwbPhysicsGpuBodyPose pose;
        const float3 origin = bo.positionSleepTime.xyz - nwbPhysicsQuatRotate(sim.localCenterMinExtent.xyz, bo.rotation);
        pose.originPad = float4(origin, 0.0f);
        pose.rotation = bo.rotation;
        pose.sweepDispFlags = float4(0.0f, 0.0f, 0.0f, 0.0f);
        g_NwbPhysicsBodyPoseBuffers[bodyPoseSlot][poseOffset + idx] = pose;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
