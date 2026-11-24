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
    const uint bodySimSlot = g_NwbPhysicsPushConstants.slots0.x;
    const uint bodyStateSlot = g_NwbPhysicsPushConstants.slots0.y;
    const uint constraintSlot = g_NwbPhysicsPushConstants.slots0.w;
    const float invH = g_NwbPhysicsPushConstants.invDeltaTime;

    const bool isSerial = (flags & NWB_PHYSICS_SOLVER_FLAG_SERIAL_LOOP) != 0u;
    if(isSerial ? (dispatchThreadID.x != 0u) : (dispatchThreadID.x >= itemCount))
        return;

    const uint startIdx = isSerial ? 0u : dispatchThreadID.x;
    const uint endIdx = isSerial ? itemCount : (dispatchThreadID.x + 1u);
    for(uint k = startIdx; k < endIdx; ++k)
        nwbPhysicsSolveSingleContact(itemOffset + k, mode, bodySimSlot, bodyStateSlot, constraintSlot, invH, flags, true);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

