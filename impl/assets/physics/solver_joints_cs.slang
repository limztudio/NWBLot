// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "physics_common.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


float3 nwbPhysicsSafeNormal(const float3 v, const float3 fallback){
    const float lenSq = dot(v, v);
    if(lenSq > 1.0e-12f)
        return v * rsqrt(lenSq);
    return fallback;
}

void nwbPhysicsComputeTangentBasis(const float3 n, out float3 outU1, out float3 outU2){
    const float3 up = abs(n.y) < 0.9f ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
    outU1 = nwbPhysicsSafeNormal(cross(up, n), float3(1.0f, 0.0f, 0.0f));
    outU2 = cross(n, outU1);
}

void nwbPhysicsInvertSymmetric3(
    const float3 a0,
    const float3 a1,
    const float3 a2,
    out float3 inv0,
    out float3 inv1,
    out float3 inv2
){
    const float c11 = a1.y * a2.z - a1.z * a1.z;
    const float c12 = a0.z * a1.z - a0.y * a2.z;
    const float c13 = a0.y * a1.z - a0.z * a1.y;
    const float det = a0.x * c11 + a0.y * c12 + a0.z * c13;
    if(abs(det) <= 1.0e-18f){
        inv0 = float3(0.0f, 0.0f, 0.0f);
        inv1 = float3(0.0f, 0.0f, 0.0f);
        inv2 = float3(0.0f, 0.0f, 0.0f);
        return;
    }
    const float invDet = 1.0f / det;
    const float c22 = a0.x * a2.z - a0.z * a0.z;
    const float c23 = a0.y * a0.z - a0.x * a1.z;
    const float c33 = a0.x * a1.y - a0.y * a0.y;
    inv0 = float3(c11, c12, c13) * invDet;
    inv1 = float3(c12, c22, c23) * invDet;
    inv2 = float3(c13, c23, c33) * invDet;
}

void nwbPhysicsComputePointEffectiveMass(
    const float invMassA,
    const float invMassB,
    const float3 invIA0,
    const float3 invIA1,
    const float3 invIA2,
    const float3 invIB0,
    const float3 invIB1,
    const float3 invIB2,
    const float3 rA,
    const float3 rB,
    out float3 kInv0,
    out float3 kInv1,
    out float3 kInv2
){
    const float sumM = invMassA + invMassB;
    const float3 axX = cross(rA, float3(1.0f, 0.0f, 0.0f));
    const float3 axY = cross(rA, float3(0.0f, 1.0f, 0.0f));
    const float3 axZ = cross(rA, float3(0.0f, 0.0f, 1.0f));
    const float3 bxX = cross(rB, float3(1.0f, 0.0f, 0.0f));
    const float3 bxY = cross(rB, float3(0.0f, 1.0f, 0.0f));
    const float3 bxZ = cross(rB, float3(0.0f, 0.0f, 1.0f));

    const float3 iaX = nwbPhysicsMulMat3Rows(invIA0, invIA1, invIA2, axX);
    const float3 iaY = nwbPhysicsMulMat3Rows(invIA0, invIA1, invIA2, axY);
    const float3 iaZ = nwbPhysicsMulMat3Rows(invIA0, invIA1, invIA2, axZ);
    const float3 ibX = nwbPhysicsMulMat3Rows(invIB0, invIB1, invIB2, bxX);
    const float3 ibY = nwbPhysicsMulMat3Rows(invIB0, invIB1, invIB2, bxY);
    const float3 ibZ = nwbPhysicsMulMat3Rows(invIB0, invIB1, invIB2, bxZ);

    const float s00 = sumM + dot(axX, iaX) + dot(bxX, ibX);
    const float s01 = dot(axX, iaY) + dot(bxX, ibY);
    const float s02 = dot(axX, iaZ) + dot(bxX, ibZ);
    const float s11 = sumM + dot(axY, iaY) + dot(bxY, ibY);
    const float s12 = dot(axY, iaZ) + dot(bxY, ibZ);
    const float s22 = sumM + dot(axZ, iaZ) + dot(bxZ, ibZ);

    const float3 sym0 = float3(s00, s01, s02);
    const float3 sym1 = float3(s01, s11, s12);
    const float3 sym2 = float3(s02, s12, s22);

    nwbPhysicsInvertSymmetric3(sym0, sym1, sym2, kInv0, kInv1, kInv2);
}

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

