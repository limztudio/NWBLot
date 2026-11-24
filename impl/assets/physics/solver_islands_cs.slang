// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_PHYSICS_ISLAND_TIER
#define NWB_PHYSICS_ISLAND_TIER 0
#endif

#define NWB_PHYSICS_USE_LDS_BODY_STATES 1
#define NWB_PHYSICS_USE_LDS_CONTACT_IMPULSES 1
#if NWB_PHYSICS_ISLAND_TIER == 1
#define NWB_PHYSICS_ISLAND_LDS_MAX_BODIES NWB_PHYSICS_LARGE_ISLAND_MAX_BODIES
#define NWB_PHYSICS_ISLAND_LDS_MAX_CONTACTS NWB_PHYSICS_LARGE_ISLAND_MAX_CONTACTS
#define NWB_PHYSICS_ISLAND_ACTIVE_WG_SIZE NWB_PHYSICS_LARGE_ISLAND_WORKGROUP_SIZE
#else
#define NWB_PHYSICS_ISLAND_LDS_MAX_BODIES NWB_PHYSICS_ISLAND_MAX_BODIES
#define NWB_PHYSICS_ISLAND_LDS_MAX_CONTACTS NWB_PHYSICS_ISLAND_MAX_CONTACTS
#define NWB_PHYSICS_ISLAND_ACTIVE_WG_SIZE NWB_PHYSICS_ISLAND_WORKGROUP_SIZE
#endif

#include "solver_contact_helpers.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_PHYSICS_ISLAND_ACTIVE_WG_SIZE, 1, 1)]
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
    const uint islandFlags = slice.jointFlags.z;
    const uint denseColorCount = min(slice.jointFlags.w & NWB_PHYSICS_ISLAND_DENSE_COLOR_MASK, NWB_PHYSICS_OVERFLOW_COLOR_INDEX);
    const uint overflowContactCount = (slice.jointFlags.w >> NWB_PHYSICS_ISLAND_OVERFLOW_COUNT_SHIFT) & NWB_PHYSICS_ISLAND_OVERFLOW_COUNT_MASK;
    const bool hasRolling = (islandFlags & NWB_PHYSICS_ISLAND_FLAG_HAS_ROLLING) != 0u;

    const uint subStepCount = g_NwbPhysicsPushConstants.modeOrSecondaryCount;
    const float dt = g_NwbPhysicsPushConstants.deltaTime;
    if(bodyCount == 0u || contactCount == 0u || subStepCount == 0u || dt <= 0.0f)
        return;

    const float h = dt / float(subStepCount);
    const float invH = g_NwbPhysicsPushConstants.invDeltaTime;
    const float maxAngSpeed = NWB_PHYSICS_MAX_ROTATION * invH;
    const float maxLinearVelocity = g_NwbPhysicsPushConstants.maxLinearVelocity;
    const float3 gravity = g_NwbPhysicsPushConstants.gravitySpeculative.xyz;
    const uint flags = g_NwbPhysicsPushConstants.flags;
    const bool warmStart = (flags & NWB_PHYSICS_SOLVER_FLAG_WARM_START) != 0u;
    const bool hasHitEvents = (flags & NWB_PHYSICS_SOLVER_FLAG_HAS_HIT_EVENTS) != 0u;

    const uint bodySimSlot = g_NwbPhysicsPushConstants.slots0.x;
    const uint bodyStateSlot = g_NwbPhysicsPushConstants.slots0.y;
    const uint bodyOutSlot = g_NwbPhysicsPushConstants.slots0.z;
    const uint constraintSlot = g_NwbPhysicsPushConstants.slots0.w;

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
    nwbPhysicsLoadInitialIslandContactsToLds(tid, contactOffset, contactCount, constraintSlot, warmStart);

    for(uint subStep = 0u; subStep < subStepCount; ++subStep){
        if(tid < bodyCount)
            nwbPhysicsIntegrateIslandBodyVelocity(invMassPositive, dvStep, linDampScale, dwStep, angDampScale, bodySimSlot, bodyOffset + tid, tid, h, maxLinearVelocity);
        GroupMemoryBarrierWithGroupSync();

        if(warmStart){
            nwbPhysicsIslandSolveContactsPass(tid, contactOffset, slice.contactColorCounts0, slice.contactColorCounts1, denseColorCount, overflowContactCount, NWB_PHYSICS_SOLVER_MODE_WARM_START, bodySimSlot, bodyStateSlot, constraintSlot, invH, NWB_PHYSICS_SOLVER_FLAG_WARM_START, hasRolling);
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

        nwbPhysicsIslandSolveContactsPass(tid, contactOffset, slice.contactColorCounts0, slice.contactColorCounts1, denseColorCount, overflowContactCount, NWB_PHYSICS_SOLVER_MODE_SOLVE_BIAS, bodySimSlot, bodyStateSlot, constraintSlot, invH, 0u, hasRolling);
        if(tid < bodyCount)
            nwbPhysicsIntegrateIslandBodyPosition(bodySimSlot, bodyOffset + tid, tid, h, maxAngSpeed);
        GroupMemoryBarrierWithGroupSync();
        nwbPhysicsIslandSolveContactsPass(tid, contactOffset, slice.contactColorCounts0, slice.contactColorCounts1, denseColorCount, overflowContactCount, NWB_PHYSICS_SOLVER_MODE_RELAX, bodySimSlot, bodyStateSlot, constraintSlot, invH, 0u, hasRolling);
    }

    if((islandFlags & NWB_PHYSICS_ISLAND_FLAG_HAS_RESTITUTION) != 0u)
        nwbPhysicsIslandSolveContactsPass(tid, contactOffset, slice.contactColorCounts0, slice.contactColorCounts1, denseColorCount, overflowContactCount, NWB_PHYSICS_SOLVER_MODE_RESTITUTION, bodySimSlot, bodyStateSlot, constraintSlot, invH, 0u, hasRolling);

    const uint impulseOutSlot = g_NwbPhysicsPushConstants.slots1.y;
    if(hasRolling)
        AllMemoryBarrierWithGroupSync();
    for(uint k = tid; k < contactCount; k += NWB_PHYSICS_ISLAND_ACTIVE_WG_SIZE)
        nwbPhysicsExportContactImpulses(contactOffset + k, constraintSlot, impulseOutSlot, hasRolling, hasHitEvents);

    if(tid < bodyCount)
        nwbPhysicsFinalizeIslandBodyToOut(bodySimSlot, bodyOutSlot, bodyOffset + tid, tid, dt);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

