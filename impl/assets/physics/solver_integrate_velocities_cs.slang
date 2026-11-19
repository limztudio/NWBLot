// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "physics_common.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_PHYSICS_WORKGROUP_SIZE, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint idx = dispatchThreadID.x;
    if(idx >= g_NwbPhysicsPushConstants.itemCount)
        return;

    const uint bodySimSlot = g_NwbPhysicsPushConstants.slots0.x;
    const uint bodyStateSlot = g_NwbPhysicsPushConstants.slots0.y;
    const float h = g_NwbPhysicsPushConstants.deltaTime;
    const float maxLinearVelocity = g_NwbPhysicsPushConstants.maxLinearVelocity;
    const float3 gravity = g_NwbPhysicsPushConstants.gravitySpeculative.xyz;

    NwbPhysicsGpuBodyState state = g_NwbPhysicsBodyStateBuffers[bodyStateSlot][idx];
    const uint flags = asuint(state.deltaPositionFlags.w);
    if((flags & NWB_PHYSICS_BODY_FLAG_DYNAMIC) == 0u)
        return;

    const NwbPhysicsGpuBodySim sim = g_NwbPhysicsBodySimBuffers[bodySimSlot][idx];
    const float invMass = sim.positionInvMass.w;
    if(invMass <= 0.0f)
        return;

    float3 v = state.linearVelocityPad.xyz;
    float3 w = state.angularVelocityPad.xyz;

    const float3 force = sim.forceLinearDamping.xyz;
    const float linearDamping = sim.forceLinearDamping.w;
    const float3 torque = sim.torqueAngularDamping.xyz;
    const float angularDamping = sim.torqueAngularDamping.w;
    const float gravityScale = sim.gravityScaleFlags.x;

    v += (h * invMass) * force + (h * gravityScale) * gravity;
    v *= 1.0f / (1.0f + h * linearDamping);

    w += h * nwbPhysicsMulMat3Rows(sim.invInertiaWorldRow0.xyz, sim.invInertiaWorldRow1.xyz, sim.invInertiaWorldRow2.xyz, torque);

    if((flags & NWB_PHYSICS_BODY_FLAG_GYROSCOPIC) != 0u){
        const float4 q = sim.rotation;
        const float3 wLocal = nwbPhysicsQuatInvRotate(w, q);
        const float3 row0 = sim.invInertiaLocalRow0.xyz;
        const float3 row1 = sim.invInertiaLocalRow1.xyz;
        const float3 row2 = sim.invInertiaLocalRow2.xyz;
        // Approximate diagonal inertia from inverse local inertia when non-zero.
        const float3 invDiag = float3(row0.x, row1.y, row2.z);
        const float3 diag = float3(
            invDiag.x > 1.0e-12f ? (1.0f / invDiag.x) : 0.0f,
            invDiag.y > 1.0e-12f ? (1.0f / invDiag.y) : 0.0f,
            invDiag.z > 1.0e-12f ? (1.0f / invDiag.z) : 0.0f
        );
        if(diag.x > 0.0f && diag.y > 0.0f && diag.z > 0.0f){
            const float3 Iomega = diag * wLocal;
            const float3 gyroTorqueLocal = -cross(wLocal, Iomega);
            const float3 wNextLocal = wLocal + h * float3(gyroTorqueLocal.x * invDiag.x, gyroTorqueLocal.y * invDiag.y, gyroTorqueLocal.z * invDiag.z);
            w = nwbPhysicsQuatRotate(wNextLocal, q);
        }
    }

    w *= 1.0f / (1.0f + h * angularDamping);

    v = nwbPhysicsApplyLinearLocks(v, flags);
    w = nwbPhysicsApplyAngularLocks(w, flags);

    const float vLenSq = dot(v, v);
    if(vLenSq > maxLinearVelocity * maxLinearVelocity && vLenSq > 1.0e-12f)
        v *= maxLinearVelocity * rsqrt(vLenSq);

    state.linearVelocityPad = float4(v, 0.0f);
    state.angularVelocityPad = float4(w, 0.0f);
    g_NwbPhysicsBodyStateBuffers[bodyStateSlot][idx] = state;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

