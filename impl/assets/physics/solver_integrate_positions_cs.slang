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
    float3 w = nwbPhysicsApplyAngularLocks(state.angularVelocityPad.xyz, flags);
    if((flags & NWB_PHYSICS_BODY_FLAG_ALLOW_FAST_ROTATION) == 0u && h > 0.0f){
        const float wLenSq = dot(w, w);
        const float maxAngSpeed = NWB_PHYSICS_MAX_ROTATION / h;
        if(wLenSq > maxAngSpeed * maxAngSpeed && wLenSq > 1.0e-12f)
            w *= maxAngSpeed * rsqrt(wLenSq);
    }

    const float3 deltaP = state.deltaPositionFlags.xyz + h * v;
    const float4 deltaQ = nwbPhysicsIntegrateDeltaQuat(state.deltaRotation, w, h);

    state.deltaPositionFlags = float4(deltaP, state.deltaPositionFlags.w);
    state.deltaRotation = deltaQ;
    g_NwbPhysicsBodyStateBuffers[bodyStateSlot][idx] = state;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

