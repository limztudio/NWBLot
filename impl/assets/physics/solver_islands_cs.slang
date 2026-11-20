// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "solver_contact_helpers.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_PHYSICS_ISLAND_WORKGROUP_SIZE, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint localIdx = dispatchThreadID.x;
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

    for(uint subStep = 0u; subStep < subStepCount; ++subStep){
        for(uint b = 0u; b < bodyCount; ++b)
            nwbPhysicsIntegrateBodyVelocity(bodySimSlot, bodyStateSlot, bodyOffset + b, h, maxLinearVelocity, gravity);

        if(warmStart){
            for(uint c = 0u; c < contactCount; ++c)
                nwbPhysicsWarmStartContact(contactOffset + c, bodyStateSlot, constraintSlot, NWB_PHYSICS_SOLVER_FLAG_WARM_START);
        }
        else if(subStep > 0u){
            for(uint c = 0u; c < contactCount; ++c)
                nwbPhysicsClearContactWarmStart(contactOffset + c, constraintSlot);
        }

        for(uint c = 0u; c < contactCount; ++c)
            nwbPhysicsSolveContactVelocity(contactOffset + c, true, bodyStateSlot, constraintSlot, invH);

        for(uint b = 0u; b < bodyCount; ++b)
            nwbPhysicsIntegrateBodyPosition(bodyStateSlot, bodyOffset + b, h);

        for(uint c = 0u; c < contactCount; ++c)
            nwbPhysicsSolveContactVelocity(contactOffset + c, false, bodyStateSlot, constraintSlot, invH);
    }

    if((islandFlags & NWB_PHYSICS_ISLAND_FLAG_HAS_RESTITUTION) != 0u){
        for(uint c = 0u; c < contactCount; ++c)
            nwbPhysicsRestitutionContact(contactOffset + c, bodyStateSlot, constraintSlot);
    }

    const uint impulseOutSlot = g_NwbPhysicsPushConstants.slots1.y;
    for(uint c = 0u; c < contactCount; ++c)
        nwbPhysicsExportContactImpulses(contactOffset + c, constraintSlot, impulseOutSlot);

    for(uint b = 0u; b < bodyCount; ++b)
        nwbPhysicsFinalizeBodyToOut(bodySimSlot, bodyStateSlot, bodyOutSlot, bodyOffset + b, dt);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

