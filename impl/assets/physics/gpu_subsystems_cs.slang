// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "physics_common.slangi"
#include "solver_body_helpers.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool nwbPhysicsSubsysBodyMatchesFilter(const uint bodyFilterSlot, const uint bodyIdx, const uint4 targetFilter){
    if(bodyFilterSlot == NWB_PHYSICS_INVALID_INDEX)
        return true;
    const uint4 bf = asuint(g_NwbPhysicsFloat4Buffers[bodyFilterSlot][bodyIdx]);
    if(nwbPhysicsFiltersMatch(bf.x, bf.y, asint(bf.z), targetFilter.x, targetFilter.y, asint(targetFilter.z)))
        return true;
    const uint extraCnt = bf.w & 0xffu;
    const uint extraOff = bf.w >> 8u;
    [loop] for(uint ei = 0u; ei < extraCnt; ++ei){
        const uint4 ef = asuint(g_NwbPhysicsFloat4Buffers[bodyFilterSlot][extraOff + ei]);
        if(nwbPhysicsFiltersMatch(ef.x, ef.y, asint(ef.z), targetFilter.x, targetFilter.y, asint(targetFilter.z)))
            return true;
    }
    return false;
}

float nwbPhysicsSubsysFieldFalloff(
    const uint falloffMode,
    const float distSq,
    const float invDist,
    const float minDist,
    const float falloffPow,
    const float radius
){
    if(falloffMode == 0u)
        return 1.0f;
    const float minD = max(0.05f, minDist);
    if(falloffMode == 1u){
        if(falloffPow <= 0.0f)
            return 1.0f;
        const float minDSq = minD * minD;
        if(distSq <= minDSq){
            if(falloffPow == 2.0f)
                return 1.0f / minDSq;
            if(falloffPow == 1.0f)
                return 1.0f / minD;
            return 1.0f / pow(minD, falloffPow);
        }
        if(falloffPow == 2.0f)
            return 1.0f / distSq;
        if(falloffPow == 1.0f)
            return invDist;
        return 1.0f / pow(distSq * invDist, falloffPow);
    }
    const float rawDist = distSq * invDist;
    const float effDist = max(minD, rawDist);
    if(falloffMode == 2u){
        if(radius <= minD)
            return 1.0f;
        return clamp(1.0f - (effDist - minD) / (radius - minD), 0.0f, 1.0f);
    }
    if(falloffMode == 3u)
        return exp(-max(0.0f, falloffPow) * (effDist - minD));
    if(falloffMode == 4u){
        if(radius <= minD)
            return 1.0f;
        const float t = clamp((effDist - minD) / (radius - minD), 0.0f, 1.0f);
        const float u = 1.0f - t;
        return u * u * (3.0f - 2.0f * u);
    }
    return 1.0f;
}

bool nwbPhysicsSubsysForceFieldContains(const NwbPhysicsGpuForceField f, const uint fieldShape, const float3 pos){
    if(fieldShape == 1u){
        const float3 pL = abs(nwbPhysicsQuatInvRotate(pos - f.shapeCenterOrCapA.xyz, f.shapeRotOrCapB));
        return all(pL <= f.shapeHalfExtentsExtra.xyz);
    }
    if(fieldShape == 2u){
        const float sr = max(0.0f, f.shapeCenterOrCapA.w);
        const float3 d = pos - f.shapeCenterOrCapA.xyz;
        return dot(d, d) <= sr * sr;
    }
    if(fieldShape == 3u){
        float segT = 0.0f;
        const float3 cp = nwbPhysicsClosestPointOnSegment(pos, f.shapeCenterOrCapA.xyz, f.shapeRotOrCapB.xyz, segT);
        const float cr = max(0.0f, f.shapeCenterOrCapA.w);
        const float3 d = pos - cp;
        return dot(d, d) <= cr * cr;
    }
    return all(pos >= f.boundsMinMeta.xyz) && all(pos <= f.boundsMaxSuction.xyz);
}

float3 nwbPhysicsSubsysEvalForceField(
    const NwbPhysicsGpuForceField f,
    const float3 pos,
    const float3 linVel,
    const float bodyMass,
    const float maxExtent,
    const bool hasInertia,
    const float timeSec,
    const float invDt,
    const float3 worldGrav,
    const float gravScale,
    inout float3 inoutTorque
){
    const uint packedMeta = asuint(f.boundsMinMeta.w);
    const uint fieldType = packedMeta & 0xffu;
    const uint fieldShape = (packedMeta >> 8u) & 0xffu;
    const uint falloffMode = (packedMeta >> 16u) & 0xffu;
    const uint enabledBounds = (packedMeta >> 24u) & 0xffu;
    if((enabledBounds & 1u) == 0u || bodyMass <= 0.0f)
        return float3(0.0f, 0.0f, 0.0f);
    if((enabledBounds & 2u) != 0u && !nwbPhysicsSubsysForceFieldContains(f, fieldShape, pos))
        return float3(0.0f, 0.0f, 0.0f);

    const float strength = f.originOrDirStrength.w;
    const float radius = f.axisRadius.w;
    const float minDist = f.minDistFalloffTurb.x;
    const float falloffPow = f.minDistFalloffTurb.y;
    const float turbAmp = f.minDistFalloffTurb.z;
    const float turbFreq = f.minDistFalloffTurb.w;
    const float inwardSuction = f.boundsMaxSuction.w;
    const float safeExt = max(0.1f, maxExtent);

    float3 hoistedDir;
    if(fieldType == 0u || fieldType == 5u)
        hoistedDir = nwbPhysicsSafeNormal(f.originOrDirStrength.xyz, float3(0.0f, -1.0f, 0.0f));
    else if(fieldType == 2u || fieldType == 7u)
        hoistedDir = nwbPhysicsSafeNormal(f.originOrDirStrength.xyz, float3(1.0f, 0.0f, 0.0f));
    else
        hoistedDir = nwbPhysicsSafeNormal(f.axisRadius.xyz, float3(0.0f, 1.0f, 0.0f));

    if(fieldType == 0u)
        return hoistedDir * (strength * bodyMass * gravScale);

    if(fieldType == 1u || fieldType == 10u){
        const float3 diff = f.originOrDirStrength.xyz - pos;
        const float distSq = dot(diff, diff);
        if(distSq <= 1.0e-12f || (radius > 0.0f && distSq > radius * radius))
            return float3(0.0f, 0.0f, 0.0f);
        const float invD = rsqrt(distSq);
        const float3 dir = diff * invD;
        const float fall = nwbPhysicsSubsysFieldFalloff(falloffMode, distSq, invD, minDist, falloffPow, radius);
        float3 res = dir * (strength * bodyMass * fall);
        if(fieldType == 10u && inwardSuction > 0.0f){
            const float minD = max(0.05f, minDist);
            if(distSq < minD * minD){
                const float vOut = -dot(linVel, dir);
                if(vOut > 0.0f)
                    res += dir * (vOut * inwardSuction * bodyMass);
            }
        }
        return res;
    }

    if(fieldType == 6u){
        const float3 diff = pos - f.originOrDirStrength.xyz;
        const float distSq = dot(diff, diff);
        if(radius > 0.0f && distSq > radius * radius)
            return float3(0.0f, 0.0f, 0.0f);
        float3 dir = float3(0.0f, 1.0f, 0.0f);
        float invD = 1.0f / max(0.05f, minDist);
        if(distSq > 1.0e-12f){
            invD = rsqrt(distSq);
            dir = diff * invD;
        }
        return dir * (strength * bodyMass * nwbPhysicsSubsysFieldFalloff(falloffMode, distSq, invD, minDist, falloffPow, radius));
    }

    if(fieldType == 2u){
        float3 windV = hoistedDir * strength;
        float3 windCurl = float3(0.0f, 0.0f, 0.0f);
        if(turbAmp > 0.0f){
            const float ph = timeSec * turbFreq + pos.x * 0.37f + pos.y * 0.53f + pos.z * 0.71f;
            float3 sV;
            float3 cV;
            sincos(float3(ph, ph * 1.31f, ph * 0.79f + 1.1f), sV, cV);
            windV += float3(sV.x, cV.y, sV.z) * turbAmp;
            windCurl = float3(cV.x, -sV.y, cV.z) * (turbAmp * safeExt * 0.5f);
        }
        if(dot(windCurl, windCurl) > 1.0e-12f)
            inoutTorque += windCurl * safeExt;
        const float3 relWind = windV - linVel;
        const float relSq = dot(relWind, relWind);
        if(relSq <= 1.0e-12f)
            return float3(0.0f, 0.0f, 0.0f);
        const float invRel = rsqrt(relSq);
        const float relSp = relSq * invRel;
        const float windCoeff = hasInertia ? (max(0.05f, maxExtent * maxExtent) * 2.0f) : maxExtent;
        const float clampedMag = min(relSp * windCoeff, bodyMass * relSp * invDt * 0.9f);
        return relWind * (clampedMag * invRel);
    }

    if(fieldType == 7u){
        float fall = 1.0f;
        if(radius > 0.0f && falloffMode != 0u){
            const float3 diff = pos - f.shapeCenterOrCapA.xyz;
            const float dSq = dot(diff, diff);
            if(dSq > radius * radius)
                return float3(0.0f, 0.0f, 0.0f);
            const float invD = (dSq > 1.0e-12f) ? rsqrt(dSq) : 1.0f;
            fall = nwbPhysicsSubsysFieldFalloff(falloffMode, dSq, invD, minDist, falloffPow, radius);
        }
        return hoistedDir * (strength * bodyMass * fall);
    }

    if(fieldType == 8u){
        const float freq = max(0.05f, turbFreq);
        const float ph = timeSec * freq + pos.x * 0.43f + pos.y * 0.67f + pos.z * 0.89f;
        float3 sV;
        float3 cV;
        sincos(float3(ph, ph * 1.37f + 0.7f, ph * 0.83f + 1.9f), sV, cV);
        const float3 turbDir = float3(sV.x - cV.z * 0.5f, sV.y - cV.x * 0.5f, sV.z - cV.y * 0.5f);
        const float amp = strength + turbAmp;
        inoutTorque += float3(cV.x, sV.y, cV.z) * (amp * safeExt * bodyMass * 0.25f);
        return turbDir * (amp * bodyMass);
    }

    if(fieldType == 9u){
        const float cd = max(0.0f, strength);
        const float3 relV = f.originOrDirStrength.xyz - linVel;
        const float relSq = dot(relV, relV);
        float3 res = float3(0.0f, 0.0f, 0.0f);
        if(relSq > 1.0e-12f && cd > 0.0f){
            const float invRel = rsqrt(relSq);
            const float relSp = relSq * invRel;
            const float area = max(0.05f, maxExtent * maxExtent);
            const float dragMag = min(0.5f * cd * area * relSq, bodyMass * relSp * invDt * 0.9f);
            res = relV * (dragMag * invRel);
        }
        return res;
    }

    if(fieldType == 3u){
        const float3 diff = pos - f.originOrDirStrength.xyz;
        const float3 radPerp = diff - hoistedDir * dot(diff, hoistedDir);
        const float distSq = dot(radPerp, radPerp);
        if(distSq <= 1.0e-12f || (radius > 0.0f && distSq > radius * radius))
            return float3(0.0f, 0.0f, 0.0f);
        const float invD = rsqrt(distSq);
        const float3 rDir = radPerp * invD;
        const float3 swDir = nwbPhysicsSafeNormal(cross(hoistedDir, rDir), float3(0.0f, 0.0f, 0.0f));
        const float fall = nwbPhysicsSubsysFieldFalloff(falloffMode, distSq, invD, minDist, falloffPow, radius);
        float3 res = swDir * (strength * bodyMass * fall);
        if(inwardSuction != 0.0f)
            res -= rDir * (inwardSuction * bodyMass * fall);
        if(turbAmp > 0.0f){
            const float ph = timeSec * turbFreq + pos.x * 0.37f + pos.y * 0.53f + pos.z * 0.71f;
            float3 sV;
            float3 cV;
            sincos(float3(ph, ph * 1.31f, ph * 0.79f + 1.1f), sV, cV);
            res += float3(sV.x, cV.y, sV.z) * (turbAmp * bodyMass * fall);
            inoutTorque += hoistedDir * (turbAmp * safeExt * bodyMass * 0.1f);
        }
        return res;
    }

    if(fieldType == 4u){
        const float damp = max(strength, 0.0f);
        const float speedSq = dot(linVel, linVel);
        if(speedSq > 1.0e-12f && damp > 0.0f){
            const float invSp = rsqrt(speedSq);
            const float sp = speedSq * invSp;
            const float dampMag = min(damp * bodyMass * sp, bodyMass * sp * invDt * 0.9f);
            return (-linVel) * (dampMag * invSp);
        }
        return float3(0.0f, 0.0f, 0.0f);
    }

    if(fieldType == 5u)
        return (hoistedDir * strength - worldGrav) * (bodyMass * gravScale);

    return float3(0.0f, 0.0f, 0.0f);
}

void nwbPhysicsSubsysApplyForceFieldAngular(
    const NwbPhysicsGpuForceField f,
    const float3 angVel,
    const float maxExtent,
    const float4 invI0,
    const float4 invI1,
    const float invDt,
    inout float3 inoutTorque
){
    const uint packedMeta = asuint(f.boundsMinMeta.w);
    const uint fieldType = packedMeta & 0xffu;
    const float strength = f.originOrDirStrength.w;
    if(fieldType != 4u && fieldType != 9u)
        return;
    const float wSq = dot(angVel, angVel);
    if(wSq <= 1.0e-12f || strength <= 0.0f)
        return;
    const float invW = rsqrt(wSq);
    const float wSp = wSq * invW;
    const float3 wDir = angVel * invW;
    const float kInvI = dot(wDir, nwbPhysicsMulSymmetric3(invI0, invI1, wDir));
    if(kInvI <= 1.0e-10f)
        return;
    const float effI = 1.0f / kInvI;
    const float rawT = (fieldType == 9u)
        ? (0.25f * strength * max(0.1f, maxExtent) * wSp * wSp)
        : (strength * effI * wSp);
    const float maxT = effI * wSp * invDt * 0.9f;
    inoutTorque -= wDir * min(rawT, maxT);
}

float nwbPhysicsSubsysFluidWaveOffset(
    const NwbPhysicsGpuFluidVolume fl,
    const float3 pos,
    const float3 t1,
    const float3 t2,
    const float timeSec,
    out float3 outSlope
){
    outSlope = float3(0.0f, 0.0f, 0.0f);
    const float waveAmp = fl.dragLiftWaveAmpFreq.z;
    if(waveAmp == 0.0f)
        return 0.0f;
    const float waveFreq = fl.dragLiftWaveAmpFreq.w;
    const float waveSpd = fl.waveSpeedAngAddedFlags.x;
    const float u = dot(pos, t1) * waveFreq + timeSec * waveSpd;
    const float v = dot(pos, t2) * (waveFreq * 0.77f) - timeSec * (waveSpd * 0.83f);
    float2 sV;
    float2 cV;
    sincos(float2(u, v), sV, cV);
    outSlope = t1 * (waveAmp * waveFreq * cV.x * cV.y) + t2 * (-waveAmp * (waveFreq * 0.77f) * sV.x * sV.y);
    return waveAmp * (sV.x * cV.y);
}

