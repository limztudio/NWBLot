// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "physics_common.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void nwbPhysicsLoadBodyState(
    const uint bodyStateSlot,
    const uint bodyIdx,
    out float3 outV,
    out float3 outW,
    out float3 outDeltaPos,
    out float4 outDeltaRot,
    out uint outFlags
){
    if(bodyIdx == NWB_PHYSICS_INVALID_INDEX){
        outV = float3(0.0f, 0.0f, 0.0f);
        outW = float3(0.0f, 0.0f, 0.0f);
        outDeltaPos = float3(0.0f, 0.0f, 0.0f);
        outDeltaRot = float4(0.0f, 0.0f, 0.0f, 1.0f);
        outFlags = 0u;
        return;
    }
    const NwbPhysicsGpuBodyState s = g_NwbPhysicsBodyStateBuffers[bodyStateSlot][bodyIdx];
    outV = s.linearVelocityPad.xyz;
    outW = s.angularVelocityPad.xyz;
    outDeltaPos = s.deltaPositionFlags.xyz;
    outDeltaRot = s.deltaRotation;
    outFlags = asuint(s.deltaPositionFlags.w);
}

void nwbPhysicsStoreBodyVelocity(const uint bodyStateSlot, const uint bodyIdx, const float3 v, const float3 w, const uint flags){
    if(bodyIdx == NWB_PHYSICS_INVALID_INDEX || (flags & NWB_PHYSICS_BODY_FLAG_DYNAMIC) == 0u)
        return;
    const float3 maskedV = nwbPhysicsApplyLinearLocks(v, flags);
    const float3 maskedW = nwbPhysicsApplyAngularLocks(w, flags);
    g_NwbPhysicsBodyStateBuffers[bodyStateSlot][bodyIdx].linearVelocityPad = float4(maskedV, 0.0f);
    g_NwbPhysicsBodyStateBuffers[bodyStateSlot][bodyIdx].angularVelocityPad = float4(maskedW, 0.0f);
}

