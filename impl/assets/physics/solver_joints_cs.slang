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

    const bool isSerial = (flags & NWB_PHYSICS_SOLVER_FLAG_SERIAL_LOOP) != 0u;
    if(isSerial ? (dispatchThreadID.x != 0u) : (dispatchThreadID.x >= itemCount))
        return;

    const uint startIdx = isSerial ? 0u : dispatchThreadID.x;
    const uint endIdx = isSerial ? itemCount : (dispatchThreadID.x + 1u);
    for(uint k = startIdx; k < endIdx; ++k){
        const uint idx = itemOffset + k;
        if(mode == NWB_PHYSICS_SOLVER_MODE_EXPORT_IMPULSES)
            nwbPhysicsExportJointImpulses(idx, jointSlot, outSlot);
        else if(mode == NWB_PHYSICS_SOLVER_MODE_WARM_START)
            nwbPhysicsWarmStartSingleJoint(idx, bodySimSlot, bodyStateSlot, jointSlot, flags);
        else if(mode == NWB_PHYSICS_SOLVER_MODE_PROJECT)
            nwbPhysicsProjectSingleJoint(idx, bodySimSlot, bodyStateSlot, jointSlot);
        else
            nwbPhysicsSolveSingleJoint(idx, mode == NWB_PHYSICS_SOLVER_MODE_SOLVE_BIAS, bodySimSlot, bodyStateSlot, jointSlot, h);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