void nwbPhysicsSubsysFluidEvalSurface(
    const NwbPhysicsGpuFluidVolume fl,
    const uint gerstnerWaveSlot,
    const float3 pos,
    const float3 surfN,
    const float timeSec,
    out float outDepth,
    out float3 outFlowVel,
    out float3 outSurfNormal
){
    const uint shapeType = asuint(fl.boundsMinShape.w);
    outFlowVel = fl.flowVelLinDrag.xyz;
    outSurfNormal = surfN;
    float3 waveT1;
    float3 waveT2;
    nwbPhysicsComputeTangentBasis(surfN, waveT1, waveT2);

    if(shapeType == 0u){
        float3 slope;
        const float waveOff = nwbPhysicsSubsysFluidWaveOffset(fl, pos, waveT1, waveT2, timeSec, slope);
        outDepth = (fl.surfaceNormalOffset.w + waveOff) - dot(pos, surfN);
        outSurfNormal = nwbPhysicsSafeNormal(surfN - slope, surfN);
        return;
    }

    if(shapeType == 7u){
        const uint gerstnerPack = asuint(fl.obbHalfExtentsGerstner.w);
        const uint gOff = gerstnerPack & 0xffffu;
        const uint gCnt = gerstnerPack >> 16u;
        float waveSum = 0.0f;
        float3 slope = float3(0.0f, 0.0f, 0.0f);
        float3 orbitalVel = float3(0.0f, 0.0f, 0.0f);
        if(gCnt == 0u || gerstnerWaveSlot == NWB_PHYSICS_INVALID_INDEX){
            const float waveAmp = fl.dragLiftWaveAmpFreq.z;
            if(waveAmp != 0.0f){
                const float k = fl.dragLiftWaveAmpFreq.w;
                const float waveSpd = fl.waveSpeedAngAddedFlags.x;
                const float phase = dot(pos, waveT1) * k - timeSec * waveSpd;
                float sV = 0.0f;
                float cV = 0.0f;
                sincos(phase, sV, cV);
                waveSum = waveAmp * sV;
                slope = waveT1 * (waveAmp * k * cV);
                orbitalVel = waveT1 * (waveAmp * waveSpd * sV);
            }
        }
        else{
            const float twoPi = 6.2831853071795865f;
            [loop] for(uint wi = 0u; wi < gCnt; ++wi){
                const float4 dirSteep = g_NwbPhysicsFloat4Buffers[gerstnerWaveSlot][(gOff + wi) * 2u + 0u];
                const float4 ampWlSpdPh = g_NwbPhysicsFloat4Buffers[gerstnerWaveSlot][(gOff + wi) * 2u + 1u];
                const float3 rawDir = dirSteep.xyz;
                const float3 projDir = nwbPhysicsSafeNormal(rawDir - surfN * dot(rawDir, surfN), waveT1);
                const float k = twoPi / max(0.05f, ampWlSpdPh.y);
                const float steep = clamp(dirSteep.w, 0.0f, 1.0f);
                const float phase = k * dot(pos, projDir) - (k * ampWlSpdPh.z) * timeSec + ampWlSpdPh.w;
                float sV = 0.0f;
                float cV = 0.0f;
                sincos(phase, sV, cV);
                waveSum += ampWlSpdPh.x * sV;
                const float denom = max(0.2f, 1.0f - steep * k * ampWlSpdPh.x * sV * 0.5f);
                slope += projDir * ((ampWlSpdPh.x * k * cV) / denom);
                orbitalVel += projDir * (ampWlSpdPh.x * k * ampWlSpdPh.z * sV);
            }
        }
        outFlowVel += orbitalVel;
        outDepth = (fl.surfaceNormalOffset.w + waveSum) - dot(pos, surfN);
        outSurfNormal = nwbPhysicsSafeNormal(surfN - slope, surfN);
        return;
    }

    if(shapeType == 2u){
        const float r = max(0.0f, fl.surfaceNormalOffset.w);
        const float3 diff = pos - fl.surfaceNormalOffset.xyz;
        outDepth = r - length(diff);
        outSurfNormal = nwbPhysicsSafeNormal(diff, surfN);
        return;
    }

    if(shapeType == 3u){
        const float3 c = fl.obbCenterSplash.xyz;
        const float4 r = nwbPhysicsQuatNormalize(fl.obbRotation);
        const float3 he = max(fl.obbHalfExtentsGerstner.xyz, float3(1.0e-3f, 1.0e-3f, 1.0e-3f));
        const float3 pL = nwbPhysicsQuatInvRotate(pos - c, r);
        const float3 upObb = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(float3(0.0f, 1.0f, 0.0f), r), surfN);
        if(pL.x < -he.x || pL.x > he.x || pL.y < -he.y || pL.z < -he.z || pL.z > he.z){
            const float3 q = clamp(pL, -he, he);
            outDepth = -length(pL - q);
            if(pL.y > he.y && pL.x >= -he.x && pL.x <= he.x && pL.z >= -he.z && pL.z <= he.z)
                outSurfNormal = upObb;
            else
                outSurfNormal = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(pL - q, r), surfN);
            return;
        }
        float3 slope;
        const float waveOff = nwbPhysicsSubsysFluidWaveOffset(fl, pos, waveT1, waveT2, timeSec, slope);
        outDepth = (he.y + waveOff) - pL.y;
        outSurfNormal = nwbPhysicsSafeNormal(upObb - slope, surfN);
        return;
    }

    const float3 bMin = fl.boundsMinShape.xyz;
    const float3 bMax = fl.boundsMaxDensity.xyz;
    if(pos.x < bMin.x || pos.x > bMax.x || pos.y < bMin.y || pos.z < bMin.z || pos.z > bMax.z){
        const float3 q = clamp(pos, bMin, bMax);
        outDepth = -length(pos - q);
        if(pos.y > bMax.y && pos.x >= bMin.x && pos.x <= bMax.x && pos.z >= bMin.z && pos.z <= bMax.z)
            outSurfNormal = float3(0.0f, 1.0f, 0.0f);
        else
            outSurfNormal = nwbPhysicsSafeNormal(pos - q, surfN);
        return;
    }
    float3 boxSlope;
    const float boxWaveOff = nwbPhysicsSubsysFluidWaveOffset(fl, pos, waveT1, waveT2, timeSec, boxSlope);
    outDepth = (bMax.y + boxWaveOff) - pos.y;
    outSurfNormal = nwbPhysicsSafeNormal(float3(0.0f, 1.0f, 0.0f) - boxSlope, surfN);
}

bool nwbPhysicsSubsysCollideParticleShape(
    const NwbPhysicsGpuShapeProxy sp,
    const float3 sOrig,
    const float4 sRot,
    const uint hullSlot,
    const uint meshBvhSlot,
    const uint meshTriSlot,
    const float3 curPos,
    const float pRadius,
    out float3 outNormal,
    out float outPen
){
    outNormal = float3(0.0f, 1.0f, 0.0f);
    outPen = 0.0f;
    const uint sType = sp.bodyIndexTypeShape.y;
    const float sR = sp.localCenterRadius.w;
    const float sumR = sR + pRadius;
    const float3 lp = nwbPhysicsQuatInvRotate(curPos - sOrig, sRot);

    if(sType == NWB_PHYSICS_SHAPE_TYPE_SPHERE){
        const float3 d = lp - sp.localCenterRadius.xyz;
        const float dSq = dot(d, d);
        if(dSq >= sumR * sumR)
            return false;
        const float dist = sqrt(dSq);
        outNormal = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(d, sRot), float3(0.0f, 1.0f, 0.0f));
        outPen = sumR - dist;
        return outPen > 1.0e-5f;
    }

    if(sType == NWB_PHYSICS_SHAPE_TYPE_CAPSULE || sType == NWB_PHYSICS_SHAPE_TYPE_SEGMENT || sType == NWB_PHYSICS_SHAPE_TYPE_TAPERED_CAPSULE){
        float tSeg = 0.0f;
        const float3 q = nwbPhysicsClosestPointOnSegment(lp, sp.localCenterRadius.xyz, sp.localExtentOrP1.xyz, tSeg);
        const float3 d = lp - q;
        float effR = sumR;
        if(sType == NWB_PHYSICS_SHAPE_TYPE_SEGMENT)
            effR = max(sR, NWB_PHYSICS_LINEAR_SLOP) + pRadius;
        else if(sType == NWB_PHYSICS_SHAPE_TYPE_TAPERED_CAPSULE)
            effR += lerp(sp.localRotationOrMeta.x, sp.localRotationOrMeta.y, tSeg);
        const float dSq = dot(d, d);
        if(dSq >= effR * effR)
            return false;
        const float dist = sqrt(dSq);
        outNormal = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(d, sRot), float3(0.0f, 1.0f, 0.0f));
        outPen = effR - dist;
        return outPen > 1.0e-5f;
    }

    if(sType == NWB_PHYSICS_SHAPE_TYPE_PLANE){
        const float4 pq = sp.localRotationOrMeta;
        const float3 rel = nwbPhysicsQuatInvRotate(lp - sp.localCenterRadius.xyz, pq);
        const float3 hExt = sp.localExtentOrP1.xyz;
        if(hExt.x > 0.0f && hExt.z > 0.0f){
            if(abs(rel.x) > hExt.x + pRadius || abs(rel.y) > hExt.z + pRadius)
                return false;
        }
        if(rel.z >= pRadius || rel.z < -max(hExt.y, 0.25f) - pRadius)
            return false;
        const float3 worldPlN = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(nwbPhysicsQuatRotate(float3(0.0f, 0.0f, 1.0f), pq), sRot), float3(0.0f, 1.0f, 0.0f));
        outNormal = worldPlN;
        outPen = pRadius - rel.z;
        return outPen > 1.0e-5f;
    }

    if(sType == NWB_PHYSICS_SHAPE_TYPE_HULL && (sp.bodyIndexTypeShape.w & NWB_PHYSICS_SHAPE_FLAG_IS_BOX) != 0u){
        const float4 bq = sp.localRotationOrMeta;
        const float3 rel = nwbPhysicsQuatInvRotate(lp - sp.localCenterRadius.xyz, bq);
        const float3 h = sp.localExtentOrP1.xyz;
        const float3 clamped = clamp(rel, -h, h);
        const float3 d = rel - clamped;
        const float dSq = dot(d, d);
        if(dSq > 1.0e-12f){
            if(dSq >= sumR * sumR)
                return false;
            outNormal = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(nwbPhysicsQuatRotate(d, bq), sRot), float3(0.0f, 1.0f, 0.0f));
            outPen = sumR - sqrt(dSq);
            return outPen > 1.0e-5f;
        }
        const float3 dFace = h - abs(rel);
        float3 nLoc = float3(rel.x >= 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f);
        float minD = dFace.x;
        if(dFace.y < minD){
            minD = dFace.y;
            nLoc = float3(0.0f, rel.y >= 0.0f ? 1.0f : -1.0f, 0.0f);
        }
        if(dFace.z < minD){
            minD = dFace.z;
            nLoc = float3(0.0f, 0.0f, rel.z >= 0.0f ? 1.0f : -1.0f);
        }
        outNormal = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(nwbPhysicsQuatRotate(nLoc, bq), sRot), float3(0.0f, 1.0f, 0.0f));
        outPen = minD + sumR;
        return outPen > 1.0e-5f;
    }

    if(sType == NWB_PHYSICS_SHAPE_TYPE_HULL && hullSlot != NWB_PHYSICS_INVALID_INDEX && sp.filterData.w != NWB_PHYSICS_INVALID_INDEX){
        const uint meta = asuint(sp.localRotationOrMeta.x);
        const uint vCnt = min(meta & 0xffu, NWB_PHYSICS_MAX_HULL_VERTICES);
        const uint fCnt = min((meta >> 8u) & 0xffu, NWB_PHYSICS_MAX_HULL_FACES);
        const uint fBase = sp.filterData.w + vCnt;
        float maxD = -1.0e20f;
        float3 bestN = float3(0.0f, 1.0f, 0.0f);
        [loop] for(uint fi = 0u; fi < fCnt; ++fi){
            const float4 pl = g_NwbPhysicsFloat4Buffers[hullSlot][fBase + fi];
            const float d = dot(lp, pl.xyz) - pl.w;
            if(d > maxD){
                maxD = d;
                bestN = pl.xyz;
            }
        }
        if(maxD >= sumR)
            return false;
        outNormal = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(bestN, sRot), float3(0.0f, 1.0f, 0.0f));
        outPen = sumR - maxD;
        return outPen > 1.0e-5f;
    }

    if(sType == NWB_PHYSICS_SHAPE_TYPE_CYLINDER){
        const float4 cq = sp.localRotationOrMeta;
        const float3 rel = nwbPhysicsQuatInvRotate(lp - sp.localCenterRadius.xyz, cq);
        const float rC = max(sp.localExtentOrP1.x, 0.0f);
        const float hh = max(sp.localExtentOrP1.y, 0.0f);
        const float rad = sqrt(rel.x * rel.x + rel.z * rel.z);
        if(rad > rC + sumR || abs(rel.y) > hh + sumR)
            return false;
        const float3 rDir = (rad > 1.0e-6f) ? float3(rel.x / rad, 0.0f, rel.z / rad) : float3(1.0f, 0.0f, 0.0f);
        const float3 clamped = rDir * min(rad, rC) + float3(0.0f, clamp(rel.y, -hh, hh), 0.0f);
        const float3 d = rel - clamped;
        const float dLen = length(d);
        if(dLen >= sumR)
            return false;
        const float3 nLoc = (dLen > 1.0e-6f) ? (d / dLen) : ((hh - abs(rel.y) < rC - rad) ? float3(0.0f, rel.y >= 0.0f ? 1.0f : -1.0f, 0.0f) : rDir);
        outNormal = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(nwbPhysicsQuatRotate(nLoc, cq), sRot), float3(0.0f, 1.0f, 0.0f));
        outPen = (dLen > 1.0e-6f) ? (sumR - dLen) : (min(rC - rad, hh - abs(rel.y)) + sumR);
        return outPen > 1.0e-5f;
    }

    if(sType == NWB_PHYSICS_SHAPE_TYPE_CONE){
        const float4 cq = sp.localRotationOrMeta;
        const float3 rel = nwbPhysicsQuatInvRotate(lp - sp.localCenterRadius.xyz, cq);
        const float rC = max(sp.localExtentOrP1.x, 1.0e-4f);
        const float hh = max(sp.localExtentOrP1.y, 1.0e-4f);
        const float rad = sqrt(rel.x * rel.x + rel.z * rel.z);
        if(rad > rC + sumR || rel.y < -hh - sumR || rel.y > hh + sumR)
            return false;
        const float2 p2 = float2(rad, rel.y);
        const float2 apex = float2(0.0f, hh);
        const float2 rim = float2(rC, -hh);
        const float2 ba = rim - apex;
        const float baLenSq = max(dot(ba, ba), 1.0e-12f);
        const float tSlant = clamp(dot(p2 - apex, ba) / baLenSq, 0.0f, 1.0f);
        const float2 dSlant = p2 - (apex + ba * tSlant);
        const float2 dBase = p2 - float2(clamp(rad, 0.0f, rC), -hh);
        const float dSlantSq = dot(dSlant, dSlant);
        const float dBaseSq = dot(dBase, dBase);
        const bool inside = (rel.y >= -hh) && (rel.y <= hh) && (2.0f * hh * rad + rC * (rel.y - hh) <= 0.0f);
        const float bestSq = min(dSlantSq, dBaseSq);
        const float sDist = inside ? -sqrt(bestSq) : sqrt(bestSq);
        if(sDist >= sumR)
            return false;
        float2 n2;
        if(dSlantSq <= dBaseSq){
            if(!inside && dSlantSq > 1.0e-12f && (tSlant <= 1.0e-5f || tSlant >= 1.0f - 1.0e-5f))
                n2 = normalize(dSlant);
            else
                n2 = normalize(float2(2.0f * hh, rC));
        }
        else{
            n2 = (!inside && dBaseSq > 1.0e-12f) ? normalize(dBase) : float2(0.0f, -1.0f);
        }
        const float3 rDir = (rad > 1.0e-6f) ? float3(rel.x / rad, 0.0f, rel.z / rad) : float3(1.0f, 0.0f, 0.0f);
        const float3 nLoc = nwbPhysicsSafeNormal(rDir * n2.x + float3(0.0f, n2.y, 0.0f), float3(0.0f, 1.0f, 0.0f));
        outNormal = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(nwbPhysicsQuatRotate(nLoc, cq), sRot), float3(0.0f, 1.0f, 0.0f));
        outPen = sumR - sDist;
        return outPen > 1.0e-5f;
    }

    if(sType == NWB_PHYSICS_SHAPE_TYPE_HEIGHTFIELD && hullSlot != NWB_PHYSICS_INVALID_INDEX && sp.filterData.w != NWB_PHYSICS_INVALID_INDEX){
        const uint hfBase = sp.filterData.w;
        NwbPhysicsHfHeader hf;
        if(!nwbPhysicsHfLoadHeader(hullSlot, hfBase, hf))
            return false;
        const float3 hfOrg = hf.origin;
        const float sx = hf.sx;
        const float sy = hf.sy;
        const float sz = hf.sz;
        const uint cols = hf.cols;
        const uint rows = hf.rows;
        const uint hfFlags = hf.flags;
        const float thick = max(hf.thickness, 0.0f);
        const int maxCellX = int(cols - 2u);
        const int maxCellZ = int(rows - 2u);
        const int minCx = max(0, int(floor((lp.x - pRadius - hfOrg.x) / sx)));
        const int maxCx = min(maxCellX, int(floor((lp.x + pRadius - hfOrg.x) / sx)));
        const int minCz = max(0, int(floor((lp.z - pRadius - hfOrg.z) / sz)));
        const int maxCz = min(maxCellZ, int(floor((lp.z + pRadius - hfOrg.z) / sz)));
        if(minCx > maxCx || minCz > maxCz)
            return false;
        const bool flipD = (hfFlags & NWB_PHYSICS_HF_FLAG_FLIP_DIAGONAL) != 0u;
        float bestPen = 0.0f;
        float3 bestNormLocal = float3(0.0f, 1.0f, 0.0f);
        bool hitTri = false;
        [loop] for(int czI = minCz; czI <= maxCz; ++czI){
            [loop] for(int cxI = minCx; cxI <= maxCx; ++cxI){
                float3 v00;
                float3 v10;
                float3 v01;
                float3 v11;
                if(!nwbPhysicsHfLoadCellVertices(hullSlot, hfBase, cols, rows, hfOrg, sx, sz, sy, hfFlags, uint(cxI), uint(czI), v00, v10, v01, v11))
                    continue;
                [loop] for(uint ti = 0u; ti < 2u; ++ti){
                    float3 tv0;
                    float3 tv1;
                    float3 tv2;
                    nwbPhysicsHfExtractTriangle(flipD, ti, v00, v10, v01, v11, tv0, tv1, tv2);
                    const float3 tN = nwbPhysicsHfTriNormal(tv0, tv1, tv2);
                    const float3 q = nwbPhysicsClosestPointOnTriangle(lp, tv0, tv1, tv2);
                    const float3 diff = lp - q;
                    const float dSq = dot(diff, diff);
                    const float signedD = dot(diff, tN);
                    if(signedD > -(thick + pRadius) && signedD < pRadius && dSq < (pRadius + thick) * (pRadius + thick)){
                        const float pen = pRadius - signedD;
                        if(pen > bestPen){
                            bestPen = pen;
                            bestNormLocal = (dSq > 1.0e-12f && signedD >= 0.0f) ? (diff * rsqrt(dSq)) : tN;
                            hitTri = true;
                        }
                    }
                }
            }
        }
        if(!hitTri || bestPen <= 1.0e-5f)
            return false;
        outNormal = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(bestNormLocal, sRot), float3(0.0f, 1.0f, 0.0f));
        outPen = bestPen;
        return true;
    }

    if(
        sType == NWB_PHYSICS_SHAPE_TYPE_MESH
        && meshBvhSlot != NWB_PHYSICS_INVALID_INDEX
        && meshTriSlot != NWB_PHYSICS_INVALID_INDEX
        && sp.filterData.w != NWB_PHYSICS_INVALID_INDEX
        && asuint(sp.localRotationOrMeta.x) != NWB_PHYSICS_INVALID_INDEX
    ){
        const uint nodeBase = asuint(sp.localRotationOrMeta.x);
        const uint triBase = sp.filterData.w;
        const float rSqLimit = pRadius * pRadius;
        const float3 rVec = float3(pRadius, pRadius, pRadius);
        const float3 qMin = lp - rVec;
        const float3 qMax = lp + rVec;

        uint stack[NWB_PHYSICS_MESH_BVH_STACK_SIZE];
        uint spIdx = 1u;
        stack[0] = 0u;
        float bestDistSq = rSqLimit;
        float3 bestDiff = float3(0.0f, 0.0f, 0.0f);
        float3 bestFaceCross = float3(0.0f, 1.0f, 0.0f);
        bool found = false;

        [loop] while(spIdx > 0u){
            --spIdx;
            const uint ni = stack[spIdx];
            const float4 n0 = g_NwbPhysicsFloat4Buffers[meshBvhSlot][(nodeBase + ni) * 2u + 0u];
            const float4 n1 = g_NwbPhysicsFloat4Buffers[meshBvhSlot][(nodeBase + ni) * 2u + 1u];
            if(!nwbPhysicsAabbOverlaps(qMin, qMax, n0.xyz, n1.xyz))
                continue;
            const uint meta = asuint(n1.w);
            if((meta & 0x80000000u) != 0u){
                const uint firstTri = asuint(n0.w);
                const uint triCount = meta & 0x7fffffffu;
                [loop] for(uint ti = 0u; ti < triCount; ++ti){
                    const uint triIdx = triBase + firstTri + ti;
                    const float3 tv0 = g_NwbPhysicsFloat4Buffers[meshTriSlot][triIdx * 3u + 0u].xyz;
                    const float3 tv1 = g_NwbPhysicsFloat4Buffers[meshTriSlot][triIdx * 3u + 1u].xyz;
                    const float3 tv2 = g_NwbPhysicsFloat4Buffers[meshTriSlot][triIdx * 3u + 2u].xyz;
                    const float3 closePt = nwbPhysicsClosestPointOnTriangle(lp, tv0, tv1, tv2);
                    const float3 diff = lp - closePt;
                    const float dSq = dot(diff, diff);
                    if(dSq < bestDistSq){
                        bestDistSq = dSq;
                        bestDiff = diff;
                        bestFaceCross = cross(tv1 - tv0, tv2 - tv0);
                        found = true;
                    }
                }
            }
            else if(spIdx + 2u <= NWB_PHYSICS_MESH_BVH_STACK_SIZE){
                stack[spIdx++] = asuint(n0.w);
                stack[spIdx++] = meta & 0x7fffffffu;
            }
        }
        if(!found)
            return false;
        const float dist = sqrt(bestDistSq);
        outPen = pRadius - dist;
        if(outPen <= 1.0e-5f)
            return false;
        const float3 localN = (dist > 1.0e-6f) ? (bestDiff / dist) : nwbPhysicsSafeNormal(bestFaceCross, float3(0.0f, 1.0f, 0.0f));
        outNormal = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(localN, sRot), float3(0.0f, 1.0f, 0.0f));
        return true;
    }

    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_PHYSICS_SUBSYS_WAVE_SLOTS 16u

