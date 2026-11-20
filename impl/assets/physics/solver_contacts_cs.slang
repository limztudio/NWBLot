// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "solver_contact_helpers.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_PHYSICS_WORKGROUP_SIZE, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint itemCount = g_NwbPhysicsPushConstants.itemCount;
    const uint itemOffset = g_NwbPhysicsPushConstants.itemOffset;
    const uint mode = g_NwbPhysicsPushConstants.modeOrSecondaryCount;
    const uint flags = g_NwbPhysicsPushConstants.flags;
    const uint bodyStateSlot = g_NwbPhysicsPushConstants.slots0.y;
    const uint constraintSlot = g_NwbPhysicsPushConstants.slots0.w;
    const float invH = g_NwbPhysicsPushConstants.invDeltaTime;

    if((flags & NWB_PHYSICS_SOLVER_FLAG_SERIAL_LOOP) != 0u){
        if(dispatchThreadID.x != 0u)
            return;
        for(uint k = 0u; k < itemCount; ++k)
            nwbPhysicsSolveSingleContact(itemOffset + k, mode, bodyStateSlot, constraintSlot, invH, flags);
        return;
    }

    const uint localIdx = dispatchThreadID.x;
    if(localIdx >= itemCount)
        return;
    nwbPhysicsSolveSingleContact(itemOffset + localIdx, mode, bodyStateSlot, constraintSlot, invH, flags);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