void nwbPhysicsApplyLinearImpulsePair(
    const float invMassA,
    const float invMassB,
    const float3 invIA0,
    const float3 invIA1,
    const float3 invIA2,
    const float3 invIB0,
    const float3 invIB1,
    const float3 invIB2,
    const float3 rA,
    const float3 rB,
    const float3 P,
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

void nwbPhysicsSolve2DLinearPlaneRows(
    const float invMassA,
    const float invMassB,
    const float3 invIA0,
    const float3 invIA1,
    const float3 invIA2,
    const float3 invIB0,
    const float3 invIB1,
    const float3 invIB2,
    const float3 rA,
    const float3 rB,
    const float3 u1,
    const float3 u2,
    const float c1,
    const float c2,
    const float3 soft,
    const bool useBias,
    inout float3 inOutImpulse,
    inout float3 vA,
    inout float3 wA,
    inout float3 vB,
    inout float3 wB
){
    const float maxBias = 10.0f;
    const float3 dv = (vB + cross(wB, rB)) - (vA + cross(wA, rA));
    const float bias1 = useBias ? clamp(soft.x * c1, -maxBias, maxBias) : 0.0f;
    const float bias2 = useBias ? clamp(soft.x * c2, -maxBias, maxBias) : 0.0f;
    const float mScale = useBias ? soft.y : 1.0f;
    const float iScale = useBias ? soft.z : 0.0f;

    const float old1 = dot(inOutImpulse, u1);
    const float old2 = dot(inOutImpulse, u2);
    const float3 rAu1 = cross(rA, u1);
    const float3 rBu1 = cross(rB, u1);
    const float3 rAu2 = cross(rA, u2);
    const float3 rBu2 = cross(rB, u2);
    const float3 irAu1 = nwbPhysicsMulMat3Rows(invIA0, invIA1, invIA2, rAu1);
    const float3 irBu1 = nwbPhysicsMulMat3Rows(invIB0, invIB1, invIB2, rBu1);
    const float3 irAu2 = nwbPhysicsMulMat3Rows(invIA0, invIA1, invIA2, rAu2);
    const float3 irBu2 = nwbPhysicsMulMat3Rows(invIB0, invIB1, invIB2, rBu2);

    const float k00 = invMassA + invMassB + dot(rAu1, irAu1) + dot(rBu1, irBu1);
    const float k01 = dot(rAu1, irAu2) + dot(rBu1, irBu2);
    const float k11 = invMassA + invMassB + dot(rAu2, irAu2) + dot(rBu2, irBu2);
    const float det = k00 * k11 - k01 * k01;

    const float rhs1 = -(dot(dv, u1) + bias1);
    const float rhs2 = -(dot(dv, u2) + bias2);

    float dP1 = 0.0f;
    float dP2 = 0.0f;
    if(abs(det) > 1.0e-18f){
        const float invDet = 1.0f / det;
        dP1 = (k11 * rhs1 - k01 * rhs2) * invDet * mScale - iScale * old1;
        dP2 = (-k01 * rhs1 + k00 * rhs2) * invDet * mScale - iScale * old2;
    }
    else{
        if(k00 > 1.0e-12f) dP1 = (rhs1 / k00) * mScale - iScale * old1;
        if(k11 > 1.0e-12f) dP2 = (rhs2 / k11) * mScale - iScale * old2;
    }

    const float new1 = old1 + dP1;
    const float new2 = old2 + dP2;
    const float3 pDelta = u1 * dP1 + u2 * dP2;
    inOutImpulse = u1 * new1 + u2 * new2;
    nwbPhysicsApplyLinearImpulsePair(invMassA, invMassB, invIA0, invIA1, invIA2, invIB0, invIB1, invIB2, rA, rB, pDelta, vA, wA, vB, wB);
}

void nwbPhysicsSolve3DAngularLockRow(
    const float3 invIA0,
    const float3 invIA1,
    const float3 invIA2,
    const float3 invIB0,
    const float3 invIB1,
    const float3 invIB2,
    const float4 qA,
    const float4 qB,
    const float4 refQ,
    const float3 soft,
    const bool useBias,
    inout float3 inOutImpulse,
    inout float3 wA,
    inout float3 wB
){
    const float maxBias = 10.0f;
    const float4 qTarget = nwbPhysicsQuatNormalize(nwbPhysicsQuatMul(qA, refQ));
    float4 qErr = nwbPhysicsQuatMul(qB, float4(-qTarget.xyz, qTarget.w));
    if(qErr.w < 0.0f)
        qErr = -qErr;
    const float3 thetaErr = qErr.xyz * 2.0f;

    float3 bias = float3(0.0f, 0.0f, 0.0f);
    const float mScale = useBias ? soft.y : 1.0f;
    const float iScale = useBias ? soft.z : 0.0f;
    if(useBias){
        bias = thetaErr * soft.x;
        const float bLenSq = dot(bias, bias);
        if(bLenSq > maxBias * maxBias && bLenSq > 1.0e-12f)
            bias *= maxBias * rsqrt(bLenSq);
    }

    float3 kAngInv0;
    float3 kAngInv1;
    float3 kAngInv2;
    nwbPhysicsInvertSymmetric3(invIA0 + invIB0, invIA1 + invIB1, invIA2 + invIB2, kAngInv0, kAngInv1, kAngInv2);

    const float3 oldL = inOutImpulse;
    const float3 relW = wB - wA;
    const float3 deltaL = nwbPhysicsMulMat3Rows(kAngInv0, kAngInv1, kAngInv2, -(relW + bias)) * mScale - oldL * iScale;
    inOutImpulse = oldL + deltaL;
    wA -= nwbPhysicsMulMat3Rows(invIA0, invIA1, invIA2, deltaL);
    wB += nwbPhysicsMulMat3Rows(invIB0, invIB1, invIB2, deltaL);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void nwbPhysicsSolveSingleJoint(
    const uint jointIdx,
    const uint mode,
    const uint bodyStateSlot,
    const uint jointSlot,
    const float h,
    const uint flags
){
    const float maxBias = 10.0f;

    NwbPhysicsGpuJointConstraint jc = g_NwbPhysicsJointConstraintBuffers[jointSlot][jointIdx];
    const uint indexA = jc.indicesTypeFlags.x;
    const uint indexB = jc.indicesTypeFlags.y;
    const uint jType = jc.indicesTypeFlags.z;
    const uint jFlags = jc.indicesTypeFlags.w;

    if(mode == NWB_PHYSICS_SOLVER_MODE_WARM_START && (flags & NWB_PHYSICS_SOLVER_FLAG_WARM_START) == 0u){
        jc.linearImpulse.xyz = float3(0.0f, 0.0f, 0.0f);
        jc.angularImpulse.xyz = float3(0.0f, 0.0f, 0.0f);
        jc.limitImpulses = float4(0.0f, 0.0f, 0.0f, 0.0f);
        jc.suspensionSoftness.w = 0.0f;
        g_NwbPhysicsJointConstraintBuffers[jointSlot][jointIdx] = jc;
        return;
    }

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

    const float invMassA = jc.params2.z;
    const float invMassB = jc.params2.w;
    const float3 invIA0 = jc.invIA0.xyz;
    const float3 invIA1 = jc.invIA1.xyz;
    const float3 invIA2 = jc.invIA2.xyz;
    const float3 invIB0 = jc.invIB0.xyz;
    const float3 invIB1 = jc.invIB1.xyz;
    const float3 invIB2 = jc.invIB2.xyz;

    if(mode == NWB_PHYSICS_SOLVER_MODE_WARM_START){
        if(jType == NWB_PHYSICS_JOINT_TYPE_DISTANCE){
            const float3 d0 = jc.baseCenterDelta.xyz + jc.rB0.xyz - jc.rA0.xyz;
            const float3 u0 = nwbPhysicsSafeNormal(d0, float3(0.0f, 1.0f, 0.0f));
            const float3 p = u0 * (jc.linearImpulse.x + jc.limitImpulses.x + jc.limitImpulses.y);
            nwbPhysicsApplyLinearImpulsePair(invMassA, invMassB, invIA0, invIA1, invIA2, invIB0, invIB1, invIB2, jc.rA0.xyz, jc.rB0.xyz, p, vA, wA, vB, wB);
        }
        else{
            const float3 axisA0 = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(jc.localAxisA.xyz, jc.qA0), float3(0.0f, 1.0f, 0.0f));
            const float3 axisB0 = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(jc.localAxisB.xyz, jc.qB0), float3(0.0f, 1.0f, 0.0f));
            if(jType == NWB_PHYSICS_JOINT_TYPE_PRISMATIC){
                const float3 rA0Eff = jc.rA0.xyz + (jc.baseCenterDelta.xyz + jc.rB0.xyz - jc.rA0.xyz);
                const float3 linP = jc.linearImpulse.xyz + axisA0 * (jc.limitImpulses.z + jc.limitImpulses.x + jc.limitImpulses.y);
                nwbPhysicsApplyLinearImpulsePair(invMassA, invMassB, invIA0, invIA1, invIA2, invIB0, invIB1, invIB2, rA0Eff, jc.rB0.xyz, linP, vA, wA, vB, wB);
                wA -= nwbPhysicsMulMat3Rows(invIA0, invIA1, invIA2, jc.angularImpulse.xyz);
                wB += nwbPhysicsMulMat3Rows(invIB0, invIB1, invIB2, jc.angularImpulse.xyz);
            }
            else if(jType == NWB_PHYSICS_JOINT_TYPE_WHEEL){
                const float3 rA0Eff = jc.rA0.xyz + (jc.baseCenterDelta.xyz + jc.rB0.xyz - jc.rA0.xyz);
                const float3 linP = jc.linearImpulse.xyz + axisA0 * (jc.suspensionSoftness.w + jc.limitImpulses.x + jc.limitImpulses.y);
                nwbPhysicsApplyLinearImpulsePair(invMassA, invMassB, invIA0, invIA1, invIA2, invIB0, invIB1, invIB2, rA0Eff, jc.rB0.xyz, linP, vA, wA, vB, wB);
                const float3 axleA0 = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(jc.localAxisB.xyz, nwbPhysicsQuatMul(jc.qA0, jc.referenceRotation)), float3(0.0f, 0.0f, 1.0f));
                const float3 angP = jc.angularImpulse.xyz + axleA0 * jc.limitImpulses.z;
                wA -= nwbPhysicsMulMat3Rows(invIA0, invIA1, invIA2, angP);
                wB += nwbPhysicsMulMat3Rows(invIB0, invIB1, invIB2, angP);
            }
            else{
                nwbPhysicsApplyLinearImpulsePair(invMassA, invMassB, invIA0, invIA1, invIA2, invIB0, invIB1, invIB2, jc.rA0.xyz, jc.rB0.xyz, jc.linearImpulse.xyz, vA, wA, vB, wB);
                float3 angP = jc.angularImpulse.xyz;
                if(jType == NWB_PHYSICS_JOINT_TYPE_REVOLUTE){
                    angP += axisA0 * (jc.limitImpulses.z + jc.limitImpulses.x + jc.limitImpulses.y);
                }
                else if(jType == NWB_PHYSICS_JOINT_TYPE_SPHERICAL){
                    float3 fU1;
                    float3 fU2;
                    nwbPhysicsComputeTangentBasis(axisA0, fU1, fU2);
                    const float3 swingAxis0 = nwbPhysicsSafeNormal(cross(axisA0, axisB0), fU1);
                    angP += axisB0 * (jc.limitImpulses.x + jc.limitImpulses.y) + swingAxis0 * jc.limitImpulses.w;
                }
                if(jType == NWB_PHYSICS_JOINT_TYPE_FIXED || jType == NWB_PHYSICS_JOINT_TYPE_REVOLUTE || jType == NWB_PHYSICS_JOINT_TYPE_MOTOR || jType == NWB_PHYSICS_JOINT_TYPE_SPHERICAL){
                    wA -= nwbPhysicsMulMat3Rows(invIA0, invIA1, invIA2, angP);
                    wB += nwbPhysicsMulMat3Rows(invIB0, invIB1, invIB2, angP);
                }
            }
        }
        nwbPhysicsStoreBodyVelocity(bodyStateSlot, indexA, vA, wA, flagsA);
        nwbPhysicsStoreBodyVelocity(bodyStateSlot, indexB, vB, wB, flagsB);
        return;
    }

    const bool useBias = (mode == NWB_PHYSICS_SOLVER_MODE_SOLVE_BIAS);
    const bool enableLimit = (jFlags & NWB_PHYSICS_JOINT_FLAG_ENABLE_LIMIT) != 0u;
    const bool enableMotor = (jFlags & NWB_PHYSICS_JOINT_FLAG_ENABLE_MOTOR) != 0u;
    const bool isSpring = (jFlags & NWB_PHYSICS_JOINT_FLAG_ENABLE_SPRING) != 0u;

    const float3 rA = nwbPhysicsQuatRotate(jc.rA0.xyz, deltaRotA);
    const float3 rB = nwbPhysicsQuatRotate(jc.rB0.xyz, deltaRotB);
    const float3 d = jc.baseCenterDelta.xyz + deltaPosB - deltaPosA + rB - rA;
    const float4 qA = nwbPhysicsQuatNormalize(nwbPhysicsQuatMul(deltaRotA, jc.qA0));
    const float4 qB = nwbPhysicsQuatNormalize(nwbPhysicsQuatMul(deltaRotB, jc.qB0));

    if(jType == NWB_PHYSICS_JOINT_TYPE_DISTANCE){
        const float dist = sqrt(dot(d, d));
        const float3 u = nwbPhysicsSafeNormal(d, float3(0.0f, 1.0f, 0.0f));
        const float3 rnA = cross(rA, u);
        const float3 rnB = cross(rB, u);
        const float k = invMassA + invMassB
            + dot(rnA, nwbPhysicsMulMat3Rows(invIA0, invIA1, invIA2, rnA))
            + dot(rnB, nwbPhysicsMulMat3Rows(invIB0, invIB1, invIB2, rnB));
        const float effMass = k > 1.0e-12f ? (1.0f / k) : 0.0f;

        if(!enableLimit || isSpring){
            if(!isSpring || useBias){
                const float3 dv = (vB + cross(wB, rB)) - (vA + cross(wA, rA));
                const float cDot = dot(dv, u);
                const float cErr = dist - jc.params0.x;
                const float bias = useBias ? clamp(jc.linearSoftness.x * cErr, -maxBias, maxBias) : 0.0f;
                const float mScale = useBias ? jc.linearSoftness.y : 1.0f;
                const float iScale = useBias ? jc.linearSoftness.z : 0.0f;
                const float dImp = -effMass * mScale * (cDot + bias) - iScale * jc.linearImpulse.x;
                jc.linearImpulse.x += dImp;
                nwbPhysicsApplyLinearImpulsePair(invMassA, invMassB, invIA0, invIA1, invIA2, invIB0, invIB1, invIB2, rA, rB, u * dImp, vA, wA, vB, wB);
            }
        }
        else{
            jc.linearImpulse.x = 0.0f;
        }

        if(enableLimit && jc.params0.y <= jc.params0.z){
            if(dist < jc.params0.y){
                const float3 dv = (vB + cross(wB, rB)) - (vA + cross(wA, rA));
                const float bias = useBias ? clamp(jc.angularSoftness.x * (dist - jc.params0.y), -maxBias, maxBias) : 0.0f;
                const float newImp = max(jc.limitImpulses.x - effMass * (dot(dv, u) + bias), 0.0f);
                const float applied = newImp - jc.limitImpulses.x;
                jc.limitImpulses.x = newImp;
                nwbPhysicsApplyLinearImpulsePair(invMassA, invMassB, invIA0, invIA1, invIA2, invIB0, invIB1, invIB2, rA, rB, u * applied, vA, wA, vB, wB);
            }
            else{
                jc.limitImpulses.x = 0.0f;
            }
            if(dist > jc.params0.z){
                const float3 dv = (vB + cross(wB, rB)) - (vA + cross(wA, rA));
                const float bias = useBias ? clamp(jc.angularSoftness.x * (dist - jc.params0.z), -maxBias, maxBias) : 0.0f;
                const float newImp = min(jc.limitImpulses.y - effMass * (dot(dv, u) + bias), 0.0f);
                const float applied = newImp - jc.limitImpulses.y;
                jc.limitImpulses.y = newImp;
                nwbPhysicsApplyLinearImpulsePair(invMassA, invMassB, invIA0, invIA1, invIA2, invIB0, invIB1, invIB2, rA, rB, u * applied, vA, wA, vB, wB);
            }
            else{
                jc.limitImpulses.y = 0.0f;
            }
        }
    }
    else if(jType == NWB_PHYSICS_JOINT_TYPE_SPHERICAL){
        float3 kInv0;
        float3 kInv1;
        float3 kInv2;
        nwbPhysicsComputePointEffectiveMass(invMassA, invMassB, invIA0, invIA1, invIA2, invIB0, invIB1, invIB2, rA, rB, kInv0, kInv1, kInv2);
        const float3 dv = (vB + cross(wB, rB)) - (vA + cross(wA, rA));
        float3 bias = float3(0.0f, 0.0f, 0.0f);
        const float mScale = useBias ? jc.linearSoftness.y : 1.0f;
        const float iScale = useBias ? jc.linearSoftness.z : 0.0f;
        if(useBias){
            bias = d * jc.linearSoftness.x;
            const float bLenSq = dot(bias, bias);
            if(bLenSq > maxBias * maxBias && bLenSq > 1.0e-12f)
                bias *= maxBias * rsqrt(bLenSq);
        }
        const float3 oldP = jc.linearImpulse.xyz;
        const float3 dP = nwbPhysicsMulMat3Rows(kInv0, kInv1, kInv2, -(dv + bias)) * mScale - oldP * iScale;
        jc.linearImpulse.xyz = oldP + dP;
        nwbPhysicsApplyLinearImpulsePair(invMassA, invMassB, invIA0, invIA1, invIA2, invIB0, invIB1, invIB2, rA, rB, dP, vA, wA, vB, wB);

        if(enableLimit){
            const float3 axisA = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(jc.localAxisA.xyz, qA), float3(0.0f, 1.0f, 0.0f));
            const float3 axisB = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(jc.localAxisB.xyz, qB), float3(0.0f, 1.0f, 0.0f));
            const float3 sInv0 = invIA0 + invIB0;
            const float3 sInv1 = invIA1 + invIB1;
            const float3 sInv2 = invIA2 + invIB2;
            const float swingLimit = jc.params1.z;
            if(swingLimit > 0.0f){
                const float swingAngle = acos(clamp(dot(axisA, axisB), -1.0f, 1.0f));
                if(swingAngle > swingLimit){
                    float3 fU1;
                    float3 fU2;
                    nwbPhysicsComputeTangentBasis(axisA, fU1, fU2);
                    const float3 swingAxis = nwbPhysicsSafeNormal(cross(axisA, axisB), fU1);
                    const float kSwing = dot(swingAxis, nwbPhysicsMulMat3Rows(sInv0, sInv1, sInv2, swingAxis));
                    const float swingMass = kSwing > 1.0e-12f ? (1.0f / kSwing) : 0.0f;
                    const float wSwing = dot(wB - wA, swingAxis);
                    const float limBias = useBias ? clamp(jc.angularSoftness.x * (swingAngle - swingLimit), -maxBias, maxBias) : 0.0f;
                    const float newCone = min(jc.limitImpulses.w - swingMass * (wSwing + limBias), 0.0f);
                    const float appliedCone = newCone - jc.limitImpulses.w;
                    jc.limitImpulses.w = newCone;
                    const float3 coneP = swingAxis * appliedCone;
                    wA -= nwbPhysicsMulMat3Rows(invIA0, invIA1, invIA2, coneP);
                    wB += nwbPhysicsMulMat3Rows(invIB0, invIB1, invIB2, coneP);
                }
                else{
                    jc.limitImpulses.w = 0.0f;
                }
            }
            const float lowLim = jc.params1.x;
            const float highLim = jc.params1.y;
            if(lowLim <= highLim && (swingLimit <= 0.0f || lowLim != 0.0f || highLim != 0.0f)){
                const float4 qTarget = nwbPhysicsQuatNormalize(nwbPhysicsQuatMul(qA, jc.referenceRotation));
                float4 qErr = nwbPhysicsQuatMul(qB, float4(-qTarget.xyz, qTarget.w));
                if(qErr.w < 0.0f)
                    qErr = -qErr;
                const float projSin = dot(qErr.xyz, axisB);
                const float4 qTwist = nwbPhysicsQuatNormalize(float4(axisB * projSin, qErr.w));
                const float twistAngle = 2.0f * atan2(dot(qTwist.xyz, axisB), qTwist.w);
                const float kTwist = dot(axisB, nwbPhysicsMulMat3Rows(sInv0, sInv1, sInv2, axisB));
                const float twistMass = kTwist > 1.0e-12f ? (1.0f / kTwist) : 0.0f;
                if(twistAngle < lowLim){
                    const float limBias = useBias ? clamp(jc.angularSoftness.x * (twistAngle - lowLim), -maxBias, maxBias) : 0.0f;
                    const float newLow = max(jc.limitImpulses.x - twistMass * (dot(wB - wA, axisB) + limBias), 0.0f);
                    const float appliedLow = newLow - jc.limitImpulses.x;
                    jc.limitImpulses.x = newLow;
                    const float3 limP = axisB * appliedLow;
                    wA -= nwbPhysicsMulMat3Rows(invIA0, invIA1, invIA2, limP);
                    wB += nwbPhysicsMulMat3Rows(invIB0, invIB1, invIB2, limP);
                }
                else{
                    jc.limitImpulses.x = 0.0f;
                }
                if(twistAngle > highLim){
                    const float limBias = useBias ? clamp(jc.angularSoftness.x * (twistAngle - highLim), -maxBias, maxBias) : 0.0f;
                    const float newHigh = min(jc.limitImpulses.y - twistMass * (dot(wB - wA, axisB) + limBias), 0.0f);
                    const float appliedHigh = newHigh - jc.limitImpulses.y;
                    jc.limitImpulses.y = newHigh;
                    const float3 limP = axisB * appliedHigh;
                    wA -= nwbPhysicsMulMat3Rows(invIA0, invIA1, invIA2, limP);
                    wB += nwbPhysicsMulMat3Rows(invIB0, invIB1, invIB2, limP);
                }
                else{
                    jc.limitImpulses.y = 0.0f;
                }
            }
        }
    }
    else if(jType == NWB_PHYSICS_JOINT_TYPE_FIXED){
        float3 kInv0;
        float3 kInv1;
        float3 kInv2;
        nwbPhysicsComputePointEffectiveMass(invMassA, invMassB, invIA0, invIA1, invIA2, invIB0, invIB1, invIB2, rA, rB, kInv0, kInv1, kInv2);
        const float3 dv = (vB + cross(wB, rB)) - (vA + cross(wA, rA));
        float3 bias = float3(0.0f, 0.0f, 0.0f);
        const float mScale = useBias ? jc.linearSoftness.y : 1.0f;
        const float iScale = useBias ? jc.linearSoftness.z : 0.0f;
        if(useBias){
            bias = d * jc.linearSoftness.x;
            const float bLenSq = dot(bias, bias);
            if(bLenSq > maxBias * maxBias && bLenSq > 1.0e-12f)
                bias *= maxBias * rsqrt(bLenSq);
        }
        const float3 oldP = jc.linearImpulse.xyz;
        const float3 dP = nwbPhysicsMulMat3Rows(kInv0, kInv1, kInv2, -(dv + bias)) * mScale - oldP * iScale;
        jc.linearImpulse.xyz = oldP + dP;
        nwbPhysicsApplyLinearImpulsePair(invMassA, invMassB, invIA0, invIA1, invIA2, invIB0, invIB1, invIB2, rA, rB, dP, vA, wA, vB, wB);

        float3 angImp = jc.angularImpulse.xyz;
        nwbPhysicsSolve3DAngularLockRow(invIA0, invIA1, invIA2, invIB0, invIB1, invIB2, qA, qB, jc.referenceRotation, jc.angularSoftness.xyz, useBias, angImp, wA, wB);
        jc.angularImpulse.xyz = angImp;
    }
    else if(jType == NWB_PHYSICS_JOINT_TYPE_REVOLUTE){
        float3 kInv0;
        float3 kInv1;
        float3 kInv2;
        nwbPhysicsComputePointEffectiveMass(invMassA, invMassB, invIA0, invIA1, invIA2, invIB0, invIB1, invIB2, rA, rB, kInv0, kInv1, kInv2);
        const float3 dv = (vB + cross(wB, rB)) - (vA + cross(wA, rA));
        float3 bias = float3(0.0f, 0.0f, 0.0f);
        const float mScale = useBias ? jc.linearSoftness.y : 1.0f;
        const float iScale = useBias ? jc.linearSoftness.z : 0.0f;
        if(useBias){
            bias = d * jc.linearSoftness.x;
            const float bLenSq = dot(bias, bias);
            if(bLenSq > maxBias * maxBias && bLenSq > 1.0e-12f)
                bias *= maxBias * rsqrt(bLenSq);
        }
        const float3 oldP = jc.linearImpulse.xyz;
        const float3 dP = nwbPhysicsMulMat3Rows(kInv0, kInv1, kInv2, -(dv + bias)) * mScale - oldP * iScale;
        jc.linearImpulse.xyz = oldP + dP;
        nwbPhysicsApplyLinearImpulsePair(invMassA, invMassB, invIA0, invIA1, invIA2, invIB0, invIB1, invIB2, rA, rB, dP, vA, wA, vB, wB);

        const float3 axisA = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(jc.localAxisA.xyz, qA), float3(0.0f, 1.0f, 0.0f));
        const float3 axisB = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(jc.localAxisB.xyz, qB), float3(0.0f, 1.0f, 0.0f));
        float3 u1;
        float3 u2;
        nwbPhysicsComputeTangentBasis(axisA, u1, u2);
        float3 crossAB = cross(axisA, axisB);
        if(dot(axisA, axisB) < -0.99f && dot(crossAB, crossAB) < 1.0e-4f)
            crossAB = u1;

        const float e1 = dot(crossAB, u1);
        const float e2 = dot(crossAB, u2);
        const float3 sInv0 = invIA0 + invIB0;
        const float3 sInv1 = invIA1 + invIB1;
        const float3 sInv2 = invIA2 + invIB2;
        const float3 su1 = nwbPhysicsMulMat3Rows(sInv0, sInv1, sInv2, u1);
        const float3 su2 = nwbPhysicsMulMat3Rows(sInv0, sInv1, sInv2, u2);
        const float m00 = dot(u1, su1);
        const float m01 = dot(u1, su2);
        const float m11 = dot(u2, su2);
        const float det = m00 * m11 - m01 * m01;

        const float angBias1 = useBias ? clamp(jc.angularSoftness.x * e1, -maxBias, maxBias) : 0.0f;
        const float angBias2 = useBias ? clamp(jc.angularSoftness.x * e2, -maxBias, maxBias) : 0.0f;
        const float angMScale = useBias ? jc.angularSoftness.y : 1.0f;
        const float angIScale = useBias ? jc.angularSoftness.z : 0.0f;
        const float3 oldAng = jc.angularImpulse.xyz;
        const float oldL1 = dot(oldAng, u1);
        const float oldL2 = dot(oldAng, u2);
        const float3 relW = wB - wA;
        const float rhs1 = -(dot(relW, u1) + angBias1);
        const float rhs2 = -(dot(relW, u2) + angBias2);

        float dL1 = 0.0f;
        float dL2 = 0.0f;
        if(abs(det) > 1.0e-18f){
            const float invDet = 1.0f / det;
            dL1 = (m11 * rhs1 - m01 * rhs2) * invDet * angMScale - angIScale * oldL1;
            dL2 = (-m01 * rhs1 + m00 * rhs2) * invDet * angMScale - angIScale * oldL2;
        }
        else{
            if(m00 > 1.0e-12f) dL1 = (rhs1 / m00) * angMScale - angIScale * oldL1;
            if(m11 > 1.0e-12f) dL2 = (rhs2 / m11) * angMScale - angIScale * oldL2;
        }
        const float3 angDelta = u1 * dL1 + u2 * dL2;
        jc.angularImpulse.xyz = u1 * (oldL1 + dL1) + u2 * (oldL2 + dL2);
        wA -= nwbPhysicsMulMat3Rows(invIA0, invIA1, invIA2, angDelta);
        wB += nwbPhysicsMulMat3Rows(invIB0, invIB1, invIB2, angDelta);

        const float kAxis = dot(axisA, nwbPhysicsMulMat3Rows(sInv0, sInv1, sInv2, axisA));
        const float axialMass = kAxis > 1.0e-12f ? (1.0f / kAxis) : 0.0f;
        const float motorSpeed = jc.params1.w;
        const float maxMotorForce = jc.params2.x;
        if(enableMotor && maxMotorForce > 0.0f && axialMass > 0.0f){
            const float wAxis = dot(wB - wA, axisA);
            const float maxImp = maxMotorForce * h;
            const float newMot = clamp(jc.limitImpulses.z - axialMass * (wAxis - motorSpeed), -maxImp, maxImp);
            const float appliedMot = newMot - jc.limitImpulses.z;
            jc.limitImpulses.z = newMot;
            const float3 motP = axisA * appliedMot;
            wA -= nwbPhysicsMulMat3Rows(invIA0, invIA1, invIA2, motP);
            wB += nwbPhysicsMulMat3Rows(invIB0, invIB1, invIB2, motP);
        }
        const float lowLim = jc.params1.x;
        const float highLim = jc.params1.y;
        if(enableLimit && lowLim <= highLim && axialMass > 0.0f){
            const float4 qTarget = nwbPhysicsQuatNormalize(nwbPhysicsQuatMul(qA, jc.referenceRotation));
            float4 qErr = nwbPhysicsQuatMul(qB, float4(-qTarget.xyz, qTarget.w));
            if(qErr.w < 0.0f)
                qErr = -qErr;
            const float projSin = dot(qErr.xyz, axisA);
            const float angle = 2.0f * atan2(projSin, qErr.w);
            if(angle < lowLim){
                const float limBias = useBias ? clamp(jc.angularSoftness.x * (angle - lowLim), -maxBias, maxBias) : 0.0f;
                const float newLow = max(jc.limitImpulses.x - axialMass * (dot(wB - wA, axisA) + limBias), 0.0f);
                const float appliedLow = newLow - jc.limitImpulses.x;
                jc.limitImpulses.x = newLow;
                const float3 limP = axisA * appliedLow;
                wA -= nwbPhysicsMulMat3Rows(invIA0, invIA1, invIA2, limP);
                wB += nwbPhysicsMulMat3Rows(invIB0, invIB1, invIB2, limP);
            }
            else{
                jc.limitImpulses.x = 0.0f;
            }
            if(angle > highLim){
                const float limBias = useBias ? clamp(jc.angularSoftness.x * (angle - highLim), -maxBias, maxBias) : 0.0f;
                const float newHigh = min(jc.limitImpulses.y - axialMass * (dot(wB - wA, axisA) + limBias), 0.0f);
                const float appliedHigh = newHigh - jc.limitImpulses.y;
                jc.limitImpulses.y = newHigh;
                const float3 limP = axisA * appliedHigh;
                wA -= nwbPhysicsMulMat3Rows(invIA0, invIA1, invIA2, limP);
                wB += nwbPhysicsMulMat3Rows(invIB0, invIB1, invIB2, limP);
            }
            else{
                jc.limitImpulses.y = 0.0f;
            }
        }
    }
    else if(jType == NWB_PHYSICS_JOINT_TYPE_PRISMATIC){
        const float3 axisA = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(jc.localAxisA.xyz, qA), float3(0.0f, 1.0f, 0.0f));
        float3 u1;
        float3 u2;
        nwbPhysicsComputeTangentBasis(axisA, u1, u2);
        const float3 rAEff = rA + d;

        float3 linImp = jc.linearImpulse.xyz;
        nwbPhysicsSolve2DLinearPlaneRows(invMassA, invMassB, invIA0, invIA1, invIA2, invIB0, invIB1, invIB2, rAEff, rB, u1, u2, dot(d, u1), dot(d, u2), jc.linearSoftness.xyz, useBias, linImp, vA, wA, vB, wB);
        jc.linearImpulse.xyz = linImp;

        float3 angImp = jc.angularImpulse.xyz;
        nwbPhysicsSolve3DAngularLockRow(invIA0, invIA1, invIA2, invIB0, invIB1, invIB2, qA, qB, jc.referenceRotation, jc.angularSoftness.xyz, useBias, angImp, wA, wB);
        jc.angularImpulse.xyz = angImp;

        const float3 rnA = cross(rAEff, axisA);
        const float3 rnB = cross(rB, axisA);
        const float kAxis = invMassA + invMassB
            + dot(rnA, nwbPhysicsMulMat3Rows(invIA0, invIA1, invIA2, rnA))
            + dot(rnB, nwbPhysicsMulMat3Rows(invIB0, invIB1, invIB2, rnB));
        const float axialMass = kAxis > 1.0e-12f ? (1.0f / kAxis) : 0.0f;

        const float motorSpeed = jc.params1.w;
        const float maxMotorForce = jc.params2.x;
        if(enableMotor && maxMotorForce > 0.0f && axialMass > 0.0f){
            const float3 dv = (vB + cross(wB, rB)) - (vA + cross(wA, rAEff));
            const float vAxis = dot(dv, axisA);
            const float maxImp = maxMotorForce * h;
            const float newMot = clamp(jc.limitImpulses.z - axialMass * (vAxis - motorSpeed), -maxImp, maxImp);
            const float appliedMot = newMot - jc.limitImpulses.z;
            jc.limitImpulses.z = newMot;
            nwbPhysicsApplyLinearImpulsePair(invMassA, invMassB, invIA0, invIA1, invIA2, invIB0, invIB1, invIB2, rAEff, rB, axisA * appliedMot, vA, wA, vB, wB);
        }

        const float lowLim = jc.params1.x;
        const float highLim = jc.params1.y;
        if(enableLimit && lowLim <= highLim && axialMass > 0.0f){
            const float trans = dot(d, axisA);
            if(trans < lowLim){
                const float3 dv = (vB + cross(wB, rB)) - (vA + cross(wA, rAEff));
                const float limBias = useBias ? clamp(jc.linearSoftness.x * (trans - lowLim), -maxBias, maxBias) : 0.0f;
                const float newLow = max(jc.limitImpulses.x - axialMass * (dot(dv, axisA) + limBias), 0.0f);
                const float appliedLow = newLow - jc.limitImpulses.x;
                jc.limitImpulses.x = newLow;
                nwbPhysicsApplyLinearImpulsePair(invMassA, invMassB, invIA0, invIA1, invIA2, invIB0, invIB1, invIB2, rAEff, rB, axisA * appliedLow, vA, wA, vB, wB);
            }
            else{
                jc.limitImpulses.x = 0.0f;
            }
            if(trans > highLim){
                const float3 dv = (vB + cross(wB, rB)) - (vA + cross(wA, rAEff));
                const float limBias = useBias ? clamp(jc.linearSoftness.x * (trans - highLim), -maxBias, maxBias) : 0.0f;
                const float newHigh = min(jc.limitImpulses.y - axialMass * (dot(dv, axisA) + limBias), 0.0f);
                const float appliedHigh = newHigh - jc.limitImpulses.y;
                jc.limitImpulses.y = newHigh;
                nwbPhysicsApplyLinearImpulsePair(invMassA, invMassB, invIA0, invIA1, invIA2, invIB0, invIB1, invIB2, rAEff, rB, axisA * appliedHigh, vA, wA, vB, wB);
            }
            else{
                jc.limitImpulses.y = 0.0f;
            }
        }
    }
    else if(jType == NWB_PHYSICS_JOINT_TYPE_MOTOR){
        if(!useBias || h <= 0.0f){
            g_NwbPhysicsJointConstraintBuffers[jointSlot][jointIdx] = jc;
            return;
        }
        const float correctionFactor = jc.params0.w;
        const float maxMotorForce = jc.params2.x;
        const float maxTorque = jc.params2.y;
        const float3 linTarget = nwbPhysicsQuatRotate(jc.linearOffset.xyz, qA);
        const float3 errLin = d - linTarget;
        const float3 dv = (vB + cross(wB, rB)) - (vA + cross(wA, rA));
        float3 kInv0;
        float3 kInv1;
        float3 kInv2;
        nwbPhysicsComputePointEffectiveMass(invMassA, invMassB, invIA0, invIA1, invIA2, invIB0, invIB1, invIB2, rA, rB, kInv0, kInv1, kInv2);
        const float3 linBias = errLin * (correctionFactor / h);

        if(maxMotorForce > 0.0f){
            const float3 oldP = jc.linearImpulse.xyz;
            const float3 dP = nwbPhysicsMulMat3Rows(kInv0, kInv1, kInv2, -(dv + linBias));
            float3 newP = oldP + dP;
            const float maxP = maxMotorForce * h;
            const float lenSq = dot(newP, newP);
            if(lenSq > maxP * maxP && lenSq > 1.0e-12f)
                newP *= maxP * rsqrt(lenSq);
            const float3 appliedP = newP - oldP;
            jc.linearImpulse.xyz = newP;
            nwbPhysicsApplyLinearImpulsePair(invMassA, invMassB, invIA0, invIA1, invIA2, invIB0, invIB1, invIB2, rA, rB, appliedP, vA, wA, vB, wB);
        }

        if(maxTorque > 0.0f){
            const float4 qTarget = nwbPhysicsQuatNormalize(nwbPhysicsQuatMul(qA, jc.referenceRotation));
            float4 qErr = nwbPhysicsQuatMul(qB, float4(-qTarget.xyz, qTarget.w));
            if(qErr.w < 0.0f)
                qErr = -qErr;
            const float3 angBias = qErr.xyz * (2.0f * correctionFactor / h);
            float3 kAngInv0;
            float3 kAngInv1;
            float3 kAngInv2;
            nwbPhysicsInvertSymmetric3(invIA0 + invIB0, invIA1 + invIB1, invIA2 + invIB2, kAngInv0, kAngInv1, kAngInv2);
            const float3 oldL = jc.angularImpulse.xyz;
            const float3 dL = nwbPhysicsMulMat3Rows(kAngInv0, kAngInv1, kAngInv2, -((wB - wA) + angBias));
            float3 newL = oldL + dL;
            const float maxL = maxTorque * h;
            const float lenSq = dot(newL, newL);
            if(lenSq > maxL * maxL && lenSq > 1.0e-12f)
                newL *= maxL * rsqrt(lenSq);
            const float3 appliedL = newL - oldL;
            jc.angularImpulse.xyz = newL;
            wA -= nwbPhysicsMulMat3Rows(invIA0, invIA1, invIA2, appliedL);
            wB += nwbPhysicsMulMat3Rows(invIB0, invIB1, invIB2, appliedL);
        }
    }
    else if(jType == NWB_PHYSICS_JOINT_TYPE_WHEEL){
        const float3 suspAxis = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(jc.localAxisA.xyz, qA), float3(0.0f, 1.0f, 0.0f));
        float3 u1;
        float3 u2;
        nwbPhysicsComputeTangentBasis(suspAxis, u1, u2);
        const float3 rAEff = rA + d;

        float3 linImp = jc.linearImpulse.xyz;
        nwbPhysicsSolve2DLinearPlaneRows(invMassA, invMassB, invIA0, invIA1, invIA2, invIB0, invIB1, invIB2, rAEff, rB, u1, u2, dot(d, u1), dot(d, u2), jc.linearSoftness.xyz, useBias, linImp, vA, wA, vB, wB);
        jc.linearImpulse.xyz = linImp;

        const float3 rnA = cross(rAEff, suspAxis);
        const float3 rnB = cross(rB, suspAxis);
        const float kSusp = invMassA + invMassB
            + dot(rnA, nwbPhysicsMulMat3Rows(invIA0, invIA1, invIA2, rnA))
            + dot(rnB, nwbPhysicsMulMat3Rows(invIB0, invIB1, invIB2, rnB));
        const float suspMass = kSusp > 1.0e-12f ? (1.0f / kSusp) : 0.0f;

        if(useBias && jc.suspensionSoftness.y > 0.0f && suspMass > 0.0f){
            const float3 dv = (vB + cross(wB, rB)) - (vA + cross(wA, rAEff));
            const float vSusp = dot(dv, suspAxis);
            const float cSusp = dot(d, suspAxis);
            const float sBias = clamp(jc.suspensionSoftness.x * cSusp, -maxBias, maxBias);
            const float dSusp = -suspMass * jc.suspensionSoftness.y * (vSusp + sBias) - jc.suspensionSoftness.z * jc.suspensionSoftness.w;
            jc.suspensionSoftness.w += dSusp;
            nwbPhysicsApplyLinearImpulsePair(invMassA, invMassB, invIA0, invIA1, invIA2, invIB0, invIB1, invIB2, rAEff, rB, suspAxis * dSusp, vA, wA, vB, wB);
        }

        const float lowLim = jc.params1.x;
        const float highLim = jc.params1.y;
        if(enableLimit && lowLim <= highLim && suspMass > 0.0f){
            const float trans = dot(d, suspAxis);
            if(trans < lowLim){
                const float3 dv = (vB + cross(wB, rB)) - (vA + cross(wA, rAEff));
                const float limBias = useBias ? clamp(jc.linearSoftness.x * (trans - lowLim), -maxBias, maxBias) : 0.0f;
                const float newLow = max(jc.limitImpulses.x - suspMass * (dot(dv, suspAxis) + limBias), 0.0f);
                const float appliedLow = newLow - jc.limitImpulses.x;
                jc.limitImpulses.x = newLow;
                nwbPhysicsApplyLinearImpulsePair(invMassA, invMassB, invIA0, invIA1, invIA2, invIB0, invIB1, invIB2, rAEff, rB, suspAxis * appliedLow, vA, wA, vB, wB);
            }
            else{
                jc.limitImpulses.x = 0.0f;
            }
            if(trans > highLim){
                const float3 dv = (vB + cross(wB, rB)) - (vA + cross(wA, rAEff));
                const float limBias = useBias ? clamp(jc.linearSoftness.x * (trans - highLim), -maxBias, maxBias) : 0.0f;
                const float newHigh = min(jc.limitImpulses.y - suspMass * (dot(dv, suspAxis) + limBias), 0.0f);
                const float appliedHigh = newHigh - jc.limitImpulses.y;
                jc.limitImpulses.y = newHigh;
                nwbPhysicsApplyLinearImpulsePair(invMassA, invMassB, invIA0, invIA1, invIA2, invIB0, invIB1, invIB2, rAEff, rB, suspAxis * appliedHigh, vA, wA, vB, wB);
            }
            else{
                jc.limitImpulses.y = 0.0f;
            }
        }

        const float4 qTarget = nwbPhysicsQuatNormalize(nwbPhysicsQuatMul(qA, jc.referenceRotation));
        const float3 axleA = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(jc.localAxisB.xyz, qTarget), float3(0.0f, 0.0f, 1.0f));
        const float3 axleB = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(jc.localAxisB.xyz, qB), float3(0.0f, 0.0f, 1.0f));
        float3 wU1;
        float3 wU2;
        nwbPhysicsComputeTangentBasis(axleA, wU1, wU2);
        float3 crossAB = cross(axleA, axleB);
        if(dot(axleA, axleB) < -0.99f && dot(crossAB, crossAB) < 1.0e-4f)
            crossAB = wU1;
        const float e1 = dot(crossAB, wU1);
        const float e2 = dot(crossAB, wU2);
        const float3 sInv0 = invIA0 + invIB0;
        const float3 sInv1 = invIA1 + invIB1;
        const float3 sInv2 = invIA2 + invIB2;
        const float3 su1 = nwbPhysicsMulMat3Rows(sInv0, sInv1, sInv2, wU1);
        const float3 su2 = nwbPhysicsMulMat3Rows(sInv0, sInv1, sInv2, wU2);
        const float m00 = dot(wU1, su1);
        const float m01 = dot(wU1, su2);
        const float m11 = dot(wU2, su2);
        const float det = m00 * m11 - m01 * m01;
        const float angBias1 = useBias ? clamp(jc.angularSoftness.x * e1, -maxBias, maxBias) : 0.0f;
        const float angBias2 = useBias ? clamp(jc.angularSoftness.x * e2, -maxBias, maxBias) : 0.0f;
        const float angMScale = useBias ? jc.angularSoftness.y : 1.0f;
        const float angIScale = useBias ? jc.angularSoftness.z : 0.0f;
        const float3 oldAng = jc.angularImpulse.xyz;
        const float oldL1 = dot(oldAng, wU1);
        const float oldL2 = dot(oldAng, wU2);
        const float3 relW = wB - wA;
        const float rhs1 = -(dot(relW, wU1) + angBias1);
        const float rhs2 = -(dot(relW, wU2) + angBias2);

        float dL1 = 0.0f;
        float dL2 = 0.0f;
        if(abs(det) > 1.0e-18f){
            const float invDet = 1.0f / det;
            dL1 = (m11 * rhs1 - m01 * rhs2) * invDet * angMScale - angIScale * oldL1;
            dL2 = (-m01 * rhs1 + m00 * rhs2) * invDet * angMScale - angIScale * oldL2;
        }
        else{
            if(m00 > 1.0e-12f) dL1 = (rhs1 / m00) * angMScale - angIScale * oldL1;
            if(m11 > 1.0e-12f) dL2 = (rhs2 / m11) * angMScale - angIScale * oldL2;
        }
        const float3 angDelta = wU1 * dL1 + wU2 * dL2;
        jc.angularImpulse.xyz = wU1 * (oldL1 + dL1) + wU2 * (oldL2 + dL2);
        wA -= nwbPhysicsMulMat3Rows(invIA0, invIA1, invIA2, angDelta);
        wB += nwbPhysicsMulMat3Rows(invIB0, invIB1, invIB2, angDelta);

        const float kAxle = dot(axleA, nwbPhysicsMulMat3Rows(sInv0, sInv1, sInv2, axleA));
        const float motorSpeed = jc.params1.w;
        const float maxMotorForce = jc.params2.x;
        if(enableMotor && maxMotorForce > 0.0f && kAxle > 1.0e-12f){
            const float axleMass = 1.0f / kAxle;
            const float wAxle = dot(wB - wA, axleA);
            const float maxImp = maxMotorForce * h;
            const float newMot = clamp(jc.limitImpulses.z - axleMass * (wAxle - motorSpeed), -maxImp, maxImp);
            const float appliedMot = newMot - jc.limitImpulses.z;
            jc.limitImpulses.z = newMot;
            const float3 motP = axleA * appliedMot;
            wA -= nwbPhysicsMulMat3Rows(invIA0, invIA1, invIA2, motP);
            wB += nwbPhysicsMulMat3Rows(invIB0, invIB1, invIB2, motP);
        }
    }

    const float4 qA0n = jc.qA0;
    const float3 axisA0Tot = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(jc.localAxisA.xyz, qA0n), float3(0.0f, 1.0f, 0.0f));
    float3 totalLinP = jc.linearImpulse.xyz;
    float3 totalAngL = jc.angularImpulse.xyz;
    if(jType == NWB_PHYSICS_JOINT_TYPE_DISTANCE){
        const float3 d0 = jc.baseCenterDelta.xyz + jc.rB0.xyz - jc.rA0.xyz;
        const float3 u0 = nwbPhysicsSafeNormal(d0, float3(0.0f, 1.0f, 0.0f));
        totalLinP = u0 * (jc.linearImpulse.x + jc.limitImpulses.x + jc.limitImpulses.y);
    }
    else if(jType == NWB_PHYSICS_JOINT_TYPE_PRISMATIC){
        totalLinP += axisA0Tot * (jc.limitImpulses.z + jc.limitImpulses.x + jc.limitImpulses.y);
    }
    else if(jType == NWB_PHYSICS_JOINT_TYPE_WHEEL){
        totalLinP += axisA0Tot * (jc.suspensionSoftness.w + jc.limitImpulses.x + jc.limitImpulses.y);
        const float4 qTarget = nwbPhysicsQuatNormalize(nwbPhysicsQuatMul(qA0n, jc.referenceRotation));
        const float3 axleA = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(jc.localAxisB.xyz, qTarget), float3(0.0f, 0.0f, 1.0f));
        totalAngL += axleA * jc.limitImpulses.z;
    }
    else if(jType == NWB_PHYSICS_JOINT_TYPE_REVOLUTE){
        totalAngL += axisA0Tot * (jc.limitImpulses.z + jc.limitImpulses.x + jc.limitImpulses.y);
    }
    else if(jType == NWB_PHYSICS_JOINT_TYPE_SPHERICAL){
        const float3 axisB0Tot = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(jc.localAxisB.xyz, jc.qB0), float3(0.0f, 1.0f, 0.0f));
        float3 fU1;
        float3 fU2;
        nwbPhysicsComputeTangentBasis(axisA0Tot, fU1, fU2);
        const float3 swingAxis = nwbPhysicsSafeNormal(cross(axisA0Tot, axisB0Tot), fU1);
        totalAngL += axisB0Tot * (jc.limitImpulses.x + jc.limitImpulses.y) + swingAxis * jc.limitImpulses.w;
    }
    jc.linearImpulse.w = max(jc.linearImpulse.w, length(totalLinP));
    jc.angularImpulse.w = max(jc.angularImpulse.w, length(totalAngL));

    g_NwbPhysicsJointConstraintBuffers[jointSlot][jointIdx] = jc;
    nwbPhysicsStoreBodyVelocity(bodyStateSlot, indexA, vA, wA, flagsA);
    nwbPhysicsStoreBodyVelocity(bodyStateSlot, indexB, vB, wB, flagsB);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_PHYSICS_WORKGROUP_SIZE, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint itemCount = g_NwbPhysicsPushConstants.itemCount;
    const uint itemOffset = g_NwbPhysicsPushConstants.itemOffset;
    const uint mode = g_NwbPhysicsPushConstants.modeOrSecondaryCount;
    const uint flags = g_NwbPhysicsPushConstants.flags;
    const uint bodyStateSlot = g_NwbPhysicsPushConstants.slots0.y;
    const uint jointSlot = g_NwbPhysicsPushConstants.slots1.x;
    const float h = g_NwbPhysicsPushConstants.deltaTime;

    if((flags & NWB_PHYSICS_SOLVER_FLAG_SERIAL_LOOP) != 0u){
        if(dispatchThreadID.x != 0u)
            return;
        for(uint k = 0u; k < itemCount; ++k)
            nwbPhysicsSolveSingleJoint(itemOffset + k, mode, bodyStateSlot, jointSlot, h, flags);
        return;
    }

    const uint localIdx = dispatchThreadID.x;
    if(localIdx >= itemCount)
        return;
    nwbPhysicsSolveSingleJoint(itemOffset + localIdx, mode, bodyStateSlot, jointSlot, h, flags);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

