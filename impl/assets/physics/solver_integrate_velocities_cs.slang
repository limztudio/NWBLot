// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "solver_body_helpers.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_PHYSICS_WORKGROUP_SIZE, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint localIdx = dispatchThreadID.x;
    if(localIdx >= g_NwbPhysicsPushConstants.itemCount)
        return;

    const uint idx = g_NwbPhysicsPushConstants.itemOffset + localIdx;
    nwbPhysicsIntegrateBodyVelocity(
        g_NwbPhysicsPushConstants.slots0.x,
        g_NwbPhysicsPushConstants.slots0.y,
        idx,
        g_NwbPhysicsPushConstants.deltaTime,
        g_NwbPhysicsPushConstants.maxLinearVelocity,
        g_NwbPhysicsPushConstants.gravitySpeculative.xyz
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

