// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "physics_common.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_PHYSICS_WORKGROUP_SIZE, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint idx = dispatchThreadID.x;
    if(idx >= g_NwbPhysicsPushConstants.itemCount)
        return;

    const uint bodyStateSlot = g_NwbPhysicsPushConstants.slots0.y;
    const float h = g_NwbPhysicsPushConstants.deltaTime;

    NwbPhysicsGpuBodyState state = g_NwbPhysicsBodyStateBuffers[bodyStateSlot][idx];
    const uint flags = asuint(state.deltaPositionFlags.w);
    const float3 v = nwbPhysicsApplyLinearLocks(state.linearVelocityPad.xyz, flags);
    const float3 w = nwbPhysicsApplyAngularLocks(state.angularVelocityPad.xyz, flags);

    const float3 deltaP = state.deltaPositionFlags.xyz + h * v;
    const float4 deltaQ = nwbPhysicsIntegrateDeltaQuat(state.deltaRotation, w, h);

    state.deltaPositionFlags = float4(deltaP, state.deltaPositionFlags.w);
    state.deltaRotation = deltaQ;
    g_NwbPhysicsBodyStateBuffers[bodyStateSlot][idx] = state;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

