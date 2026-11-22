// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_PHYSICS_USE_LDS_BODY_STATES 1

#include "solver_contact_helpers.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
    const uint islandFlags = slice.jointFlags.z;

    const uint subStepCount = g_NwbPhysicsPushConstants.modeOrSecondaryCount;
    const float dt = g_NwbPhysicsPushConstants.deltaTime;
    if(bodyCount == 0u || contactCount == 0u || subStepCount == 0u || dt <= 0.0f)
        return;

    const float h = dt / float(subStepCount);
    const float invH = g_NwbPhysicsPushConstants.invDeltaTime;
    const float maxLinearVelocity = g_NwbPhysicsPushConstants.maxLinearVelocity;
    const float3 gravity = g_NwbPhysicsPushConstants.gravitySpeculative.xyz;
    const bool warmStart = (g_NwbPhysicsPushConstants.flags & NWB_PHYSICS_SOLVER_FLAG_WARM_START) != 0u;

    const uint bodySimSlot = g_NwbPhysicsPushConstants.slots0.x;
    const uint bodyStateSlot = g_NwbPhysicsPushConstants.slots0.y;
    const uint bodyOutSlot = g_NwbPhysicsPushConstants.slots0.z;
    const uint constraintSlot = g_NwbPhysicsPushConstants.slots0.w;

    const uint tid = groupThreadID.x;
    if(tid == 0u)
        g_NwbPhysicsLdsBodyOffset = bodyOffset;

    NwbPhysicsGpuBodySim mySim = (NwbPhysicsGpuBodySim)0;
    if(tid < bodyCount){
        mySim = g_NwbPhysicsBodySimBuffers[bodySimSlot][bodyOffset + tid];
        g_NwbPhysicsLdsBodyStates[tid] = g_NwbPhysicsBodyStateBuffers[bodyStateSlot][bodyOffset + tid];
    }
    GroupMemoryBarrierWithGroupSync();

    for(uint subStep = 0u; subStep < subStepCount; ++subStep){
        if(tid < bodyCount)
            nwbPhysicsIntegrateBodyVelocityFromSim(mySim, bodyStateSlot, bodyOffset + tid, h, maxLinearVelocity, gravity);
        GroupMemoryBarrierWithGroupSync();

        if(warmStart){
            nwbPhysicsIslandSolveContactsPass(tid, contactOffset, slice.contactColorCounts0, slice.contactColorCounts1, NWB_PHYSICS_SOLVER_MODE_WARM_START, bodyStateSlot, constraintSlot, invH, NWB_PHYSICS_SOLVER_FLAG_WARM_START);
        }
        else if(subStep > 0u){
            for(uint k = tid; k < contactCount; k += NWB_PHYSICS_ISLAND_WORKGROUP_SIZE)
                nwbPhysicsClearContactWarmStart(contactOffset + k, constraintSlot);
            AllMemoryBarrierWithGroupSync();
        }

        nwbPhysicsIslandSolveContactsPass(tid, contactOffset, slice.contactColorCounts0, slice.contactColorCounts1, NWB_PHYSICS_SOLVER_MODE_SOLVE_BIAS, bodyStateSlot, constraintSlot, invH, 0u);

        if(tid < bodyCount)
            nwbPhysicsIntegrateBodyPosition(bodyStateSlot, bodyOffset + tid, h);
        GroupMemoryBarrierWithGroupSync();

        nwbPhysicsIslandSolveContactsPass(tid, contactOffset, slice.contactColorCounts0, slice.contactColorCounts1, NWB_PHYSICS_SOLVER_MODE_RELAX, bodyStateSlot, constraintSlot, invH, 0u);
    }

    if((islandFlags & NWB_PHYSICS_ISLAND_FLAG_HAS_RESTITUTION) != 0u)
        nwbPhysicsIslandSolveContactsPass(tid, contactOffset, slice.contactColorCounts0, slice.contactColorCounts1, NWB_PHYSICS_SOLVER_MODE_RESTITUTION, bodyStateSlot, constraintSlot, invH, 0u);

    const uint impulseOutSlot = g_NwbPhysicsPushConstants.slots1.y;
    AllMemoryBarrierWithGroupSync();
    for(uint k = tid; k < contactCount; k += NWB_PHYSICS_ISLAND_WORKGROUP_SIZE)
        nwbPhysicsExportContactImpulses(contactOffset + k, constraintSlot, impulseOutSlot);

    if(tid < bodyCount){
        g_NwbPhysicsBodyStateBuffers[bodyStateSlot][bodyOffset + tid] = g_NwbPhysicsLdsBodyStates[tid];
        nwbPhysicsFinalizeBodyToOutFromSim(mySim, bodyStateSlot, bodyOutSlot, bodyOffset + tid, dt);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

