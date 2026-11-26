// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "physics_common.slangi"
#include "solver_body_helpers.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


float nwbPhysicsSubsysFieldRadialFalloff(const float distSq, const float invDist, const float minDist, const float falloffPow){
    if(falloffPow <= 0.0f)
        return 1.0f;
    const float minD = max(0.05f, minDist);
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

float3 nwbPhysicsSubsysEvalForceField(
    const NwbPhysicsGpuForceField f,
    const float3 pos,
    const float3 linVel,
    const float bodyMass,
    const float dragCoeffOrExtentSq,
    const float timeSec,
    const float invDt,
    const float3 worldGrav,
    const float gravScale,
    inout float3 inoutAngVel,
    inout float3 inoutTorque
){
    const uint fieldType = asuint(f.boundsMinType.w);
    const uint enabledBounds = asuint(f.boundsMaxFilter.w);
    if((enabledBounds & 1u) == 0u || bodyMass <= 0.0f)
        return float3(0.0f, 0.0f, 0.0f);
    if((enabledBounds & 2u) != 0u){
        const float3 bMin = f.boundsMinType.xyz;
        const float3 bMax = f.boundsMaxFilter.xyz;
        if(any(pos < bMin) || any(pos > bMax))
            return float3(0.0f, 0.0f, 0.0f);
    }

    const float strength = f.originOrDirStrength.w;
    const float radius = f.axisRadius.w;
    const float minDist = f.minDistFalloffTurb.x;
    const float falloffPow = f.minDistFalloffTurb.y;
    const float turbAmp = f.minDistFalloffTurb.z;
    const float turbFreq = f.minDistFalloffTurb.w;

    if(fieldType == 0u){
        const float3 d = nwbPhysicsSafeNormal(f.originOrDirStrength.xyz, float3(0.0f, -1.0f, 0.0f));
        return d * (strength * bodyMass);
    }
    if(fieldType == 1u){
        const float3 diff = f.originOrDirStrength.xyz - pos;
        const float distSq = dot(diff, diff);
        if(distSq <= 1.0e-12f || (radius > 0.0f && distSq > radius * radius))
            return float3(0.0f, 0.0f, 0.0f);
        const float invD = rsqrt(distSq);
        return diff * (invD * strength * bodyMass * nwbPhysicsSubsysFieldRadialFalloff(distSq, invD, minDist, falloffPow));
    }
    if(fieldType == 2u){
        const float3 dir = nwbPhysicsSafeNormal(f.originOrDirStrength.xyz, float3(1.0f, 0.0f, 0.0f));
        float3 windV = dir * strength;
        if(turbAmp > 0.0f){
            const float ph = timeSec * turbFreq + pos.x * 0.37f + pos.y * 0.53f + pos.z * 0.71f;
            float3 sV;
            float3 cV;
            sincos(float3(ph, ph * 1.31f, ph * 0.79f + 1.1f), sV, cV);
            windV += float3(sV.x, cV.y, sV.z) * turbAmp;
        }
        const float3 relWind = windV - linVel;
        const float relSq = dot(relWind, relWind);
        if(relSq <= 1.0e-12f)
            return float3(0.0f, 0.0f, 0.0f);
        const float invRel = rsqrt(relSq);
        const float relSp = relSq * invRel;
        const float clampedMag = min(relSp * dragCoeffOrExtentSq, bodyMass * relSp * invDt * 0.9f);
        return relWind * (clampedMag * invRel);
    }
    if(fieldType == 3u){
        const float3 axis = nwbPhysicsSafeNormal(f.axisRadius.xyz, float3(0.0f, 1.0f, 0.0f));
        const float3 diff = pos - f.originOrDirStrength.xyz;
        const float3 radPerp = diff - axis * dot(diff, axis);
        const float distSq = dot(radPerp, radPerp);
        if(distSq <= 1.0e-12f || (radius > 0.0f && distSq > radius * radius))
            return float3(0.0f, 0.0f, 0.0f);
        const float invD = rsqrt(distSq);
        const float3 rDir = radPerp * invD;
        const float3 swDir = nwbPhysicsSafeNormal(cross(axis, rDir), float3(0.0f, 0.0f, 0.0f));
        const float fall = nwbPhysicsSubsysFieldRadialFalloff(distSq, invD, minDist, falloffPow);
        const float inwardSuction = f.extraParams.x;
        float3 res = swDir * (strength * bodyMass * fall);
        if(inwardSuction != 0.0f)
            res -= rDir * (inwardSuction * bodyMass * fall);
        if(turbAmp > 0.0f){
            const float ph = timeSec * turbFreq + pos.x * 0.37f + pos.y * 0.53f + pos.z * 0.71f;
            float3 sV;
            float3 cV;
            sincos(float3(ph, ph * 1.31f, ph * 0.79f + 1.1f), sV, cV);
            res += float3(sV.x, cV.y, sV.z) * (turbAmp * bodyMass * fall);
        }
        return res;
    }
    if(fieldType == 4u){
        const float damp = max(strength, 0.0f);
        const float speedSq = dot(linVel, linVel);
        float3 fOut = float3(0.0f, 0.0f, 0.0f);
        if(speedSq > 1.0e-12f){
            const float invSp = rsqrt(speedSq);
            const float sp = speedSq * invSp;
            const float dampMag = min(damp * bodyMass * sp, bodyMass * sp * invDt * 0.9f);
            fOut = (-linVel) * (dampMag * invSp);
        }
        const float wSq = dot(inoutAngVel, inoutAngVel);
        if(wSq > 1.0e-12f && damp > 0.0f){
            const float wScale = exp(-damp * (1.0f / max(invDt, 1.0f)));
            inoutAngVel *= wScale;
            inoutTorque += float3(0.0f, 0.0f, 0.0f);
        }
        return fOut;
    }
    if(fieldType == 5u){
        const float3 d = nwbPhysicsSafeNormal(f.originOrDirStrength.xyz, float3(0.0f, -1.0f, 0.0f));
        return (d * strength - worldGrav * gravScale) * bodyMass;
    }
    return float3(0.0f, 0.0f, 0.0f);
}

float nwbPhysicsSubsysFluidSignedDepth(const NwbPhysicsGpuFluidVolume fl, const float3 pos, const float3 surfNorm, const float timeSec){
    const uint shapeType = asuint(fl.boundsMinShape.w);
    if(shapeType == 0u){
        float surfOff = fl.surfaceNormalOffset.w;
        const float waveAmp = fl.dragLiftWaveAmpFreq.z;
        if(waveAmp > 0.0f){
            const float3 upV = (abs(surfNorm.y) > 0.9f) ? float3(1.0f, 0.0f, 0.0f) : float3(0.0f, 1.0f, 0.0f);
            const float3 t1 = nwbPhysicsSafeNormal(cross(upV, surfNorm), float3(1.0f, 0.0f, 0.0f));
            const float3 t2 = cross(surfNorm, t1);
            const float waveFreq = fl.dragLiftWaveAmpFreq.w;
            const float waveSpd = fl.waveSpeedTimeFilter.x;
            const float u = dot(pos, t1) * waveFreq + timeSec * waveSpd;
            const float v = dot(pos, t2) * (waveFreq * 0.77f) - timeSec * (waveSpd * 0.83f);
            surfOff += waveAmp * (sin(u) * cos(v));
        }
        return surfOff - dot(pos, surfNorm);
    }
    if(shapeType == 2u){
        const float r = max(0.0f, fl.surfaceNormalOffset.w);
        return r - length(pos - fl.surfaceNormalOffset.xyz);
    }
    const float3 d = min(pos - fl.boundsMinShape.xyz, fl.boundsMaxDensity.xyz - pos);
    return min(d.x, min(d.y, d.z));
}

bool nwbPhysicsSubsysCollideParticleShape(
    const NwbPhysicsGpuShapeProxy sp,
    const float3 sOrig,
    const float4 sRot,
    const uint hullSlot,
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
        const uint fCnt = min((meta >> 16u) & 0xffu, NWB_PHYSICS_MAX_HULL_FACES);
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

    if((sType == NWB_PHYSICS_SHAPE_TYPE_CYLINDER || sType == NWB_PHYSICS_SHAPE_TYPE_CONE)){
        const float4 cq = sp.localRotationOrMeta;
        const float3 rel = nwbPhysicsQuatInvRotate(lp - sp.localCenterRadius.xyz, cq);
        const float rC = max(sp.localExtentOrP1.x, 0.0f);
        const float hh = max(sp.localExtentOrP1.y, 0.0f);
        const float radSq = rel.x * rel.x + rel.z * rel.z;
        const float rad = sqrt(radSq);
        const float effR = (sType == NWB_PHYSICS_SHAPE_TYPE_CONE && hh > 1.0e-6f)
            ? (rC * clamp((hh - rel.y) / (2.0f * hh), 0.0f, 1.0f))
            : rC;
        if(rad > effR + sumR || abs(rel.y) > hh + sumR)
            return false;
        const float3 rDir = (rad > 1.0e-6f) ? float3(rel.x / rad, 0.0f, rel.z / rad) : float3(1.0f, 0.0f, 0.0f);
        const float3 clamped = rDir * min(rad, effR) + float3(0.0f, clamp(rel.y, -hh, hh), 0.0f);
        const float3 d = rel - clamped;
        const float dLen = length(d);
        if(dLen >= sumR)
            return false;
        float3 nLoc = (dLen > 1.0e-6f) ? (d / dLen) : ((hh - abs(rel.y) < effR - rad) ? float3(0.0f, rel.y >= 0.0f ? 1.0f : -1.0f, 0.0f) : rDir);
        outNormal = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(nwbPhysicsQuatRotate(nLoc, cq), sRot), float3(0.0f, 1.0f, 0.0f));
        outPen = (dLen > 1.0e-6f) ? (sumR - dLen) : (min(effR - rad, hh - abs(rel.y)) + sumR);
        return outPen > 1.0e-5f;
    }

    if(sType == NWB_PHYSICS_SHAPE_TYPE_HEIGHTFIELD && hullSlot != NWB_PHYSICS_INVALID_INDEX && sp.filterData.w != NWB_PHYSICS_INVALID_INDEX){
        const uint hfBase = sp.filterData.w;
        const float4 hdr0 = g_NwbPhysicsFloat4Buffers[hullSlot][hfBase + 0u];
        const float4 hdr1 = g_NwbPhysicsFloat4Buffers[hullSlot][hfBase + 1u];
        const float4 hdr2 = g_NwbPhysicsFloat4Buffers[hullSlot][hfBase + 2u];
        const float3 hfOrg = hdr0.xyz;
        const float sx = hdr1.x;
        const float sy = hdr1.y;
        const float sz = hdr1.z;
        const uint cols = asuint(hdr2.x);
        const uint rows = asuint(hdr2.y);
        const uint hfFlags = asuint(hdr2.z);
        const float thick = hdr2.w;
        if(cols < 2u || rows < 2u || sx <= 0.0f || sz <= 0.0f)
            return false;
        const int maxCx = int(cols - 2u);
        const int maxCz = int(rows - 2u);
        const int cxI = int(floor((lp.x - hfOrg.x) / sx));
        const int czI = int(floor((lp.z - hfOrg.z) / sz));
        if(cxI < 0 || cxI > maxCx || czI < 0 || czI > maxCz)
            return false;
        float3 v00;
        float3 v10;
        float3 v01;
        float3 v11;
        if(!nwbPhysicsHfLoadCellVertices(hullSlot, hfBase, cols, rows, hfOrg, sx, sz, sy, hfFlags, uint(cxI), uint(czI), v00, v10, v01, v11))
            return false;
        const bool flipD = (hfFlags & NWB_PHYSICS_HF_FLAG_FLIP_DIAGONAL) != 0u;
        float bestDist = pRadius + thick;
        bool hitTri = false;
        [loop] for(uint ti = 0u; ti < 2u; ++ti){
            float3 tv0;
            float3 tv1;
            float3 tv2;
            nwbPhysicsHfExtractTriangle(flipD, ti, v00, v10, v01, v11, tv0, tv1, tv2);
            const float3 tN = nwbPhysicsHfTriNormal(tv0, tv1, tv2);
            const float3 q = nwbPhysicsClosestPointOnTriangle(lp, tv0, tv1, tv2);
            const float signedD = dot(lp - q, tN);
            if(signedD > -thick - pRadius && signedD < bestDist){
                bestDist = signedD;
                outNormal = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(tN, sRot), float3(0.0f, 1.0f, 0.0f));
                outPen = (pRadius + thick * 0.0f) - signedD;
                hitTri = true;
            }
        }
        return hitTri && outPen > 1.0e-5f;
    }

    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