void nwbPhysicsApplyPointImpulse(
    const float3 P,
    const float3 rA,
    const float3 rB,
    const float invMassA,
    const float invMassB,
    const float3 invIA0,
    const float3 invIA1,
    const float3 invIA2,
    const float3 invIB0,
    const float3 invIB1,
    const float3 invIB2,
    inout float3 vA,
    inout float3 wA,
    inout float3 vB,
    inout float3 wB
){
    vA -= invMassA * P;
    wA -= nwbPhysicsMulMat3Rows(invIA0, invIA1, invIA2, cross(rA, P));
    vB += invMassB * P;
    wB += nwbPhysicsMulMat3Rows(invIB0, invIB1, invIB2, cross(rB, P));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_PHYSICS_WORKGROUP_SIZE, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint localIdx = dispatchThreadID.x;
    if(localIdx >= g_NwbPhysicsPushConstants.itemCount)
        return;

    const uint constraintIdx = g_NwbPhysicsPushConstants.itemOffset + localIdx;
    const uint mode = g_NwbPhysicsPushConstants.modeOrSecondaryCount;
    const uint bodyStateSlot = g_NwbPhysicsPushConstants.slots0.y;
    const uint constraintSlot = g_NwbPhysicsPushConstants.slots0.w;
    const float invH = g_NwbPhysicsPushConstants.invDeltaTime;

    NwbPhysicsGpuContactConstraint c = g_NwbPhysicsContactConstraintBuffers[constraintSlot][constraintIdx];
    const uint indexA = c.indicesCountFlags.x;
    const uint indexB = c.indicesCountFlags.y;
    const uint pointCount = min(c.indicesCountFlags.z, NWB_PHYSICS_MAX_MANIFOLD_POINTS);
    if(pointCount == 0u)
        return;

    float3 vA;
    float3 wA;
    float3 deltaPosA;
    float4 deltaRotA;
    uint flagsA;
    nwbPhysicsLoadBodyState(bodyStateSlot, indexA, vA, wA, deltaPosA, deltaRotA, flagsA);

    float3 vB;
    float3 wB;
    float3 deltaPosB;
    float4 deltaRotB;
    uint flagsB;
    nwbPhysicsLoadBodyState(bodyStateSlot, indexB, vB, wB, deltaPosB, deltaRotB, flagsB);

    const float3 normal = c.normalFriction.xyz;
    const float friction = c.normalFriction.w;
    const float3 tangent1 = c.tangent1Restitution.xyz;
    const float restitution = c.tangent1Restitution.w;
    const float3 tangent2 = c.tangent2Rolling.xyz;
    const float rollingResistance = c.tangent2Rolling.w;

    const float invMassA = c.invMassTangentVel.x;
    const float invMassB = c.invMassTangentVel.y;
    const float3 invIA0 = c.invIA0.xyz;
    const float3 invIA1 = c.invIA1.xyz;
    const float3 invIA2 = c.invIA2.xyz;
    const float3 invIB0 = c.invIB0.xyz;
    const float3 invIB1 = c.invIB1.xyz;
    const float3 invIB2 = c.invIB2.xyz;

    if(mode == NWB_PHYSICS_SOLVER_MODE_WARM_START){
        if((g_NwbPhysicsPushConstants.flags & 1u) == 0u){
            for(uint i = 0u; i < pointCount; ++i)
                c.points[i].impulsesMass.x = 0.0f;
            c.tangentMassFriction.z = 0.0f;
            c.tangentMassFriction.w = 0.0f;
            c.centerBTwistImpulse.w = 0.0f;
            c.invIA0.w = 0.0f;
            c.invIA1.w = 0.0f;
            c.invIA2.w = 0.0f;
            g_NwbPhysicsContactConstraintBuffers[constraintSlot][constraintIdx] = c;
            return;
        }

        for(uint i = 0u; i < pointCount; ++i){
            const float normalImpulse = c.points[i].impulsesMass.x;
            const float3 rA = c.points[i].rABaseSep.xyz;
            const float3 rB = c.points[i].rBRelVel.xyz;
            const float3 P = normalImpulse * normal;
            nwbPhysicsApplyPointImpulse(P, rA, rB, invMassA, invMassB, invIA0, invIA1, invIA2, invIB0, invIB1, invIB2, vA, wA, vB, wB);
        }

        const float3 fImpulse = c.tangentMassFriction.z * tangent1 + c.tangentMassFriction.w * tangent2;
        nwbPhysicsApplyPointImpulse(fImpulse, c.centerATwistMass.xyz, c.centerBTwistImpulse.xyz, invMassA, invMassB, invIA0, invIA1, invIA2, invIB0, invIB1, invIB2, vA, wA, vB, wB);

        const float3 twistAng = c.centerBTwistImpulse.w * normal;
        wA -= nwbPhysicsMulMat3Rows(invIA0, invIA1, invIA2, twistAng);
        wB += nwbPhysicsMulMat3Rows(invIB0, invIB1, invIB2, twistAng);

        const float3 rollAng = float3(c.invIA0.w, c.invIA1.w, c.invIA2.w);
        wA -= nwbPhysicsMulMat3Rows(invIA0, invIA1, invIA2, rollAng);
        wB += nwbPhysicsMulMat3Rows(invIB0, invIB1, invIB2, rollAng);

        nwbPhysicsStoreBodyVelocity(bodyStateSlot, indexA, vA, wA, flagsA);
        nwbPhysicsStoreBodyVelocity(bodyStateSlot, indexB, vB, wB, flagsB);
        return;
    }

    if(mode == NWB_PHYSICS_SOLVER_MODE_RESTITUTION){
        if(restitution <= 0.0f){
            return;
        }
        for(uint i = 0u; i < pointCount; ++i){
            if(c.points[i].impulsesMass.y <= 0.0f)
                continue;
            const float relVel = c.points[i].rBRelVel.w;
            if(relVel > -NWB_PHYSICS_RESTITUTION_THRESHOLD)
                continue;
            const float3 rA = c.points[i].rABaseSep.xyz;
            const float3 rB = c.points[i].rBRelVel.xyz;
            const float3 dv = (vB + cross(wB, rB)) - (vA + cross(wA, rA));
            const float vn = dot(dv, normal);
            const float normalMass = c.points[i].impulsesMass.z;
            float impulse = -normalMass * (vn + restitution * relVel);
            if(impulse > 0.0f){
                c.points[i].impulsesMass.w += impulse;
                const float3 P = impulse * normal;
                nwbPhysicsApplyPointImpulse(P, rA, rB, invMassA, invMassB, invIA0, invIA1, invIA2, invIB0, invIB1, invIB2, vA, wA, vB, wB);
            }
        }
        nwbPhysicsStoreBodyVelocity(bodyStateSlot, indexA, vA, wA, flagsA);
        nwbPhysicsStoreBodyVelocity(bodyStateSlot, indexB, vB, wB, flagsB);
        g_NwbPhysicsContactConstraintBuffers[constraintSlot][constraintIdx] = c;
        return;
    }

    const bool useBias = (mode == NWB_PHYSICS_SOLVER_MODE_SOLVE_BIAS);
    const float biasRate = c.softnessTangent00.x;
    const float massScaleCfg = c.softnessTangent00.y;
    const float impulseScaleCfg = c.softnessTangent00.z;

    float totalNormalImpulse = 0.0f;
    float maxLeverArm = 0.0f;
    for(uint i = 0u; i < pointCount; ++i){
        const float3 rA = c.points[i].rABaseSep.xyz;
        const float baseSep = c.points[i].rABaseSep.w;
        const float3 rB = c.points[i].rBRelVel.xyz;
        const float normalMass = c.points[i].impulsesMass.z;
        const float lever = c.points[i].anchorLever.w;
        if(lever > maxLeverArm)
            maxLeverArm = lever;

        const float3 dcA = deltaPosA + (nwbPhysicsQuatRotate(rA, deltaRotA) - rA);
        const float3 dcB = deltaPosB + (nwbPhysicsQuatRotate(rB, deltaRotB) - rB);
        const float currentSep = baseSep + dot(dcB - dcA, normal);

        float bias = 0.0f;
        float massScale = 1.0f;
        float impulseScale = 0.0f;
        if(useBias){
            if(currentSep > 0.0f){
                bias = currentSep * invH;
            }
            else{
                bias = biasRate * currentSep;
                massScale = massScaleCfg;
                impulseScale = impulseScaleCfg;
            }
        }

        const float3 dv = (vB + cross(wB, rB)) - (vA + cross(wA, rA));
        const float vn = dot(dv, normal);
        const float oldImpulse = c.points[i].impulsesMass.x;
        const float impulse = -normalMass * massScale * (vn + bias) - impulseScale * oldImpulse;
        const float newImpulse = max(oldImpulse + impulse, 0.0f);
        const float delta = newImpulse - oldImpulse;
        c.points[i].impulsesMass.x = newImpulse;
        if(useBias)
            c.points[i].impulsesMass.y += newImpulse;
        totalNormalImpulse += newImpulse;

        const float3 P = delta * normal;
        nwbPhysicsApplyPointImpulse(P, rA, rB, invMassA, invMassB, invIA0, invIA1, invIA2, invIB0, invIB1, invIB2, vA, wA, vB, wB);
    }

    // 2D tangent friction at manifold center.
    {
        const float3 centerA = c.centerATwistMass.xyz;
        const float3 centerB = c.centerBTwistImpulse.xyz;
        const float3 dv = (vB + cross(wB, centerB)) - (vA + cross(wA, centerA));
        const float vt1 = dot(dv, tangent1) - c.invMassTangentVel.z;
        const float vt2 = dot(dv, tangent2) - c.invMassTangentVel.w;

        const float m00 = c.softnessTangent00.w;
        const float m01 = c.tangentMassFriction.x;
        const float m11 = c.tangentMassFriction.y;
        const float2 rhs = float2(-vt1, -vt2);
        const float2 deltaF = float2(m00 * rhs.x + m01 * rhs.y, m01 * rhs.x + m11 * rhs.y);

        float2 newF = float2(c.tangentMassFriction.z, c.tangentMassFriction.w) + deltaF;
        const float maxF = friction * totalNormalImpulse;
        const float fLenSq = dot(newF, newF);
        if(fLenSq > maxF * maxF && fLenSq > 1.0e-18f)
            newF *= maxF * rsqrt(fLenSq);
        else if(maxF <= 0.0f)
            newF = float2(0.0f, 0.0f);

        const float2 appliedF = newF - float2(c.tangentMassFriction.z, c.tangentMassFriction.w);
        c.tangentMassFriction.z = newF.x;
        c.tangentMassFriction.w = newF.y;
        const float3 P = appliedF.x * tangent1 + appliedF.y * tangent2;
        nwbPhysicsApplyPointImpulse(P, centerA, centerB, invMassA, invMassB, invIA0, invIA1, invIA2, invIB0, invIB1, invIB2, vA, wA, vB, wB);
    }

    // Twist friction around contact normal.
    const float twistMass = c.centerATwistMass.w;
    if(twistMass > 0.0f && maxLeverArm > 0.0f){
        const float dwN = dot(wB - wA, normal);
        const float deltaT = -twistMass * dwN;
        const float maxT = friction * maxLeverArm * totalNormalImpulse;
        const float oldT = c.centerBTwistImpulse.w;
        const float newT = clamp(oldT + deltaT, -maxT, maxT);
        const float appliedT = newT - oldT;
        c.centerBTwistImpulse.w = newT;
        const float3 angP = appliedT * normal;
        wA -= nwbPhysicsMulMat3Rows(invIA0, invIA1, invIA2, angP);
        wB += nwbPhysicsMulMat3Rows(invIB0, invIB1, invIB2, angP);
    }

    // Rolling resistance.
    if(rollingResistance > 0.0f && totalNormalImpulse > 0.0f){
        const float3 dw = wB - wA;
        const float3 deltaR = -nwbPhysicsMulSymmetric3(c.rollingMass0, c.rollingMass1, dw);
        const float3 oldR = float3(c.invIA0.w, c.invIA1.w, c.invIA2.w);
        float3 newR = oldR + deltaR;
        const float maxR = rollingResistance * totalNormalImpulse;
        const float rLenSq = dot(newR, newR);
        if(rLenSq > maxR * maxR && rLenSq > 1.0e-18f)
            newR *= maxR * rsqrt(rLenSq);
        const float3 appliedR = newR - oldR;
        c.invIA0.w = newR.x;
        c.invIA1.w = newR.y;
        c.invIA2.w = newR.z;
        wA -= nwbPhysicsMulMat3Rows(invIA0, invIA1, invIA2, appliedR);
        wB += nwbPhysicsMulMat3Rows(invIB0, invIB1, invIB2, appliedR);
    }

    nwbPhysicsStoreBodyVelocity(bodyStateSlot, indexA, vA, wA, flagsA);
    nwbPhysicsStoreBodyVelocity(bodyStateSlot, indexB, vB, wB, flagsB);
    g_NwbPhysicsContactConstraintBuffers[constraintSlot][constraintIdx] = c;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

