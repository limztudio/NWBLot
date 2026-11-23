// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "solver_joint_helpers.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_PHYSICS_WORKGROUP_SIZE, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint itemCount = g_NwbPhysicsPushConstants.itemCount;
    const uint itemOffset = g_NwbPhysicsPushConstants.itemOffset;
    const uint mode = g_NwbPhysicsPushConstants.modeOrSecondaryCount;
    const uint flags = g_NwbPhysicsPushConstants.flags;
    const uint bodySimSlot = g_NwbPhysicsPushConstants.slots0.x;
    const uint bodyStateSlot = g_NwbPhysicsPushConstants.slots0.y;
    const uint jointSlot = g_NwbPhysicsPushConstants.slots1.x;
    const uint outSlot = g_NwbPhysicsPushConstants.slots1.z;
    const float h = g_NwbPhysicsPushConstants.deltaTime;

    if((flags & NWB_PHYSICS_SOLVER_FLAG_SERIAL_LOOP) != 0u){
        if(dispatchThreadID.x != 0u)
            return;
        if(mode == NWB_PHYSICS_SOLVER_MODE_EXPORT_IMPULSES){
            for(uint k = 0u; k < itemCount; ++k)
                nwbPhysicsExportJointImpulses(itemOffset + k, jointSlot, outSlot);
            return;
        }
        for(uint k = 0u; k < itemCount; ++k)
            nwbPhysicsSolveSingleJoint(itemOffset + k, mode, bodySimSlot, bodyStateSlot, jointSlot, h, flags);
        return;
    }

    const uint localIdx = dispatchThreadID.x;
    if(localIdx >= itemCount)
        return;
    if(mode == NWB_PHYSICS_SOLVER_MODE_EXPORT_IMPULSES){
        nwbPhysicsExportJointImpulses(itemOffset + localIdx, jointSlot, outSlot);
        return;
    }
    nwbPhysicsSolveSingleJoint(itemOffset + localIdx, mode, bodySimSlot, bodyStateSlot, jointSlot, h, flags);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

