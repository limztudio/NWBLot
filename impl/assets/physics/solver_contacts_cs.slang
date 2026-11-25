// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "solver_contact_helpers.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_PHYSICS_WORKGROUP_SIZE, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    uint itemCount = g_NwbPhysicsPushConstants.itemCount;
    uint itemOffset = g_NwbPhysicsPushConstants.itemOffset;
    const uint mode = g_NwbPhysicsPushConstants.modeOrSecondaryCount;
    const uint flags = g_NwbPhysicsPushConstants.flags;
    const uint bodySimSlot = g_NwbPhysicsPushConstants.slots0.x;
    const uint bodyStateSlot = g_NwbPhysicsPushConstants.slots0.y;
    const uint constraintSlot = g_NwbPhysicsPushConstants.slots0.w;
    const float invH = g_NwbPhysicsPushConstants.invDeltaTime;

    if((flags & NWB_PHYSICS_SOLVER_FLAG_INDIRECT_SLICE) != 0u){
        const uint sliceMetaSlot = g_NwbPhysicsPushConstants.slots1.w;
        const uint colorIdx = g_NwbPhysicsPushConstants.itemOffset;
        itemOffset = g_NwbPhysicsUintBuffers[sliceMetaSlot][colorIdx * 2u + 0u];
        itemCount = g_NwbPhysicsUintBuffers[sliceMetaSlot][colorIdx * 2u + 1u];
        if(itemCount == 0u)
            return;
    }

    const bool isSerial = (flags & NWB_PHYSICS_SOLVER_FLAG_SERIAL_LOOP) != 0u;
    if(isSerial ? (dispatchThreadID.x != 0u) : (dispatchThreadID.x >= itemCount))
        return;

    const uint startIdx = isSerial ? 0u : dispatchThreadID.x;
    const uint endIdx = isSerial ? itemCount : (dispatchThreadID.x + 1u);
    [loop] for(uint k = startIdx; k < endIdx; ++k)
        nwbPhysicsSolveSingleContact(itemOffset + k, mode, bodySimSlot, bodyStateSlot, constraintSlot, invH, flags, true);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

