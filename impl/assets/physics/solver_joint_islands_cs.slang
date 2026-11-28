// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_PHYSICS_JOINT_ISLAND_MODE
#define NWB_PHYSICS_JOINT_ISLAND_MODE 0
#endif

#if NWB_PHYSICS_JOINT_ISLAND_MODE == 2
#define NWB_PHYSICS_ENABLE_BLOCK_SOLVER 1
#else
#define NWB_PHYSICS_ENABLE_BLOCK_SOLVER 0
#endif

#define NWB_PHYSICS_USE_LDS_BODY_STATES 1
#define NWB_PHYSICS_USE_LDS_JOINT_IMPULSES 1
#if NWB_PHYSICS_JOINT_ISLAND_MODE != 1
#define NWB_PHYSICS_USE_LDS_CONTACT_IMPULSES 1
#define NWB_PHYSICS_ISLAND_LDS_MAX_CONTACTS NWB_PHYSICS_JOINT_ISLAND_MAX_CONTACTS

#include "solver_contact_helpers.slangi"
#endif
#include "solver_joint_helpers.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


groupshared uint g_NwbPhysicsLdsProjAny;

#define NWB_PHYSICS_JOINT_PASS_WARM_START 0u
#define NWB_PHYSICS_JOINT_PASS_SOLVE_BIAS 1u
#define NWB_PHYSICS_JOINT_PASS_SOLVE_RELAX 2u
#define NWB_PHYSICS_JOINT_PASS_PROJECT 3u