groupshared float3 s_SoftVec0[NWB_PHYSICS_WORKGROUP_SIZE];
groupshared float3 s_SoftVec1[NWB_PHYSICS_WORKGROUP_SIZE];
groupshared float3 s_SoftVec2[NWB_PHYSICS_SUBSYS_WAVE_SLOTS];
groupshared float s_SoftScalar0[NWB_PHYSICS_SUBSYS_WAVE_SLOTS];
groupshared uint s_SoftUint0[NWB_PHYSICS_SUBSYS_WAVE_SLOTS];
groupshared float3 s_SoftCenter;

[require(spvGroupNonUniform, spvGroupNonUniformArithmetic)]
void nwbPhysicsSubsysReduceMinMaxAndUintSum(
    const uint gtid,
    const float3 localMin,
    const float3 localMax,
    const uint localCount,
    out float3 outMin,
    out float3 outMax,
    out uint outCount
){
    const float3 wMin = WaveActiveMin(localMin);
    const float3 wMax = WaveActiveMax(localMax);
    const uint wCnt = WaveActiveSum(localCount);
    const uint laneCnt = WaveGetLaneCount();
    if(laneCnt >= NWB_PHYSICS_WORKGROUP_SIZE){
        outMin = wMin;
        outMax = wMax;
        outCount = wCnt;
        return;
    }
    const uint waveId = gtid / laneCnt;
    const uint numWaves = (NWB_PHYSICS_WORKGROUP_SIZE + laneCnt - 1u) / laneCnt;
    if(WaveIsFirstLane() && waveId < NWB_PHYSICS_SUBSYS_WAVE_SLOTS){
        s_SoftVec0[waveId] = wMin;
        s_SoftVec1[waveId] = wMax;
        s_SoftUint0[waveId] = wCnt;
    }
    GroupMemoryBarrierWithGroupSync();
    if(waveId == 0u && laneCnt >= numWaves){
        const bool activeWave = gtid < numWaves;
        const float3 rMin = WaveActiveMin(activeWave ? s_SoftVec0[gtid] : float3(1.0e30f, 1.0e30f, 1.0e30f));
        const float3 rMax = WaveActiveMax(activeWave ? s_SoftVec1[gtid] : float3(-1.0e30f, -1.0e30f, -1.0e30f));
        const uint rCnt = WaveActiveSum(activeWave ? s_SoftUint0[gtid] : 0u);
        if(gtid == 0u){
            s_SoftVec0[0] = rMin;
            s_SoftVec1[0] = rMax;
            s_SoftUint0[0] = rCnt;
        }
    }
    else if(gtid == 0u){
        float3 accMin = s_SoftVec0[0];
        float3 accMax = s_SoftVec1[0];
        uint accCnt = s_SoftUint0[0];
        const uint cap = min(numWaves, NWB_PHYSICS_SUBSYS_WAVE_SLOTS);
        [loop] for(uint wi = 1u; wi < cap; ++wi){
            accMin = min(accMin, s_SoftVec0[wi]);
            accMax = max(accMax, s_SoftVec1[wi]);
            accCnt += s_SoftUint0[wi];
        }
        s_SoftVec0[0] = accMin;
        s_SoftVec1[0] = accMax;
        s_SoftUint0[0] = accCnt;
    }
    GroupMemoryBarrierWithGroupSync();
    outMin = s_SoftVec0[0];
    outMax = s_SoftVec1[0];
    outCount = s_SoftUint0[0];
}

[require(spvGroupNonUniform, spvGroupNonUniformArithmetic)]
float nwbPhysicsSubsysReduceFloatMax(const uint gtid, const float localVal){
    const float wMax = WaveActiveMax(localVal);
    const uint laneCnt = WaveGetLaneCount();
    if(laneCnt >= NWB_PHYSICS_WORKGROUP_SIZE)
        return wMax;
    const uint waveId = gtid / laneCnt;
    const uint numWaves = (NWB_PHYSICS_WORKGROUP_SIZE + laneCnt - 1u) / laneCnt;
    if(WaveIsFirstLane() && waveId < NWB_PHYSICS_SUBSYS_WAVE_SLOTS)
        s_SoftScalar0[waveId] = wMax;
    GroupMemoryBarrierWithGroupSync();
    if(waveId == 0u && laneCnt >= numWaves){
        const float rMax = WaveActiveMax((gtid < numWaves) ? s_SoftScalar0[gtid] : 0.0f);
        if(gtid == 0u)
            s_SoftScalar0[0] = rMax;
    }
    else if(gtid == 0u){
        float acc = s_SoftScalar0[0];
        const uint cap = min(numWaves, NWB_PHYSICS_SUBSYS_WAVE_SLOTS);
        [loop] for(uint wi = 1u; wi < cap; ++wi)
            acc = max(acc, s_SoftScalar0[wi]);
        s_SoftScalar0[0] = acc;
    }
    GroupMemoryBarrierWithGroupSync();
    return s_SoftScalar0[0];
}

[require(spvGroupNonUniform, spvGroupNonUniformArithmetic)]
float nwbPhysicsSubsysReduceFloatMin(const uint gtid, const float localVal){
    const float wMin = WaveActiveMin(localVal);
    const uint laneCnt = WaveGetLaneCount();
    if(laneCnt >= NWB_PHYSICS_WORKGROUP_SIZE)
        return wMin;
    const uint waveId = gtid / laneCnt;
    const uint numWaves = (NWB_PHYSICS_WORKGROUP_SIZE + laneCnt - 1u) / laneCnt;
    if(WaveIsFirstLane() && waveId < NWB_PHYSICS_SUBSYS_WAVE_SLOTS)
        s_SoftScalar0[waveId] = wMin;
    GroupMemoryBarrierWithGroupSync();
    if(waveId == 0u && laneCnt >= numWaves){
        const float rMin = WaveActiveMin((gtid < numWaves) ? s_SoftScalar0[gtid] : 1.0f);
        if(gtid == 0u)
            s_SoftScalar0[0] = rMin;
    }
    else if(gtid == 0u){
        float acc = s_SoftScalar0[0];
        const uint cap = min(numWaves, NWB_PHYSICS_SUBSYS_WAVE_SLOTS);
        [loop] for(uint wi = 1u; wi < cap; ++wi)
            acc = min(acc, s_SoftScalar0[wi]);
        s_SoftScalar0[0] = acc;
    }
    GroupMemoryBarrierWithGroupSync();
    return s_SoftScalar0[0];
}

[require(spvGroupNonUniform, spvGroupNonUniformArithmetic)]
void nwbPhysicsSubsysReduceSum3AndUint(
    const uint gtid,
    const float3 localVec,
    const uint localCount,
    out float3 outVec,
    out uint outCount
){
    const float3 wVec = WaveActiveSum(localVec);
    const uint wCnt = WaveActiveSum(localCount);
    const uint laneCnt = WaveGetLaneCount();
    if(laneCnt >= NWB_PHYSICS_WORKGROUP_SIZE){
        outVec = wVec;
        outCount = wCnt;
        return;
    }
    const uint waveId = gtid / laneCnt;
    const uint numWaves = (NWB_PHYSICS_WORKGROUP_SIZE + laneCnt - 1u) / laneCnt;
    if(WaveIsFirstLane() && waveId < NWB_PHYSICS_SUBSYS_WAVE_SLOTS){
        s_SoftVec2[waveId] = wVec;
        s_SoftUint0[waveId] = wCnt;
    }
    GroupMemoryBarrierWithGroupSync();
    if(waveId == 0u && laneCnt >= numWaves){
        const bool activeWave = gtid < numWaves;
        const float3 rVec = WaveActiveSum(activeWave ? s_SoftVec2[gtid] : float3(0.0f, 0.0f, 0.0f));
        const uint rCnt = WaveActiveSum(activeWave ? s_SoftUint0[gtid] : 0u);
        if(gtid == 0u){
            s_SoftVec2[0] = rVec;
            s_SoftUint0[0] = rCnt;
        }
    }
    else if(gtid == 0u){
        float3 accVec = s_SoftVec2[0];
        uint accCnt = s_SoftUint0[0];
        const uint cap = min(numWaves, NWB_PHYSICS_SUBSYS_WAVE_SLOTS);
        [loop] for(uint wi = 1u; wi < cap; ++wi){
            accVec += s_SoftVec2[wi];
            accCnt += s_SoftUint0[wi];
        }
        s_SoftVec2[0] = accVec;
        s_SoftUint0[0] = accCnt;
    }
    GroupMemoryBarrierWithGroupSync();
    outVec = s_SoftVec2[0];
    outCount = s_SoftUint0[0];
}

