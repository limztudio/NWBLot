// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_PHYSICS_USE_LDS_BODY_STATES 1
#define NWB_PHYSICS_USE_LDS_CONTACT_IMPULSES 1
#define NWB_PHYSICS_ISLAND_LDS_MAX_CONTACTS NWB_PHYSICS_JOINT_ISLAND_MAX_CONTACTS

#include "solver_contact_helpers.slangi"
#include "solver_joint_helpers.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void nwbPhysicsIslandSolveJointsPass(
    const uint tid,
    const uint jointOffset,
    const uint2 jointCounts,
    const uint denseJointColors,
    const uint mode,
    const uint bodySimSlot,
    const uint bodyStateSlot,
    const uint jointSlot,
    const float h,
    const uint flags
){
    uint off = jointOffset;
    [loop]
    for(uint colorSlot = 0u; colorSlot < denseJointColors; ++colorSlot){
        const uint packed = (colorSlot < 4u) ? jointCounts.x : jointCounts.y;
        const uint cnt = (packed >> ((colorSlot & 3u) * 8u)) & 0xffu;
        for(uint j = tid; j < cnt; j += NWB_PHYSICS_ISLAND_WORKGROUP_SIZE)
            nwbPhysicsSolveSingleJoint(off + j, mode, bodySimSlot, bodyStateSlot, jointSlot, h, flags);
        off += cnt;
        GroupMemoryBarrierWithGroupSync();
    }
    const uint overflowCnt = (jointCounts.y >> 24u) & 0xffu;
    if(overflowCnt > 0u){
        if(tid == 0u){
            for(uint j = 0u; j < overflowCnt; ++j)
                nwbPhysicsSolveSingleJoint(off + j, mode, bodySimSlot, bodyStateSlot, jointSlot, h, flags);
        }
        GroupMemoryBarrierWithGroupSync();
    }
}

