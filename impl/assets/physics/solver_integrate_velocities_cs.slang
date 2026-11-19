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

        const float a11 = row0.x;
        const float a12 = row0.y;
        const float a13 = row0.z;
        const float a22 = row1.y;
        const float a23 = row1.z;
        const float a33 = row2.z;
        const float c11 = a22 * a33 - a23 * a23;
        const float c12 = a13 * a23 - a12 * a33;
        const float c13 = a12 * a23 - a13 * a22;
        const float detInv = a11 * c11 + a12 * c12 + a13 * c13;
        if(abs(detInv) > 1.0e-30f){
            const float invDet = 1.0f / detInv;
            const float c22 = a11 * a33 - a13 * a13;
            const float c23 = a12 * a13 - a11 * a23;
            const float c33 = a11 * a22 - a12 * a12;
            const float3 i0 = float3(c11, c12, c13) * invDet;
            const float3 i1 = float3(c12, c22, c23) * invDet;
            const float3 i2 = float3(c13, c23, c33) * invDet;

            const float3 iOmega = float3(dot(i0, wLocal), dot(i1, wLocal), dot(i2, wLocal));
            const float3 residual = cross(wLocal, iOmega) * h;

            const float3 swI0 = -wLocal.z * i1 + wLocal.y * i2;
            const float3 swI1 = wLocal.z * i0 - wLocal.x * i2;
            const float3 swI2 = -wLocal.y * i0 + wLocal.x * i1;
            const float3 J0 = i0 + h * (swI0 - float3(0.0f, -iOmega.z, iOmega.y));
            const float3 J1 = i1 + h * (swI1 - float3(iOmega.z, 0.0f, -iOmega.x));
            const float3 J2 = i2 + h * (swI2 - float3(-iOmega.y, iOmega.x, 0.0f));

            const float3 jc0 = cross(J1, J2);
            const float detJ = dot(J0, jc0);
            if(abs(detJ) > 1.0e-30f){
                const float3 jc1 = cross(J2, J0);
                const float3 jc2 = cross(J0, J1);
                const float3 rhs = -residual;
                const float3 dw = float3(
                    dot(rhs, float3(jc0.x, jc1.x, jc2.x)),
                    dot(rhs, float3(jc0.y, jc1.y, jc2.y)),
                    dot(rhs, float3(jc0.z, jc1.z, jc2.z))
                ) / detJ;
                w = nwbPhysicsQuatRotate(wLocal + dw, q);
            }
        }
    }

    w *= 1.0f / (1.0f + h * angularDamping);

    v = nwbPhysicsApplyLinearLocks(v, flags);
    w = nwbPhysicsApplyAngularLocks(w, flags);

    uint outFlags = flags;
    const float vLenSq = dot(v, v);
    if(vLenSq > maxLinearVelocity * maxLinearVelocity && vLenSq > 1.0e-12f){
        v *= maxLinearVelocity * rsqrt(vLenSq);
        outFlags |= NWB_PHYSICS_BODY_FLAG_IS_SPEED_CAPPED;
    }

    state.linearVelocityPad = float4(v, 0.0f);
    state.angularVelocityPad = float4(w, 0.0f);
    state.deltaPositionFlags.w = asfloat(outFlags);
    g_NwbPhysicsBodyStateBuffers[bodyStateSlot][idx] = state;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