[require(spvGroupNonUniform, spvGroupNonUniformArithmetic)]
void nwbPhysicsSubsysReduceShapeMoments(
    const uint gtid,
    inout float3 ioMassRelPos,
    inout float ioMass,
    inout float3 ioSumR,
    inout float3 ioCol0,
    inout float3 ioCol1,
    inout float3 ioCol2
){
    const float3 wPos = WaveActiveSum(ioMassRelPos);
    const float wMass = WaveActiveSum(ioMass);
    const float3 wSumR = WaveActiveSum(ioSumR);
    const float3 wCol0 = WaveActiveSum(ioCol0);
    const float3 wCol1 = WaveActiveSum(ioCol1);
    const float3 wCol2 = WaveActiveSum(ioCol2);
    const uint laneCnt = WaveGetLaneCount();
    if(laneCnt >= NWB_PHYSICS_WORKGROUP_SIZE){
        ioMassRelPos = wPos;
        ioMass = wMass;
        ioSumR = wSumR;
        ioCol0 = wCol0;
        ioCol1 = wCol1;
        ioCol2 = wCol2;
        return;
    }
    const uint waveId = gtid / laneCnt;
    const uint numWaves = (NWB_PHYSICS_WORKGROUP_SIZE + laneCnt - 1u) / laneCnt;
    if(WaveIsFirstLane() && waveId < NWB_PHYSICS_SUBSYS_WAVE_SLOTS){
        s_SoftVec0[waveId] = wPos;
        s_SoftScalar0[waveId] = wMass;
        s_SoftVec2[waveId] = wSumR;
        s_SoftVec0[NWB_PHYSICS_SUBSYS_WAVE_SLOTS + waveId] = wCol0;
        s_SoftVec1[waveId] = wCol1;
        s_SoftVec1[NWB_PHYSICS_SUBSYS_WAVE_SLOTS + waveId] = wCol2;
    }
    GroupMemoryBarrierWithGroupSync();
    if(gtid == 0u){
        float3 accPos = s_SoftVec0[0];
        float accMass = s_SoftScalar0[0];
        float3 accSumR = s_SoftVec2[0];
        float3 accCol0 = s_SoftVec0[NWB_PHYSICS_SUBSYS_WAVE_SLOTS];
        float3 accCol1 = s_SoftVec1[0];
        float3 accCol2 = s_SoftVec1[NWB_PHYSICS_SUBSYS_WAVE_SLOTS];
        const uint cap = min(numWaves, NWB_PHYSICS_SUBSYS_WAVE_SLOTS);
        [loop] for(uint wi = 1u; wi < cap; ++wi){
            accPos += s_SoftVec0[wi];
            accMass += s_SoftScalar0[wi];
            accSumR += s_SoftVec2[wi];
            accCol0 += s_SoftVec0[NWB_PHYSICS_SUBSYS_WAVE_SLOTS + wi];
            accCol1 += s_SoftVec1[wi];
            accCol2 += s_SoftVec1[NWB_PHYSICS_SUBSYS_WAVE_SLOTS + wi];
        }
        ioMassRelPos = accPos;
        ioMass = accMass;
        ioSumR = accSumR;
        ioCol0 = accCol0;
        ioCol1 = accCol1;
        ioCol2 = accCol2;
    }
}

void nwbPhysicsSubsysSolveShapeMatchQuat(
    const uint shapeOutSlot,
    const float3 com,
    const float3 a0,
    const float3 a1,
    const float3 a2
){
    float4 q = g_NwbPhysicsFloat4Buffers[shapeOutSlot][1];
    const float qLenSq = dot(q, q);
    q = (nwbFiniteFloat4(q) && qLenSq > 1.0e-8f) ? (q * rsqrt(qLenSq)) : float4(0.0f, 0.0f, 0.0f, 1.0f);
    for(uint iter = 0u; iter < 10u; ++iter){
        float3 rx;
        float3 ry;
        float3 rz;
        nwbPhysicsQuatToAxes(q, rx, ry, rz);
        const float denom = abs(dot(rx, a0) + dot(ry, a1) + dot(rz, a2)) + 1.0e-9f;
        const float3 omega = (cross(rx, a0) + cross(ry, a1) + cross(rz, a2)) / denom;
        const float w = length(omega);
        if(w < 1.0e-9f)
            break;
        const float halfW = 0.5f * w;
        const float4 dq = float4((omega / w) * sin(halfW), cos(halfW));
        q = nwbPhysicsQuatNormalize(nwbPhysicsQuatMul(dq, q));
    }
    g_NwbPhysicsFloat4Buffers[shapeOutSlot][0] = float4(com, 1.0f);
    g_NwbPhysicsFloat4Buffers[shapeOutSlot][1] = q;
}