[numthreads(NWB_PHYSICS_ISLAND_WORKGROUP_SIZE, 1, 1)]
void main(uint3 groupID : SV_GroupID, uint3 groupThreadID : SV_GroupThreadID){
    const uint localIdx = groupID.x;
    if(localIdx >= g_NwbPhysicsPushConstants.itemCount)
        return;

    const uint islandSliceSlot = g_NwbPhysicsPushConstants.slots1.w;
    const NwbPhysicsGpuIslandSlice slice = g_NwbPhysicsIslandSliceBuffers[islandSliceSlot][g_NwbPhysicsPushConstants.itemOffset + localIdx];

    const uint bodyOffset = slice.bodyContact.x;
    const uint bodyCount = slice.bodyContact.y;
    const uint contactOffset = slice.bodyContact.z;
    const uint contactCount = slice.bodyContact.w;
    const uint jointOffset = slice.jointFlags.x;
    const uint jointCount = slice.jointFlags.y;
    const uint islandFlags = slice.jointFlags.z;
    const uint denseContactColors = min(slice.jointFlags.w, NWB_PHYSICS_OVERFLOW_COLOR_INDEX);
    const uint denseJointColors = (islandFlags >> 8u) & NWB_PHYSICS_ISLAND_JOINT_OVERFLOW_COLOR_INDEX;
    const bool hasRolling = (islandFlags & NWB_PHYSICS_ISLAND_FLAG_HAS_ROLLING) != 0u;

    const uint subStepCount = g_NwbPhysicsPushConstants.modeOrSecondaryCount;
    const float dt = g_NwbPhysicsPushConstants.deltaTime;
    if(bodyCount == 0u || subStepCount == 0u || dt <= 0.0f)
        return;

    const float h = dt / float(subStepCount);
    const float invH = g_NwbPhysicsPushConstants.invDeltaTime;
    const float maxAngSpeed = NWB_PHYSICS_MAX_ROTATION * invH;
    const float maxLinearVelocity = g_NwbPhysicsPushConstants.maxLinearVelocity;
    const float3 gravity = g_NwbPhysicsPushConstants.gravitySpeculative.xyz;
    const bool warmStart = (g_NwbPhysicsPushConstants.flags & NWB_PHYSICS_SOLVER_FLAG_WARM_START) != 0u;

    const uint bodySimSlot = g_NwbPhysicsPushConstants.slots0.x;
    const uint bodyStateSlot = g_NwbPhysicsPushConstants.slots0.y;
    const uint bodyOutSlot = g_NwbPhysicsPushConstants.slots0.z;
    const uint constraintSlot = g_NwbPhysicsPushConstants.slots0.w;
    const uint jointSlot = g_NwbPhysicsPushConstants.slots1.x;

    const uint tid = groupThreadID.x;
    if(tid == 0u)
        g_NwbPhysicsLdsBodyOffset = bodyOffset;

    bool invMassPositive = false;
    float3 dvStep = float3(0.0f, 0.0f, 0.0f);
    float linDampScale = 1.0f;
    float3 dwStep = float3(0.0f, 0.0f, 0.0f);
    float angDampScale = 1.0f;
    if(tid < bodyCount)
        nwbPhysicsLoadInitialIslandBodyToLds(bodySimSlot, bodyStateSlot, bodyOffset + tid, tid, h, gravity, invMassPositive, dvStep, linDampScale, dwStep, angDampScale);
    if(contactCount > 0u)
        nwbPhysicsLoadInitialIslandContactsToLds(tid, contactOffset, contactCount, constraintSlot, warmStart);
    GroupMemoryBarrierWithGroupSync();

    for(uint subStep = 0u; subStep < subStepCount; ++subStep){
        if(tid < bodyCount)
            nwbPhysicsIntegrateIslandBodyVelocity(invMassPositive, dvStep, linDampScale, dwStep, angDampScale, bodySimSlot, bodyOffset + tid, tid, h, maxLinearVelocity);
        GroupMemoryBarrierWithGroupSync();

        if(contactCount > 0u){
            if(warmStart){
                nwbPhysicsIslandSolveContactsPass(tid, contactOffset, slice.contactColorCounts0, slice.contactColorCounts1, denseContactColors, NWB_PHYSICS_SOLVER_MODE_WARM_START, bodySimSlot, bodyStateSlot, constraintSlot, invH, NWB_PHYSICS_SOLVER_FLAG_WARM_START);
            }
            else if(subStep > 0u){
                if(hasRolling)
                    AllMemoryBarrierWithGroupSync();
                nwbPhysicsClearIslandContactWarmStartLds(tid, contactOffset, contactCount, constraintSlot, hasRolling);
                if(hasRolling)
                    AllMemoryBarrierWithGroupSync();
                else
                    GroupMemoryBarrierWithGroupSync();
            }
        }
        if(jointCount > 0u && (warmStart || subStep > 0u)){
            if(warmStart){
                nwbPhysicsIslandSolveJointsPass(tid, jointOffset, slice.contactColorCounts1.zw, denseJointColors, NWB_PHYSICS_SOLVER_MODE_WARM_START, bodySimSlot, bodyStateSlot, jointSlot, h, NWB_PHYSICS_SOLVER_FLAG_WARM_START);
            }
            else{
                AllMemoryBarrierWithGroupSync();
                for(uint j = tid; j < jointCount; j += NWB_PHYSICS_ISLAND_WORKGROUP_SIZE)
                    nwbPhysicsSolveSingleJoint(jointOffset + j, NWB_PHYSICS_SOLVER_MODE_WARM_START, bodySimSlot, bodyStateSlot, jointSlot, h, 0u);
                AllMemoryBarrierWithGroupSync();
            }
        }

        if(contactCount > 0u)
            nwbPhysicsIslandSolveContactsPass(tid, contactOffset, slice.contactColorCounts0, slice.contactColorCounts1, denseContactColors, NWB_PHYSICS_SOLVER_MODE_SOLVE_BIAS, bodySimSlot, bodyStateSlot, constraintSlot, invH, 0u);
        if(jointCount > 0u)
            nwbPhysicsIslandSolveJointsPass(tid, jointOffset, slice.contactColorCounts1.zw, denseJointColors, NWB_PHYSICS_SOLVER_MODE_SOLVE_BIAS, bodySimSlot, bodyStateSlot, jointSlot, h, 0u);

        if(tid < bodyCount)
            nwbPhysicsIntegrateIslandBodyPosition(tid, h, maxAngSpeed);
        GroupMemoryBarrierWithGroupSync();

        if(contactCount > 0u)
            nwbPhysicsIslandSolveContactsPass(tid, contactOffset, slice.contactColorCounts0, slice.contactColorCounts1, denseContactColors, NWB_PHYSICS_SOLVER_MODE_RELAX, bodySimSlot, bodyStateSlot, constraintSlot, invH, 0u);
        if(jointCount > 0u)
            nwbPhysicsIslandSolveJointsPass(tid, jointOffset, slice.contactColorCounts1.zw, denseJointColors, NWB_PHYSICS_SOLVER_MODE_RELAX, bodySimSlot, bodyStateSlot, jointSlot, h, 0u);
    }

    if(contactCount > 0u && (islandFlags & NWB_PHYSICS_ISLAND_FLAG_HAS_RESTITUTION) != 0u)
        nwbPhysicsIslandSolveContactsPass(tid, contactOffset, slice.contactColorCounts0, slice.contactColorCounts1, denseContactColors, NWB_PHYSICS_SOLVER_MODE_RESTITUTION, bodySimSlot, bodyStateSlot, constraintSlot, invH, 0u);

    const uint contactImpulseOutSlot = g_NwbPhysicsPushConstants.slots1.y;
    const uint jointImpulseOutSlot = g_NwbPhysicsPushConstants.slots1.z;
    if(jointCount > 0u || hasRolling)
        AllMemoryBarrierWithGroupSync();
    for(uint k = tid; k < contactCount; k += NWB_PHYSICS_ISLAND_WORKGROUP_SIZE)
        nwbPhysicsExportContactImpulses(contactOffset + k, constraintSlot, contactImpulseOutSlot, hasRolling);
    for(uint j = tid; j < jointCount; j += NWB_PHYSICS_ISLAND_WORKGROUP_SIZE)
        nwbPhysicsExportJointImpulses(jointOffset + j, jointSlot, jointImpulseOutSlot);

    if(tid < bodyCount)
        nwbPhysicsFinalizeIslandBodyToOut(bodySimSlot, bodyOutSlot, bodyOffset + tid, tid, dt);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

