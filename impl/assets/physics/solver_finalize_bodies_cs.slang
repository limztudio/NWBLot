// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "physics_common.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void nwbPhysicsBuildMat3FromQuat(const float4 q, out float3 r0, out float3 r1, out float3 r2){
    const float x = q.x;
    const float y = q.y;
    const float z = q.z;
    const float w = q.w;
    const float xx = x * x;
    const float yy = y * y;
    const float zz = z * z;
    const float xy = x * y;
    const float xz = x * z;
    const float yz = y * z;
    const float wx = w * x;
    const float wy = w * y;
    const float wz = w * z;

    r0 = float3(1.0f - 2.0f * (yy + zz), 2.0f * (xy - wz), 2.0f * (xz + wy));
    r1 = float3(2.0f * (xy + wz), 1.0f - 2.0f * (xx + zz), 2.0f * (yz - wx));
    r2 = float3(2.0f * (xz - wy), 2.0f * (yz + wx), 1.0f - 2.0f * (xx + yy));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_PHYSICS_WORKGROUP_SIZE, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint idx = dispatchThreadID.x;
    if(idx >= g_NwbPhysicsPushConstants.itemCount)
        return;

    const uint bodySimSlot = g_NwbPhysicsPushConstants.slots0.x;
    const uint bodyStateSlot = g_NwbPhysicsPushConstants.slots0.y;
    const float dt = g_NwbPhysicsPushConstants.deltaTime;

    NwbPhysicsGpuBodySim sim = g_NwbPhysicsBodySimBuffers[bodySimSlot][idx];
    NwbPhysicsGpuBodyState state = g_NwbPhysicsBodyStateBuffers[bodyStateSlot][idx];

    const float3 newCenter = sim.positionInvMass.xyz + state.deltaPositionFlags.xyz;
    const float4 newRotation = nwbPhysicsQuatNormalize(nwbPhysicsQuatMul(state.deltaRotation, sim.rotation));

    sim.positionInvMass = float4(newCenter, sim.positionInvMass.w);
    sim.rotation = newRotation;
    sim.forceLinearDamping = float4(0.0f, 0.0f, 0.0f, sim.forceLinearDamping.w);
    sim.torqueAngularDamping = float4(0.0f, 0.0f, 0.0f, sim.torqueAngularDamping.w);

    float3 r0;
    float3 r1;
    float3 r2;
    nwbPhysicsBuildMat3FromQuat(newRotation, r0, r1, r2);

    const float3 l0 = sim.invInertiaLocalRow0.xyz;
    const float3 l1 = sim.invInertiaLocalRow1.xyz;
    const float3 l2 = sim.invInertiaLocalRow2.xyz;

    // R * L * R^T, where columns of R^T are r0, r1, r2.
    const float3 lr0 = float3(dot(l0, r0), dot(l0, r1), dot(l0, r2));
    const float3 lr1 = float3(dot(l1, r0), dot(l1, r1), dot(l1, r2));
    const float3 lr2 = float3(dot(l2, r0), dot(l2, r1), dot(l2, r2));

    const float3 w0 = float3(r0.x * lr0.x + r0.y * lr1.x + r0.z * lr2.x, r0.x * lr0.y + r0.y * lr1.y + r0.z * lr2.y, r0.x * lr0.z + r0.y * lr1.z + r0.z * lr2.z);
    const float3 w1 = float3(r1.x * lr0.x + r1.y * lr1.x + r1.z * lr2.x, r1.x * lr0.y + r1.y * lr1.y + r1.z * lr2.y, r1.x * lr0.z + r1.y * lr1.z + r1.z * lr2.z);
    const float3 w2 = float3(r2.x * lr0.x + r2.y * lr1.x + r2.z * lr2.x, r2.x * lr0.y + r2.y * lr1.y + r2.z * lr2.y, r2.x * lr0.z + r2.y * lr1.z + r2.z * lr2.z);

    sim.invInertiaWorldRow0 = float4(w0, sim.invInertiaWorldRow0.w);
    sim.invInertiaWorldRow1 = float4(w1, sim.invInertiaWorldRow1.w);
    sim.invInertiaWorldRow2 = float4(w2, sim.invInertiaWorldRow2.w);

    const float3 v = state.linearVelocityPad.xyz;
    const float3 w = state.angularVelocityPad.xyz;
    const float minExtent = max(sim.invInertiaLocalRow0.w, 1.0e-3f);
    const float linSpeedSq = dot(v, v);
    const float angSpeedSq = dot(w, w) * minExtent * minExtent;
    const float sleepThreshold = sim.gravityScaleFlags.w;
    if(linSpeedSq + angSpeedSq <= sleepThreshold * sleepThreshold)
        sim.gravityScaleFlags.z += dt;
    else
        sim.gravityScaleFlags.z = 0.0f;

    state.deltaPositionFlags = float4(0.0f, 0.0f, 0.0f, state.deltaPositionFlags.w);
    state.deltaRotation = float4(0.0f, 0.0f, 0.0f, 1.0f);

    g_NwbPhysicsBodySimBuffers[bodySimSlot][idx] = sim;
    g_NwbPhysicsBodyStateBuffers[bodyStateSlot][idx] = state;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