[require(spvGroupNonUniform, spvGroupNonUniformArithmetic)]
[numthreads(NWB_PHYSICS_WORKGROUP_SIZE, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID, uint3 groupThreadID : SV_GroupThreadID, uint3 groupID : SV_GroupID){
    const uint tid = dispatchThreadID.x;
    const uint gtid = groupThreadID.x;
    const uint itemCount = g_NwbPhysicsPushConstants.itemCount;
    const uint mode = g_NwbPhysicsPushConstants.modeOrSecondaryCount;
    const uint flags = g_NwbPhysicsPushConstants.flags;

    if(mode == NWB_PHYSICS_SUBSYS_MODE_SOFT_BACKSTOP_COLLIDE && flags != 0u){
        const uint pSlot = g_NwbPhysicsPushConstants.slots0.x;
        const uint stateSlot = g_NwbPhysicsPushConstants.slots0.y;
        const uint shapeSlot = g_NwbPhysicsPushConstants.slots0.z;
        const uint bodyPoseSlot = g_NwbPhysicsPushConstants.slots0.w;
        const uint hullSlot = g_NwbPhysicsPushConstants.slots1.x;
        const uint worldAabbSlot = g_NwbPhysicsPushConstants.slots1.y;
        const uint meshBvhSlot = g_NwbPhysicsPushConstants.slots1.z;
        const uint meshTriSlot = g_NwbPhysicsPushConstants.slots1.w;
        const uint worldShapeCount = g_NwbPhysicsPushConstants.itemOffset;
        const float fricScale = g_NwbPhysicsPushConstants.deltaTime;
        const float restCoeff = clamp(g_NwbPhysicsPushConstants.invDeltaTime, 0.0f, 1.0f);
        const float pRadius = g_NwbPhysicsPushConstants.maxLinearVelocity;
        const float3 radVec = float3(pRadius, pRadius, pRadius);

        bool canCollide = false;
        float3 curPos = float3(0.0f, 0.0f, 0.0f);
        float3 prevPos = float3(0.0f, 0.0f, 0.0f);
        if(tid < itemCount){
            const float4 posInvMass = g_NwbPhysicsFloat4Buffers[pSlot][tid];
            if(posInvMass.w > 0.0f){
                const float4 prevPosFlags = g_NwbPhysicsSoftParticleStateBuffers[stateSlot][tid].prevPosFlags;
                if((asuint(prevPosFlags.w) & 1u) == 0u){
                    canCollide = true;
                    curPos = posInvMass.xyz;
                    prevPos = prevPosFlags.xyz;
                }
            }
        }

        [loop] for(uint tileBase = 0u; tileBase < worldShapeCount; tileBase += NWB_PHYSICS_WORKGROUP_SIZE){
            const uint loadIdx = tileBase + gtid;
            if(loadIdx < worldShapeCount && worldAabbSlot != NWB_PHYSICS_INVALID_INDEX){
                s_SoftVec0[gtid] = g_NwbPhysicsFloat4Buffers[worldAabbSlot][loadIdx * 2u + 0u].xyz;
                s_SoftVec1[gtid] = g_NwbPhysicsFloat4Buffers[worldAabbSlot][loadIdx * 2u + 1u].xyz;
            }
            GroupMemoryBarrierWithGroupSync();

            if(canCollide){
                float3 pMin = curPos - radVec;
                float3 pMax = curPos + radVec;
                const uint tileEnd = min(NWB_PHYSICS_WORKGROUP_SIZE, worldShapeCount - tileBase);
                [loop] for(uint j = 0u; j < tileEnd; ++j){
                    if(worldAabbSlot != NWB_PHYSICS_INVALID_INDEX && (any(pMax < s_SoftVec0[j]) || any(pMin > s_SoftVec1[j])))
                        continue;
                    const NwbPhysicsGpuShapeProxy sp = g_NwbPhysicsShapeProxyBuffers[shapeSlot][tileBase + j];
                    if((sp.bodyIndexTypeShape.w & NWB_PHYSICS_SHAPE_FLAG_SENSOR) != 0u)
                        continue;
                    const NwbPhysicsGpuBodyPose bp = g_NwbPhysicsBodyPoseBuffers[bodyPoseSlot][sp.bodyIndexTypeShape.x];
                    float3 sOrig = float3(0.0f, 0.0f, 0.0f);
                    float4 sRot = float4(0.0f, 0.0f, 0.0f, 1.0f);
                    nwbPhysicsResolveShapeWorldPose(sp, bp.originPad.xyz, bp.rotation, sOrig, sRot);
                    float3 outN = float3(0.0f, 1.0f, 0.0f);
                    float pen = 0.0f;
                    if(nwbPhysicsSubsysCollideParticleShape(sp, sOrig, sRot, hullSlot, meshBvhSlot, meshTriSlot, curPos, pRadius, outN, pen)){
                        const float3 disp = curPos - prevPos;
                        const float vn = dot(disp, outN);
                        const float3 tanDisp = disp - outN * vn;
                        curPos += outN * pen + tanDisp * (fricScale - 1.0f);
                        if(restCoeff > 0.0f && vn < -1.0e-4f){
                            const float curVn = dot(curPos - prevPos, outN);
                            const float targetVn = -restCoeff * vn;
                            if(targetVn > curVn)
                                prevPos -= outN * (targetVn - curVn);
                        }
                        pMin = curPos - radVec;
                        pMax = curPos + radVec;
                    }
                }
            }
            GroupMemoryBarrierWithGroupSync();
        }

        if(canCollide){
            g_NwbPhysicsFloat4Buffers[pSlot][tid].xyz = curPos;
            g_NwbPhysicsSoftParticleStateBuffers[stateSlot][tid].prevPosFlags.xyz = prevPos;
        }
        return;
    }

    if(mode == NWB_PHYSICS_SUBSYS_MODE_SOFT_SELF_COLLIDE){
        if(tid >= itemCount)
            return;
        const uint pSlot = g_NwbPhysicsPushConstants.slots0.x;
        const uint adjRangeSlot = g_NwbPhysicsPushConstants.slots0.y;
        const uint adjListSlot = g_NwbPhysicsPushConstants.slots0.z;
        const uint headSlot = g_NwbPhysicsPushConstants.slots0.w;
        if(flags == 2u){
            g_NwbPhysicsUintBuffers[headSlot][tid] = NWB_PHYSICS_INVALID_INDEX;
            return;
        }
        const uint nextSlot = g_NwbPhysicsPushConstants.slots1.x;
        const uint tableMask = g_NwbPhysicsPushConstants.slots1.y;
        const float minDist = g_NwbPhysicsPushConstants.deltaTime;
        const float invCell = (minDist > 1.0e-5f) ? (1.0f / minDist) : 1.0f;
        const float4 posSelf = g_NwbPhysicsFloat4Buffers[pSlot][tid];
        const float3 selfPos = posSelf.xyz;
        if(!nwbFiniteFloat3(selfPos))
            return;
        const int3 selfCell = int3(floor(selfPos * invCell));

        if(flags == 0u){
            const uint h = nwbPhysicsHashCell(selfCell, tableMask);
            uint prevHead = NWB_PHYSICS_INVALID_INDEX;
            InterlockedExchange(g_NwbPhysicsUintBuffers[headSlot][h], tid, prevHead);
            g_NwbPhysicsUintBuffers[nextSlot][tid] = prevHead;
            return;
        }

        const float wSelf = max(posSelf.w, 0.0f);
        const float stiff = clamp(g_NwbPhysicsPushConstants.invDeltaTime, 0.0f, 1.0f);
        if(wSelf <= 0.0f || minDist <= 1.0e-5f || stiff <= 0.0f)
            return;

        uint adjStart = 0u;
        uint adjCount = 0u;
        if(adjRangeSlot != NWB_PHYSICS_INVALID_INDEX && adjListSlot != NWB_PHYSICS_INVALID_INDEX){
            adjStart = g_NwbPhysicsUintBuffers[adjRangeSlot][tid * 2u + 0u];
            adjCount = g_NwbPhysicsUintBuffers[adjRangeSlot][tid * 2u + 1u];
        }

        const float minDistSq = minDist * minDist;
        float3 accumCorr = float3(0.0f, 0.0f, 0.0f);
        uint visitedBuckets[27];
        [unroll] for(uint bi = 0u; bi < 27u; ++bi)
            visitedBuckets[bi] = NWB_PHYSICS_INVALID_INDEX;
        uint visitedCount = 0u;
        for(int dz = -1; dz <= 1; ++dz){
            for(int dy = -1; dy <= 1; ++dy){
                for(int dx = -1; dx <= 1; ++dx){
                    const uint h = nwbPhysicsHashCell(selfCell + int3(dx, dy, dz), tableMask);
                    bool seen = false;
                    for(uint vb = 0u; vb < visitedCount; ++vb){
                        if(visitedBuckets[vb] == h){
                            seen = true;
                            break;
                        }
                    }
                    if(seen)
                        continue;
                    visitedBuckets[visitedCount++] = h;
                    uint otherIdx = g_NwbPhysicsUintBuffers[headSlot][h];
                    uint steps = 0u;
                    [loop] while(otherIdx != NWB_PHYSICS_INVALID_INDEX && steps < itemCount){
                        if(otherIdx != tid){
                            const float4 posOther = g_NwbPhysicsFloat4Buffers[pSlot][otherIdx];
                            const float3 diff = selfPos - posOther.xyz;
                            const float dSq = dot(diff, diff);
                            if(dSq < minDistSq && dSq > 1.0e-12f){
                                bool isAdj = false;
                                [loop] for(uint ai = 0u; ai < adjCount; ++ai){
                                    if(g_NwbPhysicsUintBuffers[adjListSlot][adjStart + ai] == otherIdx){
                                        isAdj = true;
                                        break;
                                    }
                                }
                                if(!isAdj){
                                    const float invD = rsqrt(dSq);
                                    const float d = dSq * invD;
                                    const float wOther = max(posOther.w, 0.0f);
                                    const float massRatio = wSelf / max(wSelf + wOther, 1.0e-12f);
                                    accumCorr += diff * (invD * (minDist - d) * stiff * massRatio);
                                }
                            }
                        }
                        otherIdx = g_NwbPhysicsUintBuffers[nextSlot][otherIdx];
                        ++steps;
                    }
                }
            }
        }

        if(dot(accumCorr, accumCorr) > 0.0f)
            g_NwbPhysicsFloat4Buffers[pSlot][tid].xyz = selfPos + accumCorr;
        return;
    }

    if(mode == NWB_PHYSICS_SUBSYS_MODE_SOFT_SHAPE_MATCH){
        if(itemCount == 0u)
            return;
        const uint pSlot = g_NwbPhysicsPushConstants.slots0.x;
        const uint restRelSlot = g_NwbPhysicsPushConstants.slots0.y;
        const uint shapeOutSlot = g_NwbPhysicsPushConstants.slots0.z;

        if(flags == 1u){
            if(tid >= itemCount)
                return;
            const float stiff = clamp(g_NwbPhysicsPushConstants.deltaTime, 0.0f, 1.0f);
            const float4 posInvMass = g_NwbPhysicsFloat4Buffers[pSlot][tid];
            if(posInvMass.w <= 0.0f || stiff <= 0.0f)
                return;
            const float4 comFlag = g_NwbPhysicsFloat4Buffers[shapeOutSlot][0];
            if(comFlag.w <= 0.0f)
                return;
            const float4 q = g_NwbPhysicsFloat4Buffers[shapeOutSlot][1];
            const float3 restRel = g_NwbPhysicsFloat4Buffers[restRelSlot][tid].xyz;
            const float3 target = comFlag.xyz + nwbPhysicsQuatRotate(restRel, q);
            g_NwbPhysicsFloat4Buffers[pSlot][tid].xyz = lerp(posInvMass.xyz, target, stiff);
            return;
        }

        if(flags == 3u){
            if(groupID.x != 0u)
                return;
            float3 sumMassRelPos = float3(0.0f, 0.0f, 0.0f);
            float sumMass = 0.0f;
            float3 sumR = float3(0.0f, 0.0f, 0.0f);
            float3 col0 = float3(0.0f, 0.0f, 0.0f);
            float3 col1 = float3(0.0f, 0.0f, 0.0f);
            float3 col2 = float3(0.0f, 0.0f, 0.0f);
            for(uint k = gtid; k < itemCount; k += NWB_PHYSICS_WORKGROUP_SIZE){
                const uint baseIdx = 2u + k * 5u;
                const float4 pm = g_NwbPhysicsFloat4Buffers[shapeOutSlot][baseIdx + 0u];
                sumMassRelPos += pm.xyz;
                sumMass += pm.w;
                sumR += g_NwbPhysicsFloat4Buffers[shapeOutSlot][baseIdx + 1u].xyz;
                col0 += g_NwbPhysicsFloat4Buffers[shapeOutSlot][baseIdx + 2u].xyz;
                col1 += g_NwbPhysicsFloat4Buffers[shapeOutSlot][baseIdx + 3u].xyz;
                col2 += g_NwbPhysicsFloat4Buffers[shapeOutSlot][baseIdx + 4u].xyz;
            }
            nwbPhysicsSubsysReduceShapeMoments(gtid, sumMassRelPos, sumMass, sumR, col0, col1, col2);
            if(gtid == 0u){
                if(sumMass <= 1.0e-12f){
                    g_NwbPhysicsFloat4Buffers[shapeOutSlot][0] = float4(0.0f, 0.0f, 0.0f, 0.0f);
                    g_NwbPhysicsFloat4Buffers[shapeOutSlot][1] = float4(0.0f, 0.0f, 0.0f, 1.0f);
                }
                else{
                    const float3 rawRef = g_NwbPhysicsFloat4Buffers[pSlot][0].xyz;
                    const float3 pRef = nwbFiniteFloat3(rawRef) ? rawRef : float3(0.0f, 0.0f, 0.0f);
                    const float3 relCom = sumMassRelPos / sumMass;
                    nwbPhysicsSubsysSolveShapeMatchQuat(
                        shapeOutSlot,
                        pRef + relCom,
                        col0 - relCom * sumR.x,
                        col1 - relCom * sumR.y,
                        col2 - relCom * sumR.z
                    );
                }
            }
            return;
        }

        if(flags == 0u && groupID.x != 0u)
            return;

        const float3 rawRef = g_NwbPhysicsFloat4Buffers[pSlot][0].xyz;
        const float3 pRef = nwbFiniteFloat3(rawRef) ? rawRef : float3(0.0f, 0.0f, 0.0f);
        const uint stepStart = (flags == 2u) ? tid : gtid;
        const uint stepStride = (flags == 2u) ? (max(g_NwbPhysicsPushConstants.itemOffset, 1u) * NWB_PHYSICS_WORKGROUP_SIZE) : NWB_PHYSICS_WORKGROUP_SIZE;

        float3 sumMassRelPos = float3(0.0f, 0.0f, 0.0f);
        float sumMass = 0.0f;
        float3 sumR = float3(0.0f, 0.0f, 0.0f);
        float3 col0 = float3(0.0f, 0.0f, 0.0f);
        float3 col1 = float3(0.0f, 0.0f, 0.0f);
        float3 col2 = float3(0.0f, 0.0f, 0.0f);
        for(uint i = stepStart; i < itemCount; i += stepStride){
            const float3 posI = g_NwbPhysicsFloat4Buffers[pSlot][i].xyz;
            const float4 rr = g_NwbPhysicsFloat4Buffers[restRelSlot][i];
            const float massW = max(rr.w, 0.0f);
            const float3 wu = (posI - pRef) * massW;
            sumMassRelPos += wu;
            sumMass += massW;
            sumR += rr.xyz * massW;
            col0 += wu * rr.x;
            col1 += wu * rr.y;
            col2 += wu * rr.z;
        }
        nwbPhysicsSubsysReduceShapeMoments(gtid, sumMassRelPos, sumMass, sumR, col0, col1, col2);
        if(gtid == 0u){
            if(flags == 2u){
                const uint baseIdx = 2u + groupID.x * 5u;
                g_NwbPhysicsFloat4Buffers[shapeOutSlot][baseIdx + 0u] = float4(sumMassRelPos, sumMass);
                g_NwbPhysicsFloat4Buffers[shapeOutSlot][baseIdx + 1u] = float4(sumR, 0.0f);
                g_NwbPhysicsFloat4Buffers[shapeOutSlot][baseIdx + 2u] = float4(col0, 0.0f);
                g_NwbPhysicsFloat4Buffers[shapeOutSlot][baseIdx + 3u] = float4(col1, 0.0f);
                g_NwbPhysicsFloat4Buffers[shapeOutSlot][baseIdx + 4u] = float4(col2, 0.0f);
            }
            else if(sumMass <= 1.0e-12f){
                g_NwbPhysicsFloat4Buffers[shapeOutSlot][0] = float4(0.0f, 0.0f, 0.0f, 0.0f);
                g_NwbPhysicsFloat4Buffers[shapeOutSlot][1] = float4(0.0f, 0.0f, 0.0f, 1.0f);
            }
            else{
                const float3 relCom = sumMassRelPos / sumMass;
                nwbPhysicsSubsysSolveShapeMatchQuat(
                    shapeOutSlot,
                    pRef + relCom,
                    col0 - relCom * sumR.x,
                    col1 - relCom * sumR.y,
                    col2 - relCom * sumR.z
                );
            }
        }
        return;
    }

    if(mode == NWB_PHYSICS_SUBSYS_MODE_SOFT_REDUCE_BOUNDS){
        if(flags == 0u){
            const uint mIdx = groupID.x;
            if(mIdx >= itemCount)
                return;
            const uint meshPosSlot = g_NwbPhysicsPushConstants.slots0.x;
            const uint meshletMetaSlot = g_NwbPhysicsPushConstants.slots0.y;
            const uint meshletVertListSlot = g_NwbPhysicsPushConstants.slots0.z;
            const uint meshletTriListSlot = g_NwbPhysicsPushConstants.slots0.w;
            const uint triIdxSlot = g_NwbPhysicsPushConstants.slots1.x;
            const uint meshletBoundsSlot = g_NwbPhysicsPushConstants.slots1.y;
            const uint meshletLocalBoundsSlot = g_NwbPhysicsPushConstants.slots1.z;

            const uint4 meta = asuint(g_NwbPhysicsFloat4Buffers[meshletMetaSlot][mIdx]);
            const uint posStart = meta.x;
            const uint posCount = meta.y;
            const uint triStart = meta.z;
            const uint triCount = meta.w;
            if(posCount == 0u || triCount == 0u){
                if(gtid == 0u){
                    g_NwbHeapStorageBuffers[meshletBoundsSlot].Store4(mIdx * 20u + 0u, uint4(0u, 0u, 0u, 0u));
                    g_NwbHeapStorageBuffers[meshletBoundsSlot].Store(mIdx * 20u + 16u, 0u);
                    g_NwbHeapStorageBuffers[meshletLocalBoundsSlot].Store4(mIdx * 32u + 0u, uint4(0u, 0u, 0u, 0u));
                    g_NwbHeapStorageBuffers[meshletLocalBoundsSlot].Store4(mIdx * 32u + 16u, uint4(0u, 0u, 0u, 0u));
                }
                return;
            }

            float3 localMin = float3(1.0e30f, 1.0e30f, 1.0e30f);
            float3 localMax = float3(-1.0e30f, -1.0e30f, -1.0e30f);
            uint validPos = 0u;
            float3 cachedPos = float3(0.0f, 0.0f, 0.0f);
            bool cachedPosValid = false;
            if(posCount <= NWB_PHYSICS_WORKGROUP_SIZE){
                if(gtid < posCount){
                    const uint vIdx = g_NwbPhysicsUintBuffers[meshletVertListSlot][posStart + gtid];
                    cachedPos = nwbMeshLoadPosition(g_NwbPhysicsMeshPosBuffers[meshPosSlot][vIdx]);
                    cachedPosValid = nwbFiniteFloat3(cachedPos);
                    if(cachedPosValid){
                        localMin = cachedPos;
                        localMax = cachedPos;
                        validPos = 1u;
                    }
                }
            }
            else{
                for(uint i = gtid; i < posCount; i += NWB_PHYSICS_WORKGROUP_SIZE){
                    const uint vIdx = g_NwbPhysicsUintBuffers[meshletVertListSlot][posStart + i];
                    const float3 pos = nwbMeshLoadPosition(g_NwbPhysicsMeshPosBuffers[meshPosSlot][vIdx]);
                    if(!nwbFiniteFloat3(pos))
                        continue;
                    localMin = min(localMin, pos);
                    localMax = max(localMax, pos);
                    ++validPos;
                }
            }
            float3 redMin = float3(0.0f, 0.0f, 0.0f);
            float3 redMax = float3(0.0f, 0.0f, 0.0f);
            uint redValidPos = 0u;
            nwbPhysicsSubsysReduceMinMaxAndUintSum(gtid, localMin, localMax, validPos, redMin, redMax, redValidPos);

            const bool boundsValid = (redValidPos == posCount) && all(redMin <= redMax);
            const float3 bCenter = (redMin + redMax) * 0.5f;
            if(gtid == 0u){
                const uint localByteOff = mIdx * 32u;
                g_NwbHeapStorageBuffers[meshletLocalBoundsSlot].Store4(localByteOff + 0u, uint4(asuint(redMin), boundsValid ? 3u : 0u));
                g_NwbHeapStorageBuffers[meshletLocalBoundsSlot].Store4(localByteOff + 16u, uint4(asuint(redMax), 0u));
            }
            if(redValidPos == 0u){
                if(gtid == 0u){
                    g_NwbHeapStorageBuffers[meshletBoundsSlot].Store4(mIdx * 20u + 0u, uint4(0u, 0u, 0u, 0u));
                    g_NwbHeapStorageBuffers[meshletBoundsSlot].Store(mIdx * 20u + 16u, 0u);
                }
                return;
            }

            float maxRSq = 0.0f;
            if(posCount <= NWB_PHYSICS_WORKGROUP_SIZE){
                if(cachedPosValid){
                    const float3 delta = cachedPos - bCenter;
                    maxRSq = dot(delta, delta);
                }
            }
            else{
                for(uint i = gtid; i < posCount; i += NWB_PHYSICS_WORKGROUP_SIZE){
                    const uint vIdx = g_NwbPhysicsUintBuffers[meshletVertListSlot][posStart + i];
                    const float3 pos = nwbMeshLoadPosition(g_NwbPhysicsMeshPosBuffers[meshPosSlot][vIdx]);
                    if(!nwbFiniteFloat3(pos))
                        continue;
                    const float3 delta = pos - bCenter;
                    maxRSq = max(maxRSq, dot(delta, delta));
                }
            }
            const float radiusOut = sqrt(nwbPhysicsSubsysReduceFloatMax(gtid, maxRSq)) * 1.25f;

            float3 areaNormalSum = float3(0.0f, 0.0f, 0.0f);
            uint validFaces = 0u;
            float3 cachedFaceUnit = float3(0.0f, 0.0f, 0.0f);
            bool cachedFaceValid = false;
            if(triCount <= NWB_PHYSICS_WORKGROUP_SIZE){
                if(gtid < triCount){
                    const uint triIdx = g_NwbPhysicsUintBuffers[meshletTriListSlot][triStart + gtid];
                    const uint i0 = g_NwbPhysicsUintBuffers[triIdxSlot][triIdx * 3u + 0u];
                    const uint i1 = g_NwbPhysicsUintBuffers[triIdxSlot][triIdx * 3u + 1u];
                    const uint i2 = g_NwbPhysicsUintBuffers[triIdxSlot][triIdx * 3u + 2u];
                    const float3 p0 = nwbMeshLoadPosition(g_NwbPhysicsMeshPosBuffers[meshPosSlot][i0]);
                    const float3 p1 = nwbMeshLoadPosition(g_NwbPhysicsMeshPosBuffers[meshPosSlot][i1]);
                    const float3 p2 = nwbMeshLoadPosition(g_NwbPhysicsMeshPosBuffers[meshPosSlot][i2]);
                    const float3 fn = cross(p1 - p0, p2 - p0);
                    const float fnLenSq = dot(fn, fn);
                    if(nwbFiniteFloat3(fn) && fnLenSq > 1.0e-12f){
                        areaNormalSum = fn;
                        validFaces = 1u;
                        cachedFaceUnit = fn * rsqrt(fnLenSq);
                        cachedFaceValid = true;
                    }
                }
            }
            else{
                for(uint ti = gtid; ti < triCount; ti += NWB_PHYSICS_WORKGROUP_SIZE){
                    const uint triIdx = g_NwbPhysicsUintBuffers[meshletTriListSlot][triStart + ti];
                    const uint i0 = g_NwbPhysicsUintBuffers[triIdxSlot][triIdx * 3u + 0u];
                    const uint i1 = g_NwbPhysicsUintBuffers[triIdxSlot][triIdx * 3u + 1u];
                    const uint i2 = g_NwbPhysicsUintBuffers[triIdxSlot][triIdx * 3u + 2u];
                    const float3 p0 = nwbMeshLoadPosition(g_NwbPhysicsMeshPosBuffers[meshPosSlot][i0]);
                    const float3 p1 = nwbMeshLoadPosition(g_NwbPhysicsMeshPosBuffers[meshPosSlot][i1]);
                    const float3 p2 = nwbMeshLoadPosition(g_NwbPhysicsMeshPosBuffers[meshPosSlot][i2]);
                    const float3 fn = cross(p1 - p0, p2 - p0);
                    if(nwbFiniteFloat3(fn) && dot(fn, fn) > 1.0e-12f){
                        areaNormalSum += fn;
                        ++validFaces;
                    }
                }
            }
            float3 redAreaNormalSum = float3(0.0f, 0.0f, 0.0f);
            uint redValidFaces = 0u;
            nwbPhysicsSubsysReduceSum3AndUint(gtid, areaNormalSum, validFaces, redAreaNormalSum, redValidFaces);
            const float3 coneAxis = nwbSafeNormalize(redAreaNormalSum, float3(0.0f, 0.0f, 0.0f));

            float minCutoff = 1.0f;
            if(triCount <= NWB_PHYSICS_WORKGROUP_SIZE){
                if(cachedFaceValid)
                    minCutoff = dot(coneAxis, cachedFaceUnit);
            }
            else{
                for(uint ti = gtid; ti < triCount; ti += NWB_PHYSICS_WORKGROUP_SIZE){
                    const uint triIdx = g_NwbPhysicsUintBuffers[meshletTriListSlot][triStart + ti];
                    const uint i0 = g_NwbPhysicsUintBuffers[triIdxSlot][triIdx * 3u + 0u];
                    const uint i1 = g_NwbPhysicsUintBuffers[triIdxSlot][triIdx * 3u + 1u];
                    const uint i2 = g_NwbPhysicsUintBuffers[triIdxSlot][triIdx * 3u + 2u];
                    const float3 p0 = nwbMeshLoadPosition(g_NwbPhysicsMeshPosBuffers[meshPosSlot][i0]);
                    const float3 p1 = nwbMeshLoadPosition(g_NwbPhysicsMeshPosBuffers[meshPosSlot][i1]);
                    const float3 p2 = nwbMeshLoadPosition(g_NwbPhysicsMeshPosBuffers[meshPosSlot][i2]);
                    const float3 fn = nwbSafeNormalize(cross(p1 - p0, p2 - p0), float3(0.0f, 0.0f, 0.0f));
                    if(nwbValidFrameDirection(fn))
                        minCutoff = min(minCutoff, dot(coneAxis, fn));
                }
            }
            const float finalCutoff = nwbPhysicsSubsysReduceFloatMin(gtid, minCutoff);

            if(gtid == 0u){
                const bool coneValid = (redValidFaces == triCount) && nwbValidFrameDirection(coneAxis) && (finalCutoff > 0.0f);
                const uint conePacked = coneValid ? nwbMeshPackCone(coneAxis, finalCutoff) : 0u;
                g_NwbHeapStorageBuffers[meshletBoundsSlot].Store4(mIdx * 20u + 0u, asuint(float4(bCenter, radiusOut)));
                g_NwbHeapStorageBuffers[meshletBoundsSlot].Store(mIdx * 20u + 16u, conePacked);
            }
            return;
        }

        if(groupID.x != 0u)
            return;
        const uint meshletLocalBoundsSlot = g_NwbPhysicsPushConstants.slots0.x;
        const uint localBoundsSlot = g_NwbPhysicsPushConstants.slots0.y;
        float3 totalMin = float3(1.0e30f, 1.0e30f, 1.0e30f);
        float3 totalMax = float3(-1.0e30f, -1.0e30f, -1.0e30f);
        uint validCnt = 0u;
        for(uint m = gtid; m < itemCount; m += NWB_PHYSICS_WORKGROUP_SIZE){
            const uint4 lo = g_NwbHeapStorageBuffers[meshletLocalBoundsSlot].Load4(m * 32u + 0u);
            const uint4 hi = g_NwbHeapStorageBuffers[meshletLocalBoundsSlot].Load4(m * 32u + 16u);
            if((lo.w & NWB_RUNTIME_MESH_BOUNDS_FLAG_VALID) == 0u)
                continue;
            const float3 mMin = asfloat(lo.xyz);
            const float3 mMax = asfloat(hi.xyz);
            if(!nwbFiniteFloat3(mMin) || !nwbFiniteFloat3(mMax))
                continue;
            totalMin = min(totalMin, mMin);
            totalMax = max(totalMax, mMax);
            ++validCnt;
        }
        float3 redMin = float3(0.0f, 0.0f, 0.0f);
        float3 redMax = float3(0.0f, 0.0f, 0.0f);
        uint redValidCnt = 0u;
        nwbPhysicsSubsysReduceMinMaxAndUintSum(gtid, totalMin, totalMax, validCnt, redMin, redMax, redValidCnt);
        if(gtid == 0u){
            const bool outValid = (redValidCnt > 0u) && all(redMin <= redMax);
            const float3 outMin = outValid ? redMin : float3(0.0f, 0.0f, 0.0f);
            const float3 outMax = outValid ? redMax : float3(0.0f, 0.0f, 0.0f);
            g_NwbHeapStorageBuffers[localBoundsSlot].Store4(0u, uint4(asuint(outMin), outValid ? 3u : 0u));
            g_NwbHeapStorageBuffers[localBoundsSlot].Store4(16u, uint4(asuint(outMax), 0u));
        }
        return;
    }

    if(tid >= itemCount)
        return;

    const uint idx = g_NwbPhysicsPushConstants.itemOffset + tid;

    if(mode == NWB_PHYSICS_SUBSYS_MODE_SOFT_PREDICT){
        const uint pSlot = g_NwbPhysicsPushConstants.slots0.x;
        const uint fieldSlot = g_NwbPhysicsPushConstants.slots0.y;
        const uint fluidSlot = g_NwbPhysicsPushConstants.slots0.z;
        const uint stateSlot = g_NwbPhysicsPushConstants.slots0.w;
        const uint gerstnerSlot = g_NwbPhysicsPushConstants.slots1.x;
        const uint fieldCount = g_NwbPhysicsPushConstants.itemOffset;
        const uint fluidCount = flags;
        const float h = g_NwbPhysicsPushConstants.deltaTime;
        const float dampScale = g_NwbPhysicsPushConstants.invDeltaTime;
        const float dragCoeff = g_NwbPhysicsPushConstants.maxLinearVelocity;
        const float timeSec = g_NwbPhysicsPushConstants.linearSlop;
        const float3 grav = g_NwbPhysicsPushConstants.gravitySpeculative.xyz;
        const float invH = (h > 1.0e-5f) ? (1.0f / h) : 1.0e5f;

        float4 posInvMass = g_NwbPhysicsFloat4Buffers[pSlot][tid];
        NwbPhysicsGpuSoftParticleState st = g_NwbPhysicsSoftParticleStateBuffers[stateSlot][tid];
        st.prevPosFlags.xyz = posInvMass.xyz;
        const uint pFlags = asuint(st.prevPosFlags.w);
        const float invMass = posInvMass.w;
        if((pFlags & 1u) != 0u || invMass <= 0.0f){
            st.velPad.xyz = float3(0.0f, 0.0f, 0.0f);
            g_NwbPhysicsSoftParticleStateBuffers[stateSlot][tid] = st;
            return;
        }

        const float mass = 1.0f / invMass;
        float3 totalF = st.extForcePad.xyz;
        const float dragK = clamp(dragCoeff * 4.0f, 0.05f, 4.0f) * mass;
        float3 dummyTrq = float3(0.0f, 0.0f, 0.0f);
        if(fieldSlot != NWB_PHYSICS_INVALID_INDEX && fieldCount > 0u){
            [loop] for(uint fi = 0u; fi < fieldCount; ++fi)
                totalF += nwbPhysicsSubsysEvalForceField(g_NwbPhysicsForceFieldBuffers[fieldSlot][fi], posInvMass.xyz, st.velPad.xyz, mass, dragK, false, timeSec, invH, grav, 1.0f, dummyTrq);
        }
        if(fluidSlot != NWB_PHYSICS_INVALID_INDEX && fluidCount > 0u){
            const float pRad = max(g_NwbPhysicsPushConstants.gravitySpeculative.w, 1.0e-2f);
            const float pVol = (4.0f / 3.0f) * 3.1415926535897932f * pRad * pRad * pRad;
            [loop] for(uint fli = 0u; fli < fluidCount; ++fli){
                const NwbPhysicsGpuFluidVolume fl = g_NwbPhysicsFluidVolumeBuffers[fluidSlot][fli];
                if(asuint(fl.waveSpeedAngAddedFlags.w) == 0u)
                    continue;
                const float3 surfN = nwbPhysicsSafeNormal(fl.surfaceNormalOffset.xyz, float3(0.0f, 1.0f, 0.0f));
                float d = 0.0f;
                float3 flowVel = float3(0.0f, 0.0f, 0.0f);
                float3 localSurfN = surfN;
                nwbPhysicsSubsysFluidEvalSurface(fl, gerstnerSlot, posInvMass.xyz, surfN, timeSec, d, flowVel, localSurfN);
                if(d + pRad > 0.0f){
                    const float hCap = clamp(d + pRad, 0.0f, 2.0f * pRad);
                    const float vCap = (3.1415926535897932f / 3.0f) * hCap * hCap * (3.0f * pRad - hCap);
                    const float frac = clamp(vCap / max(pVol, 1.0e-7f), 0.0f, 1.0f);
                    const float dispM = fl.boundsMaxDensity.w * vCap;
                    totalF += (-grav) * dispM;
                    const float3 relV = flowVel - st.velPad.xyz;
                    const float sp = length(relV);
                    if(sp > 1.0e-5f){
                        const float linD = fl.flowVelLinDrag.w * sp * frac * mass;
                        const float maxF = (mass * sp) * invH * 0.9f;
                        totalF += (relV / sp) * min(linD, maxF);
                    }
                }
            }
        }

        const float3 acc = grav + totalF * invMass;
        const float3 newVel = (st.velPad.xyz + acc * h) * dampScale;
        st.velPad.xyz = newVel;
        posInvMass.xyz += newVel * h;
        st.extForcePad.xyz = float3(0.0f, 0.0f, 0.0f);
        g_NwbPhysicsFloat4Buffers[pSlot][tid] = posInvMass;
        g_NwbPhysicsSoftParticleStateBuffers[stateSlot][tid] = st;
        return;
    }

    if(mode == NWB_PHYSICS_SUBSYS_MODE_SOFT_RESET_LAMBDAS){
        const uint conSlot = g_NwbPhysicsPushConstants.slots0.x;
        if(flags == 0u)
            g_NwbPhysicsSoftDistConstraintBuffers[conSlot][idx].restCompLambdaPad.z = 0.0f;
        else if(flags == 1u)
            g_NwbPhysicsSoftVolConstraintBuffers[conSlot][idx].restCompLambdaPad.z = 0.0f;
        else
            g_NwbPhysicsSoftBendConstraintBuffers[conSlot][idx].restAngleCompLambdaPad.z = 0.0f;
        return;
    }

    if(mode == NWB_PHYSICS_SUBSYS_MODE_SOFT_DISTANCE_COLOR){
        const uint pSlot = g_NwbPhysicsPushConstants.slots0.x;
        const uint dcSlot = g_NwbPhysicsPushConstants.slots0.y;
        const float invDt2 = g_NwbPhysicsPushConstants.deltaTime;
        NwbPhysicsGpuSoftDistConstraint dc = g_NwbPhysicsSoftDistConstraintBuffers[dcSlot][idx];
        const uint i0 = dc.indices.x;
        const uint i1 = dc.indices.y;
        if(i0 == i1)
            return;
        const float4 p0 = g_NwbPhysicsFloat4Buffers[pSlot][i0];
        const float4 p1 = g_NwbPhysicsFloat4Buffers[pSlot][i1];
        const float w0 = max(p0.w, 0.0f);
        const float w1 = max(p1.w, 0.0f);
        const float wSum = w0 + w1;
        if(wSum <= 1.0e-12f)
            return;
        const float3 diff = p0.xyz - p1.xyz;
        const float dSq = dot(diff, diff);
        if(dSq <= 1.0e-16f)
            return;
        const float invD = rsqrt(dSq);
        const float d = dSq * invD;
        const float3 n = diff * invD;
        const float cVal = d - dc.restCompLambdaPad.x;
        const float alphaTilde = dc.restCompLambdaPad.y * invDt2;
        const float dLambda = (-cVal - alphaTilde * dc.restCompLambdaPad.z) / (wSum + alphaTilde);
        dc.restCompLambdaPad.z += dLambda;
        g_NwbPhysicsSoftDistConstraintBuffers[dcSlot][idx].restCompLambdaPad.z = dc.restCompLambdaPad.z;
        if(w0 > 0.0f)
            g_NwbPhysicsFloat4Buffers[pSlot][i0].xyz = p0.xyz + n * (dLambda * w0);
        if(w1 > 0.0f)
            g_NwbPhysicsFloat4Buffers[pSlot][i1].xyz = p1.xyz - n * (dLambda * w1);
        return;
    }

    if(mode == NWB_PHYSICS_SUBSYS_MODE_SOFT_VOLUME_COLOR){
        const uint pSlot = g_NwbPhysicsPushConstants.slots0.x;
        const uint vcSlot = g_NwbPhysicsPushConstants.slots0.y;
        const float invDt2 = g_NwbPhysicsPushConstants.deltaTime;
        NwbPhysicsGpuSoftVolConstraint vc = g_NwbPhysicsSoftVolConstraintBuffers[vcSlot][idx];
        const float4 p0 = g_NwbPhysicsFloat4Buffers[pSlot][vc.indices.x];
        const float4 p1 = g_NwbPhysicsFloat4Buffers[pSlot][vc.indices.y];
        const float4 p2 = g_NwbPhysicsFloat4Buffers[pSlot][vc.indices.z];
        const float4 p3 = g_NwbPhysicsFloat4Buffers[pSlot][vc.indices.w];
        const float w0 = max(p0.w, 0.0f);
        const float w1 = max(p1.w, 0.0f);
        const float w2 = max(p2.w, 0.0f);
        const float w3 = max(p3.w, 0.0f);
        const float3 x0 = p0.xyz;
        const float3 x1 = p1.xyz;
        const float3 x2 = p2.xyz;
        const float3 x3 = p3.xyz;
        const float3 g1 = cross(x2 - x0, x3 - x0) * (1.0f / 6.0f);
        const float3 g2 = cross(x3 - x0, x1 - x0) * (1.0f / 6.0f);
        const float3 g3 = cross(x1 - x0, x2 - x0) * (1.0f / 6.0f);
        const float3 g0 = -(g1 + g2 + g3);
        const float denomGrad = w0 * dot(g0, g0) + w1 * dot(g1, g1) + w2 * dot(g2, g2) + w3 * dot(g3, g3);
        if(denomGrad <= 1.0e-12f)
            return;
        const float vol = dot(x1 - x0, g1);
        const float cVal = vol - vc.restCompLambdaPad.x;
        const float alphaTilde = vc.restCompLambdaPad.y * invDt2;
        const float dLambda = (-cVal - alphaTilde * vc.restCompLambdaPad.z) / (denomGrad + alphaTilde);
        vc.restCompLambdaPad.z += dLambda;
        g_NwbPhysicsSoftVolConstraintBuffers[vcSlot][idx].restCompLambdaPad.z = vc.restCompLambdaPad.z;
        if(w0 > 0.0f) g_NwbPhysicsFloat4Buffers[pSlot][vc.indices.x].xyz = x0 + g0 * (dLambda * w0);
        if(w1 > 0.0f) g_NwbPhysicsFloat4Buffers[pSlot][vc.indices.y].xyz = x1 + g1 * (dLambda * w1);
        if(w2 > 0.0f) g_NwbPhysicsFloat4Buffers[pSlot][vc.indices.z].xyz = x2 + g2 * (dLambda * w2);
        if(w3 > 0.0f) g_NwbPhysicsFloat4Buffers[pSlot][vc.indices.w].xyz = x3 + g3 * (dLambda * w3);
        return;
    }

    if(mode == NWB_PHYSICS_SUBSYS_MODE_SOFT_BENDING_COLOR){
        const uint pSlot = g_NwbPhysicsPushConstants.slots0.x;
        const uint bcSlot = g_NwbPhysicsPushConstants.slots0.y;
        const float invDt2 = g_NwbPhysicsPushConstants.deltaTime;
        NwbPhysicsGpuSoftBendConstraint bc = g_NwbPhysicsSoftBendConstraintBuffers[bcSlot][idx];
        const float4 p0 = g_NwbPhysicsFloat4Buffers[pSlot][bc.indices.x];
        const float4 p1 = g_NwbPhysicsFloat4Buffers[pSlot][bc.indices.y];
        const float4 p2 = g_NwbPhysicsFloat4Buffers[pSlot][bc.indices.z];
        const float4 p3 = g_NwbPhysicsFloat4Buffers[pSlot][bc.indices.w];
        const float w0 = max(p0.w, 0.0f);
        const float w1 = max(p1.w, 0.0f);
        const float w2 = max(p2.w, 0.0f);
        const float w3 = max(p3.w, 0.0f);
        if(w0 + w1 + w2 + w3 <= 1.0e-12f)
            return;

        const float3 x0 = p0.xyz;
        const float3 x1 = p1.xyz;
        const float3 x2 = p2.xyz;
        const float3 x3 = p3.xyz;
        const float3 e = x1 - x0;
        const float eLenSq = dot(e, e);
        if(eLenSq <= 1.0e-12f)
            return;
        const float eInv = rsqrt(eLenSq);
        const float eLen = eLenSq * eInv;
        const float3 eHat = e * eInv;

        const float3 n1Raw = cross(e, x2 - x0);
        const float3 n2Raw = cross(x3 - x0, e);
        const float a1Sq = dot(n1Raw, n1Raw);
        const float a2Sq = dot(n2Raw, n2Raw);
        if(a1Sq <= 1.0e-12f || a2Sq <= 1.0e-12f)
            return;
        const float a1Inv = rsqrt(a1Sq);
        const float a2Inv = rsqrt(a2Sq);
        const float a1 = a1Sq * a1Inv;
        const float a2 = a2Sq * a2Inv;
        const float3 n1Hat = n1Raw * a1Inv;
        const float3 n2Hat = n2Raw * a2Inv;

        const float sinVal = dot(cross(n1Hat, n2Hat), eHat);
        const float cosVal = clamp(dot(n1Hat, n2Hat), -1.0f, 1.0f);
        const float phi = atan2(sinVal, cosVal);
        float cVal = phi - bc.restAngleCompLambdaPad.x;
        if(cVal > 3.14159265359f)
            cVal -= 6.28318530718f;
        if(cVal < -3.14159265359f)
            cVal += 6.28318530718f;

        const float3 q2 = (-n1Hat) * (eLen / a1);
        const float3 q3 = (-n2Hat) * (eLen / a2);
        const float invESq = 1.0f / eLenSq;
        const float u = dot(x2 - x0, e) * invESq;
        const float v = dot(x3 - x0, e) * invESq;
        const float3 q0 = q2 * (u - 1.0f) + q3 * (v - 1.0f);
        const float3 q1 = -(q2 * u + q3 * v);

        const float denom = w0 * dot(q0, q0) + w1 * dot(q1, q1) + w2 * dot(q2, q2) + w3 * dot(q3, q3);
        const float alphaTilde = bc.restAngleCompLambdaPad.y * invDt2;
        if(denom + alphaTilde <= 1.0e-12f)
            return;
        const float dLambda = (-cVal - alphaTilde * bc.restAngleCompLambdaPad.z) / (denom + alphaTilde);
        bc.restAngleCompLambdaPad.z += dLambda;
        g_NwbPhysicsSoftBendConstraintBuffers[bcSlot][idx].restAngleCompLambdaPad.z = bc.restAngleCompLambdaPad.z;
        if(w0 > 0.0f) g_NwbPhysicsFloat4Buffers[pSlot][bc.indices.x].xyz = x0 + q0 * (dLambda * w0);
        if(w1 > 0.0f) g_NwbPhysicsFloat4Buffers[pSlot][bc.indices.y].xyz = x1 + q1 * (dLambda * w1);
        if(w2 > 0.0f) g_NwbPhysicsFloat4Buffers[pSlot][bc.indices.z].xyz = x2 + q2 * (dLambda * w2);
        if(w3 > 0.0f) g_NwbPhysicsFloat4Buffers[pSlot][bc.indices.w].xyz = x3 + q3 * (dLambda * w3);
        return;
    }

    if(mode == NWB_PHYSICS_SUBSYS_MODE_SOFT_BACKSTOP_COLLIDE){
        const uint pSlot = g_NwbPhysicsPushConstants.slots0.x;
        const uint backstopSlot = g_NwbPhysicsPushConstants.slots0.y;
        const uint rangeSlot = g_NwbPhysicsPushConstants.slots0.z;
        const float4 posInvMass = g_NwbPhysicsFloat4Buffers[pSlot][tid];
        if(posInvMass.w <= 0.0f)
            return;
        const uint start = g_NwbPhysicsUintBuffers[rangeSlot][tid * 2u + 0u];
        const uint cnt = g_NwbPhysicsUintBuffers[rangeSlot][tid * 2u + 1u];
        if(cnt == 0u)
            return;
        float3 pCur = posInvMass.xyz;
        [loop] for(uint bi = 0u; bi < cnt; ++bi){
            const NwbPhysicsGpuSoftBackstop bs = g_NwbPhysicsSoftBackstopBuffers[backstopSlot][start + bi];
            const float3 center = bs.centerMaxDist.xyz;
            const float maxDist = bs.centerMaxDist.w;
            if(maxDist > 0.0f){
                const float3 diff = pCur - center;
                const float dSq = dot(diff, diff);
                if(dSq > maxDist * maxDist && dSq > 1.0e-12f)
                    pCur = center + diff * (maxDist * rsqrt(dSq));
            }
            const float rBs = bs.normalRadiusIdx.y;
            if(rBs > 0.0f){
                const uint nXY = asuint(bs.normalRadiusIdx.z);
                const float nx = (float(nXY & 0xffffu) - 32767.0f) * (1.0f / 32767.0f);
                const float ny = (float((nXY >> 16u) & 0xffffu) - 32767.0f) * (1.0f / 32767.0f);
                const float3 rawN = float3(nx, ny, bs.normalRadiusIdx.w);
                const float nSq = dot(rawN, rawN);
                if(nSq > 1.0e-10f){
                    const float3 n = rawN * rsqrt(nSq);
                    const float3 bsCenter = center - n * rBs;
                    const float3 toP = pCur - bsCenter;
                    const float bsSq = dot(toP, toP);
                    if(bsSq < rBs * rBs){
                        const float3 pushDir = (bsSq > 1.0e-12f) ? (toP * rsqrt(bsSq)) : n;
                        pCur = bsCenter + pushDir * rBs;
                    }
                }
            }
        }
        g_NwbPhysicsFloat4Buffers[pSlot][tid].xyz = pCur;
        return;
    }

    if(mode == NWB_PHYSICS_SUBSYS_MODE_SOFT_AERO_DRAG){
        const uint pSlot = g_NwbPhysicsPushConstants.slots0.x;
        const uint triRangeSlot = g_NwbPhysicsPushConstants.slots0.y;
        const uint triIdxListSlot = g_NwbPhysicsPushConstants.slots0.z;
        const uint surfTriSlot = g_NwbPhysicsPushConstants.slots0.w;
        const uint stateSlot = g_NwbPhysicsPushConstants.slots1.x;
        const float h = g_NwbPhysicsPushConstants.deltaTime;
        const float dragC = max(g_NwbPhysicsPushConstants.invDeltaTime, 0.0f);
        const float liftC = g_NwbPhysicsPushConstants.maxLinearVelocity;
        const float3 windVel = g_NwbPhysicsPushConstants.gravitySpeculative.xyz;

        const float4 posInvMass = g_NwbPhysicsFloat4Buffers[pSlot][tid];
        if(posInvMass.w <= 0.0f)
            return;
        const uint triStart = g_NwbPhysicsUintBuffers[triRangeSlot][tid * 2u + 0u];
        const uint triCnt = g_NwbPhysicsUintBuffers[triRangeSlot][tid * 2u + 1u];
        if(triCnt == 0u || (dragC <= 0.0f && abs(liftC) <= 1.0e-6f))
            return;

        float3 totalDisp = float3(0.0f, 0.0f, 0.0f);
        const bool hasDrag = dragC > 0.0f;
        const bool hasLift = abs(liftC) > 1.0e-6f;
        [loop] for(uint ti = 0u; ti < triCnt; ++ti){
            const uint triId = g_NwbPhysicsUintBuffers[triIdxListSlot][triStart + ti];
            const uint4 tri = asuint(g_NwbPhysicsFloat4Buffers[surfTriSlot][triId]);
            const float3 x0 = g_NwbPhysicsFloat4Buffers[pSlot][tri.x].xyz;
            const float3 x1 = g_NwbPhysicsFloat4Buffers[pSlot][tri.y].xyz;
            const float3 x2 = g_NwbPhysicsFloat4Buffers[pSlot][tri.z].xyz;
            const float3 c = cross(x1 - x0, x2 - x0);
            const float areaSq = dot(c, c);
            if(areaSq <= 1.0e-12f)
                continue;
            const float invDoubleA = rsqrt(areaSq);
            const float doubleArea = areaSq * invDoubleA;
            const float3 triN = c * invDoubleA;
            const float3 v0 = g_NwbPhysicsSoftParticleStateBuffers[stateSlot][tri.x].velPad.xyz;
            const float3 v1 = g_NwbPhysicsSoftParticleStateBuffers[stateSlot][tri.y].velPad.xyz;
            const float3 v2 = g_NwbPhysicsSoftParticleStateBuffers[stateSlot][tri.z].velPad.xyz;
            const float3 avgVel = (v0 + v1 + v2) * (1.0f / 3.0f);
            const float3 vRel = windVel - avgVel;
            const float vn = dot(vRel, triN);
            const float baseCoeff = doubleArea * (dragC * h * h) * (1.0f / 6.0f);
            if(hasDrag)
                totalDisp += triN * (vn * clamp(baseCoeff * posInvMass.w, 0.0f, 0.9f));
            if(hasLift){
                const float vRelSq = dot(vRel, vRel);
                if(vRelSq > 1.0e-10f){
                    const float3 nEff = (vn > 0.0f) ? (-triN) : triN;
                    const float3 liftVec = cross(cross(vRel, nEff), vRel);
                    const float liftLenSq = dot(liftVec, liftVec);
                    if(liftLenSq > 1.0e-14f){
                        const float3 liftDir = liftVec * rsqrt(liftLenSq);
                        const float liftImpulse = (doubleArea * liftC * abs(vn) * sqrt(vRelSq) * h * h) / 6.0f;
                        totalDisp += liftDir * clamp(liftImpulse * posInvMass.w, -0.5f, 0.5f);
                    }
                }
            }
        }
        g_NwbPhysicsFloat4Buffers[pSlot][tid].xyz = posInvMass.xyz + totalDisp;
        return;
    }

    if(mode == NWB_PHYSICS_SUBSYS_MODE_SOFT_DEFORM_MESH){
        if(flags == 0u){
            const uint pSlot = g_NwbPhysicsPushConstants.slots0.x;
            const uint vertMapSlot = g_NwbPhysicsPushConstants.slots0.y;
            const uint xfConstSlot = g_NwbPhysicsPushConstants.slots0.z;
            const uint outMeshPosSlot = g_NwbPhysicsPushConstants.slots0.w;
            const uint cagePartSlot = g_NwbPhysicsPushConstants.slots1.x;
            const uint cageBarySlot = g_NwbPhysicsPushConstants.slots1.y;

            float3 worldP = float3(0.0f, 0.0f, 0.0f);
            if(cagePartSlot != NWB_PHYSICS_INVALID_INDEX && cageBarySlot != NWB_PHYSICS_INVALID_INDEX){
                const uint4 cp = asuint(g_NwbPhysicsFloat4Buffers[cagePartSlot][tid]);
                const float4 bw = g_NwbPhysicsFloat4Buffers[cageBarySlot][tid];
                worldP =
                    g_NwbPhysicsFloat4Buffers[pSlot][cp.x].xyz * bw.x
                    + g_NwbPhysicsFloat4Buffers[pSlot][cp.y].xyz * bw.y
                    + g_NwbPhysicsFloat4Buffers[pSlot][cp.z].xyz * bw.z
                    + g_NwbPhysicsFloat4Buffers[pSlot][cp.w].xyz * bw.w
                ;
            }
            else{
                const uint pIdx = g_NwbPhysicsUintBuffers[vertMapSlot][tid];
                worldP = g_NwbPhysicsFloat4Buffers[pSlot][pIdx].xyz;
            }

            const float4 xfPosWorld = g_NwbPhysicsFloat4Buffers[xfConstSlot][0];
            const float4 xfRot = g_NwbPhysicsFloat4Buffers[xfConstSlot][1];
            const float3 invScale = g_NwbPhysicsFloat4Buffers[xfConstSlot][2].xyz;
            float3 localP = worldP;
            if(xfPosWorld.w != 0.0f)
                localP = nwbPhysicsQuatInvRotate(worldP - xfPosWorld.xyz, xfRot) * invScale;
            if(!nwbFiniteFloat3(localP))
                localP = float3(0.0f, 0.0f, 0.0f);
            g_NwbPhysicsMeshPosBuffers[outMeshPosSlot][tid] = nwbMeshStorePosition(localP);
            return;
        }

        if(flags == 1u){
            const uint meshPosSlot = g_NwbPhysicsPushConstants.slots0.x;
            const uint vertTriRangeSlot = g_NwbPhysicsPushConstants.slots0.y;
            const uint vertTriListSlot = g_NwbPhysicsPushConstants.slots0.z;
            const uint triIdxSlot = g_NwbPhysicsPushConstants.slots0.w;
            const uint uvSlot = g_NwbPhysicsPushConstants.slots1.x;
            const uint outNormSlot = g_NwbPhysicsPushConstants.slots1.y;
            const uint outTanSlot = g_NwbPhysicsPushConstants.slots1.z;

            const uint tStart = g_NwbPhysicsUintBuffers[vertTriRangeSlot][tid * 2u + 0u];
            const uint tCnt = g_NwbPhysicsUintBuffers[vertTriRangeSlot][tid * 2u + 1u];
            float3 sumN = float3(0.0f, 0.0f, 0.0f);
            float3 sumT = float3(0.0f, 0.0f, 0.0f);
            float3 sumB = float3(0.0f, 0.0f, 0.0f);
            const bool hasUv = (uvSlot != NWB_PHYSICS_INVALID_INDEX);

            [loop] for(uint ti = 0u; ti < tCnt; ++ti){
                const uint triIdx = g_NwbPhysicsUintBuffers[vertTriListSlot][tStart + ti];
                const uint i0 = g_NwbPhysicsUintBuffers[triIdxSlot][triIdx * 3u + 0u];
                const uint i1 = g_NwbPhysicsUintBuffers[triIdxSlot][triIdx * 3u + 1u];
                const uint i2 = g_NwbPhysicsUintBuffers[triIdxSlot][triIdx * 3u + 2u];
                const float3 p0 = nwbMeshLoadPosition(g_NwbPhysicsMeshPosBuffers[meshPosSlot][i0]);
                const float3 p1 = nwbMeshLoadPosition(g_NwbPhysicsMeshPosBuffers[meshPosSlot][i1]);
                const float3 p2 = nwbMeshLoadPosition(g_NwbPhysicsMeshPosBuffers[meshPosSlot][i2]);
                const float3 e1 = p1 - p0;
                const float3 e2 = p2 - p0;
                const float3 fn = cross(e1, e2);
                sumN += fn;
                if(hasUv){
                    const float2 uv0v = g_NwbPhysicsFloat4Buffers[uvSlot][i0].xy;
                    const float2 uv1v = g_NwbPhysicsFloat4Buffers[uvSlot][i1].xy;
                    const float2 uv2v = g_NwbPhysicsFloat4Buffers[uvSlot][i2].xy;
                    const float du1 = uv1v.x - uv0v.x;
                    const float dv1 = uv1v.y - uv0v.y;
                    const float du2 = uv2v.x - uv0v.x;
                    const float dv2 = uv2v.y - uv0v.y;
                    const float det = du1 * dv2 - du2 * dv1;
                    const float signDet = (det < 0.0f) ? -1.0f : 1.0f;
                    const float3 tContrib = (e1 * dv2 - e2 * dv1) * signDet;
                    const float3 bContrib = (e2 * du1 - e1 * du2) * signDet;
                    if(dot(tContrib, tContrib) > 1.0e-18f){
                        sumT += tContrib;
                        sumB += bContrib;
                    }
                }
            }

            const float3 n = nwbSafeNormalize(sumN, float3(0.0f, 1.0f, 0.0f));
            const float3 projT = sumT - n * dot(n, sumT);
            float3 unitT;
            float handedness = 1.0f;
            if(dot(projT, projT) > 1.0e-12f){
                unitT = normalize(projT);
                handedness = (dot(cross(n, unitT), sumB) < 0.0f) ? -1.0f : 1.0f;
            }
            else{
                float3 fallbackB;
                nwbPhysicsComputeTangentBasis(n, unitT, fallbackB);
            }
            g_NwbPhysicsMeshHalf4Buffers[outNormSlot][tid] = nwbMeshStoreHalf4(float4(n, 0.0f));
            g_NwbPhysicsMeshHalf4Buffers[outTanSlot][tid] = nwbMeshStoreHalf4(float4(unitT, handedness));
            return;
        }

        const uint triIdxSlot = g_NwbPhysicsPushConstants.slots0.x;
        const uint normSlot = g_NwbPhysicsPushConstants.slots0.y;
        const uint attrSlot = g_NwbPhysicsPushConstants.slots0.z;
        const uint vIdx = g_NwbPhysicsUintBuffers[triIdxSlot][tid];
        const NwbMeshHalf4 nHalf = g_NwbPhysicsMeshHalf4Buffers[normSlot][vIdx];
        g_NwbHeapStorageBuffers[attrSlot].Store2(tid * 16u, uint2(nHalf.xy, nHalf.zw));
        return;
    }

    if(mode == NWB_PHYSICS_SUBSYS_MODE_SOFT_VEL_UPDATE){
        const uint pSlot = g_NwbPhysicsPushConstants.slots0.x;
        const uint stateSlot = g_NwbPhysicsPushConstants.slots0.y;
        const float invH = g_NwbPhysicsPushConstants.deltaTime;
        const float4 posInvMass = g_NwbPhysicsFloat4Buffers[pSlot][tid];
        const float4 prevPosFlags = g_NwbPhysicsSoftParticleStateBuffers[stateSlot][tid].prevPosFlags;
        if((asuint(prevPosFlags.w) & 1u) != 0u || posInvMass.w <= 0.0f)
            g_NwbPhysicsSoftParticleStateBuffers[stateSlot][tid].velPad.xyz = float3(0.0f, 0.0f, 0.0f);
        else
            g_NwbPhysicsSoftParticleStateBuffers[stateSlot][tid].velPad.xyz = (posInvMass.xyz - prevPosFlags.xyz) * invH;
        return;
    }

    if(mode == NWB_PHYSICS_SUBSYS_MODE_APPLY_FORCE_FIELDS){
        const uint bodySimSlot = g_NwbPhysicsPushConstants.slots0.x;
        const uint bodyStateSlot = g_NwbPhysicsPushConstants.slots0.y;
        const uint fieldSlot = g_NwbPhysicsPushConstants.slots0.z;
        const uint bodyFilterSlot = g_NwbPhysicsPushConstants.slots0.w;
        const uint fieldCount = g_NwbPhysicsPushConstants.itemOffset;
        const float timeSec = g_NwbPhysicsPushConstants.deltaTime;
        const float invDt = g_NwbPhysicsPushConstants.invDeltaTime;
        const float3 worldGrav = g_NwbPhysicsPushConstants.gravitySpeculative.xyz;

        NwbPhysicsGpuBodySim sim = g_NwbPhysicsBodySimBuffers[bodySimSlot][tid];
        NwbPhysicsGpuBodyState st = g_NwbPhysicsBodyStateBuffers[bodyStateSlot][tid];
        uint bFlags = asuint(st.deltaPositionFlags.w);
        if((bFlags & NWB_PHYSICS_BODY_FLAG_DYNAMIC) == 0u || sim.positionInvMass.w <= 0.0f)
            return;

        const float bodyMass = 1.0f / sim.positionInvMass.w;
        const float maxExt = max(sim.localCenterMinExtent.w, 0.05f);
        float3 addForce = float3(0.0f, 0.0f, 0.0f);
        float3 addTorque = float3(0.0f, 0.0f, 0.0f);
        [loop] for(uint fi = 0u; fi < fieldCount; ++fi){
            const NwbPhysicsGpuForceField fld = g_NwbPhysicsForceFieldBuffers[fieldSlot][fi];
            if(!nwbPhysicsSubsysBodyMatchesFilter(bodyFilterSlot, tid, fld.filterData))
                continue;
            float3 fTorque = float3(0.0f, 0.0f, 0.0f);
            const float3 fForce = nwbPhysicsSubsysEvalForceField(
                fld,
                sim.positionInvMass.xyz,
                st.linearVelocityPad.xyz,
                bodyMass,
                maxExt,
                true,
                timeSec,
                invDt,
                worldGrav,
                sim.invInertiaWorld1.z,
                fTorque
            );
            nwbPhysicsSubsysApplyForceFieldAngular(
                fld,
                st.angularVelocityPad.xyz,
                maxExt,
                sim.invInertiaWorld0,
                sim.invInertiaWorld1,
                invDt,
                fTorque
            );
            addForce += fForce;
            addTorque += fTorque;
        }
        if(any(addForce != float3(0.0f, 0.0f, 0.0f)) || any(addTorque != float3(0.0f, 0.0f, 0.0f))){
            g_NwbPhysicsBodySimBuffers[bodySimSlot][tid].forceLinearDamping.xyz = sim.forceLinearDamping.xyz + addForce;
            g_NwbPhysicsBodySimBuffers[bodySimSlot][tid].torqueAngularDamping.xyz = sim.torqueAngularDamping.xyz + addTorque;
            if((bFlags & NWB_PHYSICS_BODY_FLAG_SLEEPING) != 0u){
                bFlags &= ~NWB_PHYSICS_BODY_FLAG_SLEEPING;
                nwbPhysicsWriteBodyFlags(bodyStateSlot, tid, bFlags);
                g_NwbPhysicsBodySimBuffers[bodySimSlot][tid].invInertiaWorld1.w = 0.0f;
            }
        }
        return;
    }

    if(mode == NWB_PHYSICS_SUBSYS_MODE_APPLY_FLUID_BUOYANCY){
        const uint bodySimSlot = g_NwbPhysicsPushConstants.slots0.x;
        const uint bodyStateSlot = g_NwbPhysicsPushConstants.slots0.y;
        const uint fluidSlot = g_NwbPhysicsPushConstants.slots0.z;
        const uint bodyVolSlot = g_NwbPhysicsPushConstants.slots0.w;
        const uint gerstnerSlot = g_NwbPhysicsPushConstants.slots1.x;
        const uint bodyFilterSlot = g_NwbPhysicsPushConstants.slots1.y;
        const uint eventRingSlot = g_NwbPhysicsPushConstants.slots1.z;
        const uint prepCounterSlot = g_NwbPhysicsPushConstants.slots1.w;
        const uint fluidCount = g_NwbPhysicsPushConstants.itemOffset;
        const uint maxEvents = flags;
        const float timeSec = g_NwbPhysicsPushConstants.deltaTime;
        const float invDt = g_NwbPhysicsPushConstants.invDeltaTime;
        const float3 grav = g_NwbPhysicsPushConstants.gravitySpeculative.xyz;

        NwbPhysicsGpuBodySim sim = g_NwbPhysicsBodySimBuffers[bodySimSlot][tid];
        NwbPhysicsGpuBodyState st = g_NwbPhysicsBodyStateBuffers[bodyStateSlot][tid];
        uint bFlags = asuint(st.deltaPositionFlags.w);
        if((bFlags & NWB_PHYSICS_BODY_FLAG_DYNAMIC) == 0u || sim.positionInvMass.w <= 0.0f)
            return;

        const float bodyMass = 1.0f / sim.positionInvMass.w;
        const float effR = max(sim.localCenterMinExtent.w, 0.05f);
        const float4 volInfo = (bodyVolSlot != NWB_PHYSICS_INVALID_INDEX)
            ? g_NwbPhysicsFloat4Buffers[bodyVolSlot][tid]
            : float4((4.0f / 3.0f) * 3.1415926535897932f * effR * effR * effR, effR, effR, effR);
        const float bodyVol = max(volInfo.x, 1.0e-6f);
        const float testR = max(max(volInfo.y, volInfo.z), volInfo.w);

        float3 addForce = float3(0.0f, 0.0f, 0.0f);
        float3 addTorque = float3(0.0f, 0.0f, 0.0f);
        [loop] for(uint fli = 0u; fli < fluidCount; ++fli){
            const NwbPhysicsGpuFluidVolume fl = g_NwbPhysicsFluidVolumeBuffers[fluidSlot][fli];
            if(asuint(fl.waveSpeedAngAddedFlags.w) == 0u)
                continue;
            if(!nwbPhysicsSubsysBodyMatchesFilter(bodyFilterSlot, tid, fl.filterData))
                continue;
            const float3 surfN = nwbPhysicsSafeNormal(fl.surfaceNormalOffset.xyz, float3(0.0f, 1.0f, 0.0f));
            float d = 0.0f;
            float3 flowVel = float3(0.0f, 0.0f, 0.0f);
            float3 localSurfN = surfN;
            nwbPhysicsSubsysFluidEvalSurface(fl, gerstnerSlot, sim.positionInvMass.xyz, surfN, timeSec, d, flowVel, localSurfN);
            if(d + testR <= 0.0f)
                continue;
            const float subFrac = clamp((d + testR) / (2.0f * max(testR, 1.0e-4f)), 0.0f, 1.0f);
            const float subVol = bodyVol * subFrac;
            const float dispMass = fl.boundsMaxDensity.w * subVol;
            float3 fluidForce = (-grav) * (dispMass * sim.invInertiaWorld1.z);

            const float3 relV = flowVel - st.linearVelocityPad.xyz;
            const float spSq = dot(relV, relV);
            const float areaEst = 2.0f * (volInfo.y * volInfo.w + volInfo.y * volInfo.z + volInfo.z * volInfo.w) * subFrac;
            if(spSq > 1.0e-10f){
                const float invSp = rsqrt(spSq);
                const float sp = spSq * invSp;
                const float3 relDir = relV * invSp;
                const float linMag = fl.flowVelLinDrag.w * sp * subFrac * bodyMass
                    + 0.5f * fl.boundsMaxDensity.w * fl.dragLiftWaveAmpFreq.y * areaEst * spSq;
                fluidForce += relDir * min(linMag, bodyMass * sp * invDt * 0.9f);
                if(fl.dragLiftWaveAmpFreq.x != 0.0f){
                    const float3 liftDir = nwbPhysicsSafeNormal(localSurfN - relDir * dot(localSurfN, relDir), localSurfN);
                    const float rawLift = 0.5f * fl.boundsMaxDensity.w * fl.dragLiftWaveAmpFreq.x * areaEst * spSq;
                    const float maxLift = bodyMass * sp * invDt * 2.0f;
                    fluidForce += liftDir * clamp(rawLift, -maxLift, maxLift);
                }
            }

            const float addedMass = max(0.0f, fl.waveSpeedAngAddedFlags.z) * dispMass;
            if(addedMass > 1.0e-7f){
                const float effRatio = bodyMass / (bodyMass + addedMass);
                fluidForce = fluidForce * effRatio - (grav * sim.invInertiaWorld1.z) * (addedMass * effRatio);
            }
            addForce += fluidForce;

            const float3 angV = st.angularVelocityPad.xyz;
            const float wSq = dot(angV, angV);
            if(wSq > 1.0e-10f){
                const float invW = rsqrt(wSq);
                const float wSp = wSq * invW;
                const float3 wDir = angV * invW;
                const float rawT = fl.waveSpeedAngAddedFlags.y * subFrac * max(0.1f, testR) * wSp;
                const float kInvI = dot(wDir, nwbPhysicsMulSymmetric3(sim.invInertiaWorld0, sim.invInertiaWorld1, wDir));
                const float maxT = (kInvI > 1.0e-10f) ? ((1.0f / kInvI) * wSp * invDt * 0.9f) : rawT;
                addTorque -= wDir * min(rawT, maxT);
            }

            const float impactSpeed = max(0.0f, -dot(st.linearVelocityPad.xyz, surfN));
            const float impactImpulse = bodyMass * impactSpeed;
            const float splashThr = fl.obbCenterSplash.w;
            if(splashThr > 0.0f && subFrac > 0.01f && impactImpulse >= splashThr)
                nwbPhysicsEmitEvent(prepCounterSlot, eventRingSlot, maxEvents, NWB_PHYSICS_GPU_EVENT_FLUID_SPLASH, tid, fl.filterData.w, 1u, float4(sim.positionInvMass.xyz, subFrac), float4(localSurfN, impactSpeed), uint4(tid, fl.filterData.w, asuint(impactImpulse), 0u));
        }
        if(any(addForce != float3(0.0f, 0.0f, 0.0f)) || any(addTorque != float3(0.0f, 0.0f, 0.0f))){
            g_NwbPhysicsBodySimBuffers[bodySimSlot][tid].forceLinearDamping.xyz = sim.forceLinearDamping.xyz + addForce;
            g_NwbPhysicsBodySimBuffers[bodySimSlot][tid].torqueAngularDamping.xyz = sim.torqueAngularDamping.xyz + addTorque;
            if((bFlags & NWB_PHYSICS_BODY_FLAG_SLEEPING) != 0u){
                bFlags &= ~NWB_PHYSICS_BODY_FLAG_SLEEPING;
                nwbPhysicsWriteBodyFlags(bodyStateSlot, tid, bFlags);
                g_NwbPhysicsBodySimBuffers[bodySimSlot][tid].invInertiaWorld1.w = 0.0f;
            }
        }
        return;
    }

    if(mode == NWB_PHYSICS_SUBSYS_MODE_APPLY_EXPLOSION){
        const uint bodySimSlot = g_NwbPhysicsPushConstants.slots0.x;
        const uint bodyStateSlot = g_NwbPhysicsPushConstants.slots0.y;
        const uint bodyOutSlot = g_NwbPhysicsPushConstants.slots0.z;
        const uint bodyFilterSlot = g_NwbPhysicsPushConstants.slots0.w;
        const uint4 expFilter = uint4(g_NwbPhysicsPushConstants.slots1.xyz, 0u);
        const float impulseMag = g_NwbPhysicsPushConstants.deltaTime;
        const float upwardBias = g_NwbPhysicsPushConstants.invDeltaTime;
        const float radius = g_NwbPhysicsPushConstants.maxLinearVelocity;
        const float falloffPow = max(g_NwbPhysicsPushConstants.linearSlop, 0.0f);
        const float3 origin = g_NwbPhysicsPushConstants.gravitySpeculative.xyz;

        const NwbPhysicsGpuBodySim sim = g_NwbPhysicsBodySimBuffers[bodySimSlot][tid];
        NwbPhysicsGpuBodyState st = g_NwbPhysicsBodyStateBuffers[bodyStateSlot][tid];
        uint bFlags = asuint(st.deltaPositionFlags.w);
        if((bFlags & NWB_PHYSICS_BODY_FLAG_DYNAMIC) == 0u || sim.positionInvMass.w <= 0.0f || radius <= 0.0f)
            return;
        if(!nwbPhysicsSubsysBodyMatchesFilter(bodyFilterSlot, tid, expFilter))
            return;

        const float3 diff = sim.positionInvMass.xyz - origin;
        const float dist = length(diff);
        if(dist >= radius)
            return;
        const float atten = pow(clamp(1.0f - dist / radius, 0.0f, 1.0f), falloffPow);
        const float3 radDir = nwbPhysicsSafeNormal(diff, float3(0.0f, 1.0f, 0.0f));
        const float3 blastDir = nwbPhysicsSafeNormal(radDir + float3(0.0f, upwardBias, 0.0f), float3(0.0f, 1.0f, 0.0f));
        const float3 newVel = st.linearVelocityPad.xyz + blastDir * (impulseMag * atten * sim.positionInvMass.w);
        bFlags &= ~NWB_PHYSICS_BODY_FLAG_SLEEPING;
        g_NwbPhysicsBodyStateBuffers[bodyStateSlot][tid].linearVelocityPad = float4(newVel, asfloat(bFlags));
        g_NwbPhysicsBodyStateBuffers[bodyStateSlot][tid].deltaPositionFlags.w = asfloat(bFlags);
        g_NwbPhysicsBodySimBuffers[bodySimSlot][tid].invInertiaWorld1.w = 0.0f;
        if(bodyOutSlot != NWB_PHYSICS_INVALID_INDEX)
            g_NwbPhysicsBodyOutBuffers[bodyOutSlot][tid].linearVelocityFlags = float4(newVel, asfloat(bFlags));
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