bool nwbPhysicsIslandJointsPass(
    const uint tid,
    const uint jointOffset,
    const uint2 jointCounts,
    const uint denseJointColors,
    const uint passMode,
    const uint bodySimSlot,
    const uint bodyStateSlot,
    const uint jointSlot,
    const float h,
    const uint flags,
    const bool hasJointAux
){
    bool anyProj = false;
    uint off = jointOffset;
    const uint overflowCnt = (jointCounts.y >> 24u) & 0xffu;
    const uint totalSlots = denseJointColors + (overflowCnt > 0u ? 1u : 0u);
    [loop]
    for(uint colorSlot = 0u; colorSlot < totalSlots; ++colorSlot){
        const bool isOverflow = (colorSlot == denseJointColors);
        uint cnt = overflowCnt;
        if(!isOverflow){
            const uint packed = (colorSlot < 4u) ? jointCounts.x : jointCounts.y;
            cnt = (packed >> ((colorSlot & 3u) * 8u)) & 0xffu;
        }
        if(!isOverflow || tid == 0u){
            const uint startIdx = isOverflow ? 0u : tid;
            const uint stepIdx = isOverflow ? 1u : NWB_PHYSICS_ISLAND_WORKGROUP_SIZE;
            for(uint j = startIdx; j < cnt; j += stepIdx){
                if(passMode == NWB_PHYSICS_JOINT_PASS_WARM_START)
                    nwbPhysicsWarmStartSingleJoint(off + j, bodySimSlot, bodyStateSlot, jointSlot, flags);
                else if(passMode == NWB_PHYSICS_JOINT_PASS_SOLVE_BIAS || passMode == NWB_PHYSICS_JOINT_PASS_SOLVE_RELAX)
                    nwbPhysicsSolveSingleJoint(off + j, passMode == NWB_PHYSICS_JOINT_PASS_SOLVE_BIAS, bodySimSlot, bodyStateSlot, jointSlot, h);
                else if(nwbPhysicsProjectSingleJoint(off + j, bodySimSlot, bodyStateSlot, jointSlot))
                    anyProj = true;
            }
        }
        off += cnt;
        if(hasJointAux)
            AllMemoryBarrierWithGroupSync();
        else
            GroupMemoryBarrierWithGroupSync();
    }
    return anyProj;
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
#if NWB_PHYSICS_JOINT_ISLAND_MODE != 1
    const uint contactOffset = slice.bodyContact.z;
    const uint contactCount = slice.bodyContact.w;
    const uint islandFlags = slice.jointFlags.z;
    const uint denseContactColors = min(slice.jointFlags.w & NWB_PHYSICS_ISLAND_DENSE_COLOR_MASK, NWB_PHYSICS_OVERFLOW_COLOR_INDEX);
    const uint overflowContactCount = (slice.jointFlags.w >> NWB_PHYSICS_ISLAND_OVERFLOW_COUNT_SHIFT) & NWB_PHYSICS_ISLAND_OVERFLOW_COUNT_MASK;
    const uint denseJointColors = (islandFlags >> 8u) & NWB_PHYSICS_ISLAND_JOINT_OVERFLOW_COLOR_INDEX;
    const bool hasRolling = (islandFlags & NWB_PHYSICS_ISLAND_FLAG_HAS_ROLLING) != 0u;
    const bool hasJointAux = (islandFlags & NWB_PHYSICS_ISLAND_FLAG_HAS_JOINT_AUX) != 0u;
#else
    const uint islandFlags = slice.jointFlags.z;
    const uint denseJointColors = (islandFlags >> 8u) & NWB_PHYSICS_ISLAND_JOINT_OVERFLOW_COLOR_INDEX;
    const bool hasJointAux = (islandFlags & NWB_PHYSICS_ISLAND_FLAG_HAS_JOINT_AUX) != 0u;
#endif
    const uint jointOffset = slice.jointFlags.x;
    const uint jointCount = slice.jointFlags.y;

    const uint subStepCount = g_NwbPhysicsPushConstants.modeOrSecondaryCount;
    const float dt = g_NwbPhysicsPushConstants.deltaTime;
    if(bodyCount == 0u || subStepCount == 0u || dt <= 0.0f)
        return;

    const float h = dt / float(subStepCount);
    const float invH = g_NwbPhysicsPushConstants.invDeltaTime;
    const float maxAngSpeed = NWB_PHYSICS_MAX_ROTATION * invH;
    const float maxLinearVelocity = g_NwbPhysicsPushConstants.maxLinearVelocity;
    const float3 gravity = g_NwbPhysicsPushConstants.gravitySpeculative.xyz;
    const uint flags = g_NwbPhysicsPushConstants.flags;
    const bool warmStart = (flags & NWB_PHYSICS_SOLVER_FLAG_WARM_START) != 0u;
#if NWB_PHYSICS_JOINT_ISLAND_MODE != 1
    const bool hasHitEvents = (flags & NWB_PHYSICS_SOLVER_FLAG_HAS_HIT_EVENTS) != 0u;
#endif

    const uint bodySimSlot = g_NwbPhysicsPushConstants.slots0.x;
    const uint bodyStateSlot = g_NwbPhysicsPushConstants.slots0.y;
    const uint bodyOutSlot = g_NwbPhysicsPushConstants.slots0.z;
#if NWB_PHYSICS_JOINT_ISLAND_MODE != 1
    const uint constraintSlot = g_NwbPhysicsPushConstants.slots0.w;
#endif
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
#if NWB_PHYSICS_JOINT_ISLAND_MODE != 1
    if(contactCount > 0u)
        nwbPhysicsLoadInitialIslandContactsToLds(tid, contactOffset, contactCount, constraintSlot, warmStart);
#endif
    if(jointCount > 0u)
        nwbPhysicsLoadInitialIslandJointsToLds(tid, jointOffset, jointCount, jointSlot, warmStart);

    for(uint subStep = 0u; subStep < subStepCount; ++subStep){
        if(tid < bodyCount)
            nwbPhysicsIntegrateIslandBodyVelocity(invMassPositive, dvStep, linDampScale, dwStep, angDampScale, bodySimSlot, bodyOffset + tid, tid, h, maxLinearVelocity);
        GroupMemoryBarrierWithGroupSync();

#if NWB_PHYSICS_JOINT_ISLAND_MODE != 1
        if(contactCount > 0u){
            if(warmStart){
                nwbPhysicsIslandSolveContactsPass(tid, contactOffset, slice.contactColorCounts0, slice.contactColorCounts1, denseContactColors, overflowContactCount, NWB_PHYSICS_SOLVER_MODE_WARM_START, bodySimSlot, bodyStateSlot, constraintSlot, invH, NWB_PHYSICS_SOLVER_FLAG_WARM_START, hasRolling);
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
#endif
        if(jointCount > 0u && (warmStart || subStep > 0u)){
            if(warmStart){
                nwbPhysicsIslandJointsPass(tid, jointOffset, slice.contactColorCounts1.zw, denseJointColors, NWB_PHYSICS_JOINT_PASS_WARM_START, bodySimSlot, bodyStateSlot, jointSlot, h, NWB_PHYSICS_SOLVER_FLAG_WARM_START, hasJointAux);
            }
            else{
                nwbPhysicsClearIslandJointWarmStartLds(tid, jointOffset, jointCount, jointSlot, hasJointAux);
                if(hasJointAux)
                    AllMemoryBarrierWithGroupSync();
                else
                    GroupMemoryBarrierWithGroupSync();
            }
        }

#if NWB_PHYSICS_JOINT_ISLAND_MODE != 1
        const uint blockFlag = flags & NWB_PHYSICS_SOLVER_FLAG_BLOCK_SOLVER;
        if(contactCount > 0u)
            nwbPhysicsIslandSolveContactsPass(tid, contactOffset, slice.contactColorCounts0, slice.contactColorCounts1, denseContactColors, overflowContactCount, NWB_PHYSICS_SOLVER_MODE_SOLVE_BIAS, bodySimSlot, bodyStateSlot, constraintSlot, invH, blockFlag, hasRolling);
#endif
        if(jointCount > 0u)
            nwbPhysicsIslandJointsPass(tid, jointOffset, slice.contactColorCounts1.zw, denseJointColors, NWB_PHYSICS_JOINT_PASS_SOLVE_BIAS, bodySimSlot, bodyStateSlot, jointSlot, h, 0u, hasJointAux);
        if(tid < bodyCount)
            nwbPhysicsIntegrateIslandBodyPosition(bodySimSlot, bodyOffset + tid, tid, h, maxAngSpeed);
        GroupMemoryBarrierWithGroupSync();
#if NWB_PHYSICS_JOINT_ISLAND_MODE != 1
        if(contactCount > 0u)
            nwbPhysicsIslandSolveContactsPass(tid, contactOffset, slice.contactColorCounts0, slice.contactColorCounts1, denseContactColors, overflowContactCount, NWB_PHYSICS_SOLVER_MODE_RELAX, bodySimSlot, bodyStateSlot, constraintSlot, invH, blockFlag, hasRolling);
#endif
        if(jointCount > 0u)
            nwbPhysicsIslandJointsPass(tid, jointOffset, slice.contactColorCounts1.zw, denseJointColors, NWB_PHYSICS_JOINT_PASS_SOLVE_RELAX, bodySimSlot, bodyStateSlot, jointSlot, h, 0u, hasJointAux);
    }

#if NWB_PHYSICS_JOINT_ISLAND_MODE != 1
    if(contactCount > 0u && (islandFlags & NWB_PHYSICS_ISLAND_FLAG_HAS_RESTITUTION) != 0u)
        nwbPhysicsIslandSolveContactsPass(tid, contactOffset, slice.contactColorCounts0, slice.contactColorCounts1, denseContactColors, overflowContactCount, NWB_PHYSICS_SOLVER_MODE_RESTITUTION, bodySimSlot, bodyStateSlot, constraintSlot, invH, 0u, hasRolling);

    const uint contactImpulseOutSlot = g_NwbPhysicsPushConstants.slots1.y;
    if(hasRolling)
        AllMemoryBarrierWithGroupSync();
    for(uint k = tid; k < contactCount; k += NWB_PHYSICS_ISLAND_WORKGROUP_SIZE)
        nwbPhysicsExportContactImpulses(contactOffset + k, constraintSlot, contactImpulseOutSlot, hasRolling, hasHitEvents);
#endif
    if(jointCount > 0u && (islandFlags & NWB_PHYSICS_ISLAND_FLAG_HAS_PROJECTION) != 0u){
        for(uint projIter = 0u; projIter < 4u; ++projIter){
            if(tid == 0u)
                g_NwbPhysicsLdsProjAny = 0u;
            GroupMemoryBarrierWithGroupSync();
            const bool localProj = nwbPhysicsIslandJointsPass(tid, jointOffset, slice.contactColorCounts1.zw, denseJointColors, NWB_PHYSICS_JOINT_PASS_PROJECT, bodySimSlot, bodyStateSlot, jointSlot, h, 0u, hasJointAux);
            if(localProj)
                InterlockedOr(g_NwbPhysicsLdsProjAny, 1u);
            if(hasJointAux)
                AllMemoryBarrierWithGroupSync();
            else
                GroupMemoryBarrierWithGroupSync();
            if(g_NwbPhysicsLdsProjAny == 0u)
                break;
        }
    }
    const uint jointImpulseOutSlot = g_NwbPhysicsPushConstants.slots1.z;
    for(uint j = tid; j < jointCount; j += NWB_PHYSICS_ISLAND_WORKGROUP_SIZE)
        nwbPhysicsExportJointImpulses(jointOffset + j, jointSlot, jointImpulseOutSlot);

    if(tid < bodyCount)
        nwbPhysicsFinalizeIslandBodyToOut(bodySimSlot, bodyOutSlot, bodyOffset + tid, tid, dt);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

