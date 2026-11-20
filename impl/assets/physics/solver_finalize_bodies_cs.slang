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
    const uint subStepCount = g_NwbPhysicsPushConstants.modeOrSecondaryCount;
    const float dt = g_NwbPhysicsPushConstants.deltaTime;
    if(subStepCount > 0u && dt > 0.0f){
        const float h = dt / float(subStepCount);
        nwbPhysicsSolveFreeBodyToOut(
            g_NwbPhysicsPushConstants.slots0.x,
            g_NwbPhysicsPushConstants.slots0.y,
            g_NwbPhysicsPushConstants.slots0.z,
            idx,
            dt,
            subStepCount,
            h,
            g_NwbPhysicsPushConstants.maxLinearVelocity,
            g_NwbPhysicsPushConstants.gravitySpeculative.xyz
        );
        return;
    }

    nwbPhysicsFinalizeBodyToOut(
        g_NwbPhysicsPushConstants.slots0.x,
        g_NwbPhysicsPushConstants.slots0.y,
        g_NwbPhysicsPushConstants.slots0.z,
        idx,
        dt
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