groupshared float3 s_SoftAabbMin[NWB_PHYSICS_WORKGROUP_SIZE];
groupshared float3 s_SoftAabbMax[NWB_PHYSICS_WORKGROUP_SIZE];


[numthreads(NWB_PHYSICS_WORKGROUP_SIZE, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID, uint3 groupThreadID : SV_GroupThreadID){
    const uint tid = dispatchThreadID.x;
    const uint itemCount = g_NwbPhysicsPushConstants.itemCount;
    const uint mode = g_NwbPhysicsPushConstants.modeOrSecondaryCount;
    const uint flags = g_NwbPhysicsPushConstants.flags;

    if(mode == NWB_PHYSICS_SUBSYS_MODE_SOFT_BACKSTOP_COLLIDE && flags != 0u){
        const uint pSlot = g_NwbPhysicsPushConstants.slots0.x;
        const uint shapeSlot = g_NwbPhysicsPushConstants.slots0.z;
        const uint bodyPoseSlot = g_NwbPhysicsPushConstants.slots0.w;
        const uint hullSlot = g_NwbPhysicsPushConstants.slots1.x;
        const uint worldAabbSlot = g_NwbPhysicsPushConstants.slots1.y;
        const uint worldShapeCount = g_NwbPhysicsPushConstants.itemOffset;
        const float fricScale = g_NwbPhysicsPushConstants.deltaTime;
        const float restCoeff = clamp(g_NwbPhysicsPushConstants.invDeltaTime, 0.0f, 1.0f);
        const float pRadius = g_NwbPhysicsPushConstants.maxLinearVelocity;
        const float3 radVec = float3(pRadius, pRadius, pRadius);

        NwbPhysicsGpuSoftParticle pt;
        bool canCollide = false;
        float3 curPos = float3(0.0f, 0.0f, 0.0f);
        float3 prevPos = float3(0.0f, 0.0f, 0.0f);
        if(tid < itemCount){
            pt = g_NwbPhysicsSoftParticleBuffers[pSlot][tid];
            if((asuint(pt.prevPosFlags.w) & 1u) == 0u && pt.posInvMass.w > 0.0f){
                canCollide = true;
                curPos = pt.posInvMass.xyz;
                prevPos = pt.prevPosFlags.xyz;
            }
        }

        [loop] for(uint tileBase = 0u; tileBase < worldShapeCount; tileBase += NWB_PHYSICS_WORKGROUP_SIZE){
            const uint loadIdx = tileBase + groupThreadID.x;
            if(loadIdx < worldShapeCount && worldAabbSlot != NWB_PHYSICS_INVALID_INDEX){
                s_SoftAabbMin[groupThreadID.x] = g_NwbPhysicsFloat4Buffers[worldAabbSlot][loadIdx * 2u + 0u].xyz;
                s_SoftAabbMax[groupThreadID.x] = g_NwbPhysicsFloat4Buffers[worldAabbSlot][loadIdx * 2u + 1u].xyz;
            }
            GroupMemoryBarrierWithGroupSync();

            if(canCollide){
                float3 pMin = curPos - radVec;
                float3 pMax = curPos + radVec;
                const uint tileEnd = min(NWB_PHYSICS_WORKGROUP_SIZE, worldShapeCount - tileBase);
                [loop] for(uint j = 0u; j < tileEnd; ++j){
                    if(worldAabbSlot != NWB_PHYSICS_INVALID_INDEX && (any(pMax < s_SoftAabbMin[j]) || any(pMin > s_SoftAabbMax[j])))
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
                    if(nwbPhysicsSubsysCollideParticleShape(sp, sOrig, sRot, hullSlot, curPos, pRadius, outN, pen)){
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
            pt.posInvMass.xyz = curPos;
            pt.prevPosFlags.xyz = prevPos;
            g_NwbPhysicsSoftParticleBuffers[pSlot][tid] = pt;
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
        const uint fieldCount = g_NwbPhysicsPushConstants.itemOffset;
        const uint fluidCount = flags;
        const float h = g_NwbPhysicsPushConstants.deltaTime;
        const float dampScale = g_NwbPhysicsPushConstants.invDeltaTime;
        const float dragCoeff = g_NwbPhysicsPushConstants.maxLinearVelocity;
        const float timeSec = g_NwbPhysicsPushConstants.linearSlop;
        const float3 grav = g_NwbPhysicsPushConstants.gravitySpeculative.xyz;
        const float invH = (h > 1.0e-5f) ? (1.0f / h) : 1.0e5f;

        NwbPhysicsGpuSoftParticle pt = g_NwbPhysicsSoftParticleBuffers[pSlot][tid];
        pt.prevPosFlags.xyz = pt.posInvMass.xyz;
        const uint pFlags = asuint(pt.prevPosFlags.w);
        const float invMass = pt.posInvMass.w;
        if((pFlags & 1u) != 0u || invMass <= 0.0f){
            pt.velPad.xyz = float3(0.0f, 0.0f, 0.0f);
            g_NwbPhysicsSoftParticleBuffers[pSlot][tid] = pt;
            return;
        }

        const float mass = 1.0f / invMass;
        float3 totalF = pt.extForcePad.xyz;
        const float dragK = clamp(dragCoeff * 4.0f, 0.05f, 4.0f) * mass;
        float3 dummyAng = float3(0.0f, 0.0f, 0.0f);
        float3 dummyTrq = float3(0.0f, 0.0f, 0.0f);
        if(fieldSlot != NWB_PHYSICS_INVALID_INDEX && fieldCount > 0u){
            [loop] for(uint fi = 0u; fi < fieldCount; ++fi)
                totalF += nwbPhysicsSubsysEvalForceField(g_NwbPhysicsForceFieldBuffers[fieldSlot][fi], pt.posInvMass.xyz, pt.velPad.xyz, mass, dragK, timeSec, invH, grav, 1.0f, dummyAng, dummyTrq);
        }
        if(fluidSlot != NWB_PHYSICS_INVALID_INDEX && fluidCount > 0u){
            const float pRad = max(g_NwbPhysicsPushConstants.gravitySpeculative.w, 1.0e-2f);
            const float pVol = (4.0f / 3.0f) * 3.1415926535897932f * pRad * pRad * pRad;
            [loop] for(uint fli = 0u; fli < fluidCount; ++fli){
                const NwbPhysicsGpuFluidVolume fl = g_NwbPhysicsFluidVolumeBuffers[fluidSlot][fli];
                if(asuint(fl.waveSpeedTimeFilter.w) == 0u)
                    continue;
                const float3 surfN = nwbPhysicsSafeNormal(fl.surfaceNormalOffset.xyz, float3(0.0f, 1.0f, 0.0f));
                const float d = nwbPhysicsSubsysFluidSignedDepth(fl, pt.posInvMass.xyz, surfN, timeSec);
                if(d + pRad > 0.0f){
                    const float hCap = clamp(d + pRad, 0.0f, 2.0f * pRad);
                    const float vCap = (3.1415926535897932f / 3.0f) * hCap * hCap * (3.0f * pRad - hCap);
                    const float frac = clamp(vCap / max(pVol, 1.0e-7f), 0.0f, 1.0f);
                    const float dispM = fl.boundsMaxDensity.w * vCap;
                    totalF += (-grav) * dispM;
                    const float3 relV = fl.flowVelLinDrag.xyz - pt.velPad.xyz;
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
        const float3 newVel = (pt.velPad.xyz + acc * h) * dampScale;
        pt.velPad.xyz = newVel;
        pt.posInvMass.xyz += newVel * h;
        pt.extForcePad.xyz = float3(0.0f, 0.0f, 0.0f);
        g_NwbPhysicsSoftParticleBuffers[pSlot][tid] = pt;
        return;
    }

    if(mode == NWB_PHYSICS_SUBSYS_MODE_SOFT_RESET_LAMBDAS){
        const uint conSlot = g_NwbPhysicsPushConstants.slots0.x;
        if(flags == 0u)
            g_NwbPhysicsSoftDistConstraintBuffers[conSlot][idx].restCompLambdaPad.z = 0.0f;
        else
            g_NwbPhysicsSoftVolConstraintBuffers[conSlot][idx].restCompLambdaPad.z = 0.0f;
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
        NwbPhysicsGpuSoftParticle p0 = g_NwbPhysicsSoftParticleBuffers[pSlot][i0];
        NwbPhysicsGpuSoftParticle p1 = g_NwbPhysicsSoftParticleBuffers[pSlot][i1];
        const float w0 = ((asuint(p0.prevPosFlags.w) & 1u) != 0u) ? 0.0f : p0.posInvMass.w;
        const float w1 = ((asuint(p1.prevPosFlags.w) & 1u) != 0u) ? 0.0f : p1.posInvMass.w;
        const float wSum = w0 + w1;
        if(wSum <= 1.0e-12f)
            return;
        const float3 diff = p0.posInvMass.xyz - p1.posInvMass.xyz;
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
            g_NwbPhysicsSoftParticleBuffers[pSlot][i0].posInvMass.xyz = p0.posInvMass.xyz + n * (dLambda * w0);
        if(w1 > 0.0f)
            g_NwbPhysicsSoftParticleBuffers[pSlot][i1].posInvMass.xyz = p1.posInvMass.xyz - n * (dLambda * w1);
        return;
    }

    if(mode == NWB_PHYSICS_SUBSYS_MODE_SOFT_VOLUME_COLOR){
        const uint pSlot = g_NwbPhysicsPushConstants.slots0.x;
        const uint vcSlot = g_NwbPhysicsPushConstants.slots0.y;
        const float invDt2 = g_NwbPhysicsPushConstants.deltaTime;
        NwbPhysicsGpuSoftVolConstraint vc = g_NwbPhysicsSoftVolConstraintBuffers[vcSlot][idx];
        NwbPhysicsGpuSoftParticle p0 = g_NwbPhysicsSoftParticleBuffers[pSlot][vc.indices.x];
        NwbPhysicsGpuSoftParticle p1 = g_NwbPhysicsSoftParticleBuffers[pSlot][vc.indices.y];
        NwbPhysicsGpuSoftParticle p2 = g_NwbPhysicsSoftParticleBuffers[pSlot][vc.indices.z];
        NwbPhysicsGpuSoftParticle p3 = g_NwbPhysicsSoftParticleBuffers[pSlot][vc.indices.w];
        const float w0 = ((asuint(p0.prevPosFlags.w) & 1u) != 0u) ? 0.0f : p0.posInvMass.w;
        const float w1 = ((asuint(p1.prevPosFlags.w) & 1u) != 0u) ? 0.0f : p1.posInvMass.w;
        const float w2 = ((asuint(p2.prevPosFlags.w) & 1u) != 0u) ? 0.0f : p2.posInvMass.w;
        const float w3 = ((asuint(p3.prevPosFlags.w) & 1u) != 0u) ? 0.0f : p3.posInvMass.w;
        const float3 x0 = p0.posInvMass.xyz;
        const float3 x1 = p1.posInvMass.xyz;
        const float3 x2 = p2.posInvMass.xyz;
        const float3 x3 = p3.posInvMass.xyz;
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
        if(w0 > 0.0f) g_NwbPhysicsSoftParticleBuffers[pSlot][vc.indices.x].posInvMass.xyz = x0 + g0 * (dLambda * w0);
        if(w1 > 0.0f) g_NwbPhysicsSoftParticleBuffers[pSlot][vc.indices.y].posInvMass.xyz = x1 + g1 * (dLambda * w1);
        if(w2 > 0.0f) g_NwbPhysicsSoftParticleBuffers[pSlot][vc.indices.z].posInvMass.xyz = x2 + g2 * (dLambda * w2);
        if(w3 > 0.0f) g_NwbPhysicsSoftParticleBuffers[pSlot][vc.indices.w].posInvMass.xyz = x3 + g3 * (dLambda * w3);
        return;
    }

    if(mode == NWB_PHYSICS_SUBSYS_MODE_SOFT_BACKSTOP_COLLIDE){
        const uint pSlot = g_NwbPhysicsPushConstants.slots0.x;
        const uint backstopSlot = g_NwbPhysicsPushConstants.slots0.y;
        const NwbPhysicsGpuSoftBackstop bs = g_NwbPhysicsSoftBackstopBuffers[backstopSlot][tid];
        const uint pIdx = asuint(bs.normalRadiusIdx.x);
        NwbPhysicsGpuSoftParticle pt = g_NwbPhysicsSoftParticleBuffers[pSlot][pIdx];
        if((asuint(pt.prevPosFlags.w) & 1u) != 0u || pt.posInvMass.w <= 0.0f)
            return;
        float3 pCur = pt.posInvMass.xyz;
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
        g_NwbPhysicsSoftParticleBuffers[pSlot][pIdx].posInvMass.xyz = pCur;
        return;
    }

    if(mode == NWB_PHYSICS_SUBSYS_MODE_SOFT_VEL_UPDATE){
        const uint pSlot = g_NwbPhysicsPushConstants.slots0.x;
        const float invH = g_NwbPhysicsPushConstants.deltaTime;
        NwbPhysicsGpuSoftParticle pt = g_NwbPhysicsSoftParticleBuffers[pSlot][tid];
        if((asuint(pt.prevPosFlags.w) & 1u) != 0u || pt.posInvMass.w <= 0.0f)
            pt.velPad.xyz = float3(0.0f, 0.0f, 0.0f);
        else
            pt.velPad.xyz = (pt.posInvMass.xyz - pt.prevPosFlags.xyz) * invH;
        g_NwbPhysicsSoftParticleBuffers[pSlot][tid].velPad.xyz = pt.velPad.xyz;
        return;
    }

    if(mode == NWB_PHYSICS_SUBSYS_MODE_APPLY_FORCE_FIELDS){
        const uint bodySimSlot = g_NwbPhysicsPushConstants.slots0.x;
        const uint bodyStateSlot = g_NwbPhysicsPushConstants.slots0.y;
        const uint fieldSlot = g_NwbPhysicsPushConstants.slots0.z;
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
        const float minExt = max(sim.localCenterMinExtent.w, 0.05f);
        const float boxWindK = max(0.05f, minExt * minExt) * 2.0f;
        float3 angVel = st.angularVelocityPad.xyz;
        const float3 origAng = angVel;
        float3 addForce = float3(0.0f, 0.0f, 0.0f);
        float3 addTorque = float3(0.0f, 0.0f, 0.0f);
        [loop] for(uint fi = 0u; fi < fieldCount; ++fi){
            addForce += nwbPhysicsSubsysEvalForceField(
                g_NwbPhysicsForceFieldBuffers[fieldSlot][fi],
                sim.positionInvMass.xyz,
                st.linearVelocityPad.xyz,
                bodyMass,
                boxWindK,
                timeSec,
                invDt,
                worldGrav,
                sim.invInertiaWorld1.z,
                angVel,
                addTorque
            );
        }
        if(any(addForce != float3(0.0f, 0.0f, 0.0f)) || any(angVel != origAng)){
            g_NwbPhysicsBodySimBuffers[bodySimSlot][tid].forceLinearDamping.xyz = sim.forceLinearDamping.xyz + addForce;
            g_NwbPhysicsBodySimBuffers[bodySimSlot][tid].torqueAngularDamping.xyz = sim.torqueAngularDamping.xyz + addTorque;
            if((bFlags & NWB_PHYSICS_BODY_FLAG_SLEEPING) != 0u){
                bFlags &= ~NWB_PHYSICS_BODY_FLAG_SLEEPING;
                nwbPhysicsWriteBodyFlags(bodyStateSlot, tid, bFlags);
                g_NwbPhysicsBodySimBuffers[bodySimSlot][tid].invInertiaWorld1.w = 0.0f;
            }
            if(any(angVel != origAng))
                g_NwbPhysicsBodyStateBuffers[bodyStateSlot][tid].angularVelocityPad.xyz = angVel;
        }
        return;
    }

    if(mode == NWB_PHYSICS_SUBSYS_MODE_APPLY_FLUID_BUOYANCY){
        const uint bodySimSlot = g_NwbPhysicsPushConstants.slots0.x;
        const uint bodyStateSlot = g_NwbPhysicsPushConstants.slots0.y;
        const uint fluidSlot = g_NwbPhysicsPushConstants.slots0.z;
        const uint bodyVolSlot = g_NwbPhysicsPushConstants.slots0.w;
        const uint fluidCount = g_NwbPhysicsPushConstants.itemOffset;
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
            if(asuint(fl.waveSpeedTimeFilter.w) == 0u)
                continue;
            const float3 surfN = nwbPhysicsSafeNormal(fl.surfaceNormalOffset.xyz, float3(0.0f, 1.0f, 0.0f));
            const float d = nwbPhysicsSubsysFluidSignedDepth(fl, sim.positionInvMass.xyz, surfN, timeSec);
            if(d + testR <= 0.0f)
                continue;
            const float subFrac = clamp((d + testR) / (2.0f * max(testR, 1.0e-4f)), 0.0f, 1.0f);
            const float subVol = bodyVol * subFrac;
            const float dispMass = fl.boundsMaxDensity.w * subVol;
            addForce += (-grav) * (dispMass * sim.invInertiaWorld1.z);

            const float3 relV = fl.flowVelLinDrag.xyz - st.linearVelocityPad.xyz;
            const float spSq = dot(relV, relV);
            if(spSq > 1.0e-10f){
                const float invSp = rsqrt(spSq);
                const float sp = spSq * invSp;
                const float areaEst = 2.0f * (volInfo.y * volInfo.w + volInfo.y * volInfo.z) * subFrac;
                const float linMag = fl.flowVelLinDrag.w * sp * subFrac * bodyMass
                    + 0.5f * fl.boundsMaxDensity.w * fl.dragLiftWaveAmpFreq.y * areaEst * spSq * 0.01f;
                addForce += (relV * invSp) * min(linMag, bodyMass * sp * invDt * 0.9f);
                if(fl.dragLiftWaveAmpFreq.x != 0.0f)
                    addForce += surfN * (0.5f * fl.boundsMaxDensity.w * fl.dragLiftWaveAmpFreq.x * areaEst * spSq * 0.01f);
            }

            const float3 angV = st.angularVelocityPad.xyz;
            const float wSq = dot(angV, angV);
            if(wSq > 1.0e-10f){
                const float invW = rsqrt(wSq);
                const float wSp = wSq * invW;
                const float3 wDir = angV * invW;
                const float rawT = fl.waveSpeedTimeFilter.y * subFrac * max(0.1f, testR) * wSp;
                const float kInvI = dot(wDir, nwbPhysicsMulSymmetric3(sim.invInertiaWorld0, sim.invInertiaWorld1, wDir));
                const float maxT = (kInvI > 1.0e-10f) ? ((1.0f / kInvI) * wSp * invDt * 0.9f) : rawT;
                addTorque -= wDir * min(rawT, maxT);
            }
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

