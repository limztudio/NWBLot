// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "physics_common.slangi"

#ifndef NWB_PHYSICS_NP_PROFILE
#define NWB_PHYSICS_NP_PROFILE 0
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool nwbPhysicsAabbOverlaps(const float3 minA, const float3 maxA, const float3 minB, const float3 maxB){
    return all(minA <= maxB) && all(minB <= maxA);
}


float3 nwbPhysicsClosestPointOnSegment(const float3 a, const float3 b, const float3 p){
    const float3 ab = b - a;
    const float lenSq = dot(ab, ab);
    if(lenSq <= 1.0e-12f)
        return a;
    const float t = clamp(dot(p - a, ab) / lenSq, 0.0f, 1.0f);
    return a + ab * t;
}

void nwbPhysicsClosestPointsSegments(
    const float3 p1,
    const float3 q1,
    const float3 p2,
    const float3 q2,
    out float outS,
    out float outT,
    out float3 outC1,
    out float3 outC2
){
    const float3 d1 = q1 - p1;
    const float3 d2 = q2 - p2;
    const float3 r = p1 - p2;
    const float a = dot(d1, d1);
    const float e = dot(d2, d2);
    const float f = dot(d2, r);

    float s = 0.0f;
    float t = 0.0f;

    if(a <= 1.0e-12f && e <= 1.0e-12f){
        s = 0.0f;
        t = 0.0f;
    }
    else if(a <= 1.0e-12f){
        s = 0.0f;
        t = clamp(f / e, 0.0f, 1.0f);
    }
    else{
        const float c = dot(d1, r);
        if(e <= 1.0e-12f){
            t = 0.0f;
            s = clamp(-c / a, 0.0f, 1.0f);
        }
        else{
            const float b = dot(d1, d2);
            const float denom = a * e - b * b;
            if(denom > 1.0e-12f)
                s = clamp((b * f - c * e) / denom, 0.0f, 1.0f);
            else
                s = 0.0f;

            t = (b * s + f) / e;
            if(t < 0.0f){
                t = 0.0f;
                s = clamp(-c / a, 0.0f, 1.0f);
            }
            else if(t > 1.0f){
                t = 1.0f;
                s = clamp((b - c) / a, 0.0f, 1.0f);
            }
        }
    }

    outS = s;
    outT = t;
    outC1 = p1 + d1 * s;
    outC2 = p2 + d2 * t;
}

float3 nwbPhysicsSafeNormal(const float3 v, const float3 fallback){
    const float lenSq = dot(v, v);
    if(lenSq > 1.0e-12f)
        return v * rsqrt(lenSq);
    return fallback;
}

void nwbPhysicsWorldTransform(const NwbPhysicsGpuBodySim body, out float3 outOrigin, out float4 outRot){
    outRot = body.rotation;
    const float3 localCenter = body.localCenterMinExtent.xyz;
    outOrigin = body.positionInvMass.xyz - nwbPhysicsQuatRotate(localCenter, outRot);
}

float nwbPhysicsProjectBoxHalfExtent(const float3 ax, const float3 ay, const float3 az, const float3 halfExtents, const float3 dir){
    return abs(dot(ax, dir)) * halfExtents.x + abs(dot(ay, dir)) * halfExtents.y + abs(dot(az, dir)) * halfExtents.z;
}

void nwbPhysicsGetBoxFace(
    const float3 center,
    const float3 ax,
    const float3 ay,
    const float3 az,
    const float3 halfExtents,
    const int faceIdx,
    out float3 outVerts[4]
){
    float3 n = ax;
    float3 u = ay;
    float3 v = az;
    float dn = halfExtents.x;
    float du = halfExtents.y;
    float dv = halfExtents.z;

    if(faceIdx == 0){
        n = ax; u = ay; v = az; dn = halfExtents.x; du = halfExtents.y; dv = halfExtents.z;
    }
    else if(faceIdx == 1){
        n = -ax; u = -ay; v = az; dn = halfExtents.x; du = halfExtents.y; dv = halfExtents.z;
    }
    else if(faceIdx == 2){
        n = ay; u = az; v = ax; dn = halfExtents.y; du = halfExtents.z; dv = halfExtents.x;
    }
    else if(faceIdx == 3){
        n = -ay; u = -az; v = ax; dn = halfExtents.y; du = halfExtents.z; dv = halfExtents.x;
    }
    else if(faceIdx == 4){
        n = az; u = ax; v = ay; dn = halfExtents.z; du = halfExtents.x; dv = halfExtents.y;
    }
    else{
        n = -az; u = -ax; v = ay; dn = halfExtents.z; du = halfExtents.x; dv = halfExtents.y;
    }

    const float3 fc = center + n * dn;
    const float3 eu = u * du;
    const float3 ev = v * dv;
    outVerts[0] = fc - eu - ev;
    outVerts[1] = fc + eu - ev;
    outVerts[2] = fc + eu + ev;
    outVerts[3] = fc - eu + ev;
}

float nwbPhysicsClipIntervalAxis(const float p0, const float d, const float hExt, inout float tMin, inout float tMax){
    if(abs(d) < 1.0e-8f){
        if(p0 < -hExt || p0 > hExt){
            tMin = 1.0f;
            tMax = 0.0f;
        }
        return 0.0f;
    }
    float t0 = (-hExt - p0) / d;
    float t1 = (hExt - p0) / d;
    if(t0 > t1){
        const float tmp = t0;
        t0 = t1;
        t1 = tmp;
    }
    tMin = max(tMin, t0);
    tMax = min(tMax, t1);
    return 1.0f;
}

void nwbPhysicsComputeBasis(const float3 n, out float3 outU1, out float3 outU2){
    const float3 up = abs(n.y) < 0.9f ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
    outU1 = nwbPhysicsSafeNormal(cross(up, n), float3(1.0f, 0.0f, 0.0f));
    outU2 = cross(n, outU1);
}

#if NWB_PHYSICS_NP_PROFILE == 0
uint nwbPhysicsClipBox2DPlaneAxis8(
    const float4 inPts[8],
    const uint inCount,
    const uint axisCoord,
    const float signVal,
    const float planeOffset,
    const uint planeId,
    out float4 outPts[8]
){
    uint outCount = 0u;
    if(inCount == 0u){
        outPts[0] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return 0u;
    }

    float4 prevPt = inPts[inCount - 1u];
    uint prevFeat = asuint(prevPt.w);
    float prevDist = signVal * ((axisCoord == 0u) ? prevPt.x : prevPt.y) - planeOffset;

    for(uint i = 0u; i < inCount; ++i){
        const float4 curPt = inPts[i];
        const uint curFeat = asuint(curPt.w);
        const float curDist = signVal * ((axisCoord == 0u) ? curPt.x : curPt.y) - planeOffset;

        if(curDist <= 0.0f){
            if(prevDist > 0.0f){
                const float denom = prevDist - curDist;
                const float t = abs(denom) > 1.0e-12f ? (prevDist / denom) : 0.0f;
                if(outCount < 8u){
                    const uint feat = 0x4000u | ((planeId & 0xfu) << 8u) | (curFeat & 0xffu);
                    outPts[outCount] = float4(lerp(prevPt.xyz, curPt.xyz, clamp(t, 0.0f, 1.0f)), asfloat(feat));
                    ++outCount;
                }
            }
            if(outCount < 8u){
                outPts[outCount] = curPt;
                ++outCount;
            }
        }
        else if(prevDist <= 0.0f){
            const float denom = prevDist - curDist;
            const float t = abs(denom) > 1.0e-12f ? (prevDist / denom) : 0.0f;
            if(outCount < 8u){
                const uint feat = 0x8000u | ((planeId & 0xfu) << 8u) | (prevFeat & 0xffu);
                outPts[outCount] = float4(lerp(prevPt.xyz, curPt.xyz, clamp(t, 0.0f, 1.0f)), asfloat(feat));
                ++outCount;
            }
        }

        prevPt = curPt;
        prevFeat = curFeat;
        prevDist = curDist;
    }
    return outCount;
}

void nwbPhysicsReduceBoxManifoldTo4(
    const float3 uAxis,
    const float3 vAxis,
    const float3 clipRefN,
    const float3 refFaceCenter,
    const float4 candPts[8],
    const uint candCount,
    const bool refIsA,
    const float rA,
    const float rB,
    const float totalR,
    const float3 bodyPosA,
    const float3 bodyPosB,
    inout NwbPhysicsGpuNarrowphaseManifold result
){
    uint outIndices[4] = { 0u, 1u, 2u, 3u };
    uint outCount = candCount;
    if(candCount > NWB_PHYSICS_MAX_MANIFOLD_POINTS){
        uint idx0 = 0u;
        for(uint i = 1u; i < candCount; ++i){
            if(candPts[i].z < candPts[idx0].z)
                idx0 = i;
        }

        uint idx1 = 0u;
        float bestDistSq1 = -1.0f;
        const float3 p0 = candPts[idx0].xyz;
        for(uint i = 0u; i < candCount; ++i){
            const float3 diff = candPts[i].xyz - p0;
            const float dSq = dot(diff, diff);
            if(dSq > bestDistSq1){
                bestDistSq1 = dSq;
                idx1 = i;
            }
        }

        uint idx2 = 0u;
        float bestArea2 = -1.0f;
        const float3 p1 = candPts[idx1].xyz;
        const float2 e01 = p1.xy - p0.xy;
        for(uint i = 0u; i < candCount; ++i){
            if(i == idx0 || i == idx1)
                continue;
            const float2 d0i = candPts[i].xy - p0.xy;
            const float area = abs(e01.x * d0i.y - e01.y * d0i.x);
            if(area > bestArea2){
                bestArea2 = area;
                idx2 = i;
            }
        }

        uint idx3 = 0u;
        float bestOutsideArea = -1.0f;
        uint fallbackIdx3 = 0u;
        float bestFallbackArea = -1.0f;
        const float2 p2 = candPts[idx2].xy;
        const float2 e12 = p2 - p1.xy;
        const float2 e20 = p0.xy - p2;
        const float2 d02 = p2 - p0.xy;
        const float baseSign = e01.x * d02.y - e01.y * d02.x;
        for(uint i = 0u; i < candCount; ++i){
            if(i == idx0 || i == idx1 || i == idx2)
                continue;
            const float2 pi = candPts[i].xy;
            const float2 d0i = pi - p0.xy;
            const float2 d1i = pi - p1.xy;
            const float2 d2i = pi - p2;
            const float a0 = e01.x * d0i.y - e01.y * d0i.x;
            const float a1 = e12.x * d1i.y - e12.y * d1i.x;
            const float a2 = e20.x * d2i.y - e20.y * d2i.x;
            const float maxAbs = max(abs(a0), max(abs(a1), abs(a2)));
            if(maxAbs > bestFallbackArea){
                bestFallbackArea = maxAbs;
                fallbackIdx3 = i;
            }
            float outsideArea = -1.0f;
            if(baseSign >= 0.0f){
                if(a0 < 0.0f && -a0 > outsideArea) outsideArea = -a0;
                if(a1 < 0.0f && -a1 > outsideArea) outsideArea = -a1;
                if(a2 < 0.0f && -a2 > outsideArea) outsideArea = -a2;
            }
            else{
                if(a0 > 0.0f && a0 > outsideArea) outsideArea = a0;
                if(a1 > 0.0f && a1 > outsideArea) outsideArea = a1;
                if(a2 > 0.0f && a2 > outsideArea) outsideArea = a2;
            }
            if(outsideArea > bestOutsideArea){
                bestOutsideArea = outsideArea;
                idx3 = i;
            }
        }
        if(bestOutsideArea < 0.0f)
            idx3 = fallbackIdx3;

        outIndices[0] = idx0;
        outIndices[1] = idx1;
        outIndices[2] = idx2;
        outIndices[3] = idx3;
        outCount = NWB_PHYSICS_MAX_MANIFOLD_POINTS;
    }

    for(uint k = 0u; k < outCount; ++k){
        const uint ci = outIndices[k];
        const float4 pt = candPts[ci];
        const float sep = pt.z - totalR;
        const float3 worldPt = refFaceCenter + uAxis * pt.x + vAxis * pt.y + clipRefN * pt.z;
        const float3 wRef = worldPt - clipRefN * (sep + totalR - (refIsA ? rA : rB));
        const float3 wInc = worldPt - clipRefN * (refIsA ? rB : rA);
        const float3 wA = refIsA ? wRef : wInc;
        const float3 wB = refIsA ? wInc : wRef;
        result.anchorAFeature[k] = float4(wA - bodyPosA, pt.w);
        result.anchorBSeparation[k] = float4(wB - bodyPosB, sep);
    }
    result.pairInfo.z = outCount;
}

void nwbPhysicsCollideBoxes(
    const NwbPhysicsGpuShapeProxy proxyA,
    const float3 originA,
    const float4 rotA,
    const float3 bodyPosA,
    const NwbPhysicsGpuShapeProxy proxyB,
    const float3 originB,
    const float4 rotB,
    const float3 bodyPosB,
    const float maxDist,
    inout NwbPhysicsGpuNarrowphaseManifold result
){
    const float3 cA = originA + nwbPhysicsQuatRotate(proxyA.localCenterRadius.xyz, rotA);
    const float4 qA = nwbPhysicsQuatNormalize(nwbPhysicsQuatMul(rotA, proxyA.localRotation));
    const float3 hA = proxyA.localExtentOrP1.xyz;
    const float rA = proxyA.localCenterRadius.w;

    const float3 cB = originB + nwbPhysicsQuatRotate(proxyB.localCenterRadius.xyz, rotB);
    const float4 qB = nwbPhysicsQuatNormalize(nwbPhysicsQuatMul(rotB, proxyB.localRotation));
    const float3 hB = proxyB.localExtentOrP1.xyz;
    const float rB = proxyB.localCenterRadius.w;

    float3 axA;
    float3 ayA;
    float3 azA;
    nwbPhysicsQuatToAxes(qA, axA, ayA, azA);
    float3 axB;
    float3 ayB;
    float3 azB;
    nwbPhysicsQuatToAxes(qB, axB, ayB, azB);

    const float3 axesA[3] = { axA, ayA, azA };
    const float3 axesB[3] = { axB, ayB, azB };
    const float halfA[3] = { hA.x, hA.y, hA.z };
    const float halfB[3] = { hB.x, hB.y, hB.z };

    const float3 dCenter = cB - cA;
    const float totalR = rA + rB;

    const float3 tVec = float3(dot(dCenter, axA), dot(dCenter, ayA), dot(dCenter, azA));
    const float r00 = dot(axA, axB);
    const float r01 = dot(axA, ayB);
    const float r02 = dot(axA, azB);
    const float r10 = dot(ayA, axB);
    const float r11 = dot(ayA, ayB);
    const float r12 = dot(ayA, azB);
    const float r20 = dot(azA, axB);
    const float r21 = dot(azA, ayB);
    const float r22 = dot(azA, azB);

    const float R[3][3] = {
        { r00, r01, r02 },
        { r10, r11, r12 },
        { r20, r21, r22 }
    };
    const float absR[3][3] = {
        { abs(r00), abs(r01), abs(r02) },
        { abs(r10), abs(r11), abs(r12) },
        { abs(r20), abs(r21), abs(r22) }
    };
    const float tArr[3] = { tVec.x, tVec.y, tVec.z };

    int bestAxis = -1;
    float bestSep = -1.0e20f;
    float bestSignedProj = 0.0f;
    float3 bestNormal = float3(0.0f, 1.0f, 0.0f);

    [unroll]
    for(int i = 0; i < 3; ++i){
        const float signedProj = tArr[i];
        const float rBProj = hB.x * absR[i][0] + hB.y * absR[i][1] + hB.z * absR[i][2];
        const float sep = abs(signedProj) - (halfA[i] + rBProj) - totalR;
        if(sep > maxDist)
            return;
        if(sep > bestSep){
            bestSep = sep;
            bestAxis = i;
            bestSignedProj = signedProj;
            bestNormal = (signedProj < 0.0f) ? -axesA[i] : axesA[i];
        }
    }

    [unroll]
    for(int j = 0; j < 3; ++j){
        const float signedProj = tVec.x * R[0][j] + tVec.y * R[1][j] + tVec.z * R[2][j];
        const float rAProj = hA.x * absR[0][j] + hA.y * absR[1][j] + hA.z * absR[2][j];
        const float sep = abs(signedProj) - (rAProj + halfB[j]) - totalR;
        if(sep > maxDist)
            return;
        if(sep > bestSep + 1.0e-4f){
            bestSep = sep;
            bestAxis = 3 + j;
            bestSignedProj = signedProj;
            bestNormal = (signedProj < 0.0f) ? -axesB[j] : axesB[j];
        }
    }

    [unroll]
    for(int i = 0; i < 3; ++i){
        const int i1 = (i + 1) % 3;
        const int i2 = (i + 2) % 3;
        [unroll]
        for(int j = 0; j < 3; ++j){
            const int j1 = (j + 1) % 3;
            const int j2 = (j + 2) % 3;
            const float c1 = R[i1][j];
            const float c2 = R[i2][j];
            const float lenSq = c1 * c1 + c2 * c2;
            if(lenSq < 1.0e-5f)
                continue;
            const float invLen = rsqrt(lenSq);
            const float signedProj = tArr[i2] * c1 - tArr[i1] * c2;
            const float rAU = halfA[i1] * absR[i2][j] + halfA[i2] * absR[i1][j];
            const float rBU = halfB[j1] * absR[i][j2] + halfB[j2] * absR[i][j1];
            const float sep = (abs(signedProj) - (rAU + rBU)) * invLen - totalR;
            if(sep > maxDist)
                return;
            if(sep > bestSep + 5.0e-4f){
                bestSep = sep;
                bestAxis = 6 + i * 3 + j;
                float3 n = (axesA[i2] * c1 - axesA[i1] * c2) * invLen;
                if(signedProj < 0.0f)
                    n = -n;
                bestNormal = n;
            }
        }
    }

    if(bestAxis >= 6){
        float3 pA = cA;
        float3 pB = cB;
        [unroll]
        for(int k = 0; k < 3; ++k){
            if(k != (bestAxis - 6) / 3)
                pA += axesA[k] * ((dot(axesA[k], bestNormal) >= 0.0f ? 1.0f : -1.0f) * halfA[k]);
            if(k != (bestAxis - 6) % 3)
                pB += axesB[k] * ((dot(axesB[k], bestNormal) <= 0.0f ? 1.0f : -1.0f) * halfB[k]);
        }
        const float3 eA = axesA[(bestAxis - 6) / 3];
        const float3 eB = axesB[(bestAxis - 6) % 3];
        const float hEA = halfA[(bestAxis - 6) / 3];
        const float hEB = halfB[(bestAxis - 6) % 3];
        float s = 0.0f;
        float t = 0.0f;
        float3 cEdgeA;
        float3 cEdgeB;
        nwbPhysicsClosestPointsSegments(pA - eA * hEA, pA + eA * hEA, pB - eB * hEB, pB + eB * hEB, s, t, cEdgeA, cEdgeB);
        result.pairInfo.z = 1u;
        result.normalPad = float4(bestNormal, 0.0f);
        result.anchorAFeature[0] = float4(cEdgeA + bestNormal * rA - bodyPosA, asfloat(uint(100 + bestAxis)));
        result.anchorBSeparation[0] = float4(cEdgeB - bestNormal * rB - bodyPosB, bestSep);
        return;
    }

    const bool refIsA = (bestAxis < 3);
    const float3 refNormal = refIsA ? bestNormal : -bestNormal;
    const float3 refCenter = refIsA ? cA : cB;
    const float3 incCenter = refIsA ? cB : cA;
    const float3 refAxes[3] = { refIsA ? axA : axB, refIsA ? ayA : ayB, refIsA ? azA : azB };
    const float3 incAxes[3] = { refIsA ? axB : axA, refIsA ? ayB : ayA, refIsA ? azB : azA };
    const float refH[3] = { refIsA ? hA.x : hB.x, refIsA ? hA.y : hB.y, refIsA ? hA.z : hB.z };
    const float incH[3] = { refIsA ? hB.x : hA.x, refIsA ? hB.y : hA.y, refIsA ? hB.z : hA.z };

    const int refFace = refIsA ? (bestAxis * 2 + (bestSignedProj < 0.0f ? 1 : 0)) : ((bestAxis - 3) * 2 + (bestSignedProj < 0.0f ? 0 : 1));

    int incFace = 0;
    float bestIncDot = 2.0f;
    [unroll]
    for(int k = 0; k < 3; ++k){
        const float d = dot(incAxes[k], refNormal);
        if(d < bestIncDot){
            bestIncDot = d;
            incFace = k * 2;
        }
        if(-d < bestIncDot){
            bestIncDot = -d;
            incFace = k * 2 + 1;
        }
    }

    const int refAxisIdx = refFace / 2;
    const int uIdx = (refAxisIdx + 1) % 3;
    const int vIdx = (refAxisIdx + 2) % 3;
    const float3 uAxis = refAxes[uIdx];
    const float3 vAxis = refAxes[vIdx];
    const float3 clipRefN = refAxes[refAxisIdx] * ((refFace % 2 == 0) ? 1.0f : -1.0f);
    const float3 refFaceCenter = refCenter + clipRefN * refH[refAxisIdx];

    float4 poly0[8];
    float4 poly1[8];
    float3 incVerts[4];
    nwbPhysicsGetBoxFace(incCenter, incAxes[0], incAxes[1], incAxes[2], float3(incH[0], incH[1], incH[2]), incFace, incVerts);
    [unroll]
    for(uint p = 0u; p < 4u; ++p){
        const float3 rel = incVerts[p] - refFaceCenter;
        poly0[p] = float4(dot(uAxis, rel), dot(vAxis, rel), dot(clipRefN, rel), asfloat(p));
    }
    uint polyCount = 4u;

    const float hu = refH[uIdx];
    const float hv = refH[vIdx];
    polyCount = nwbPhysicsClipBox2DPlaneAxis8(poly0, polyCount, 0u, 1.0f, hu, 1u, poly1);
    if(polyCount == 0u)
        return;
    polyCount = nwbPhysicsClipBox2DPlaneAxis8(poly1, polyCount, 0u, -1.0f, hu, 2u, poly0);
    if(polyCount == 0u)
        return;
    polyCount = nwbPhysicsClipBox2DPlaneAxis8(poly0, polyCount, 1u, 1.0f, hv, 3u, poly1);
    if(polyCount == 0u)
        return;
    polyCount = nwbPhysicsClipBox2DPlaneAxis8(poly1, polyCount, 1u, -1.0f, hv, 4u, poly0);
    if(polyCount == 0u)
        return;

    const uint boxFacePack = ((refIsA ? 1u : 0u) << 28u) | ((uint(refFace) & 0xfu) << 24u) | ((uint(incFace) & 0xfu) << 20u);
    uint candCount = 0u;
    for(uint p = 0u; p < polyCount && candCount < 8u; ++p){
        const float sep = poly0[p].z - totalR;
        if(sep <= maxDist){
            const uint fullFeat = boxFacePack | (asuint(poly0[p].w) & 0xfffffu);
            poly1[candCount] = float4(poly0[p].xyz, asfloat(fullFeat));
            ++candCount;
        }
    }

    result.normalPad = float4(bestNormal, 0.0f);
    nwbPhysicsReduceBoxManifoldTo4(uAxis, vAxis, clipRefN, refFaceCenter, poly1, candCount, refIsA, rA, rB, totalR, bodyPosA, bodyPosB, result);
}
#endif

#if NWB_PHYSICS_NP_PROFILE == 1
uint nwbPhysicsClipPolygonPlane(
    const float4 inPts[16],
    const uint inCount,
    const float3 planeNormal,
    const float planeOffset,
    const uint planeId,
    out float4 outPts[16]
){
    uint outCount = 0u;
    if(inCount == 0u){
        outPts[0] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return 0u;
    }

    float4 prevPt = inPts[inCount - 1u];
    uint prevFeat = asuint(prevPt.w);
    float prevDist = dot(planeNormal, prevPt.xyz) - planeOffset;

    for(uint i = 0u; i < inCount; ++i){
        const float4 curPt = inPts[i];
        const uint curFeat = asuint(curPt.w);
        const float curDist = dot(planeNormal, curPt.xyz) - planeOffset;

        if(curDist <= 0.0f){
            if(prevDist > 0.0f){
                const float denom = prevDist - curDist;
                const float t = abs(denom) > 1.0e-12f ? (prevDist / denom) : 0.0f;
                if(outCount < 16u){
                    const uint feat = 0x4000u | ((planeId & 0x3fu) << 8u) | (curFeat & 0xffu);
                    outPts[outCount] = float4(lerp(prevPt.xyz, curPt.xyz, clamp(t, 0.0f, 1.0f)), asfloat(feat));
                    ++outCount;
                }
            }
            if(outCount < 16u){
                outPts[outCount] = curPt;
                ++outCount;
            }
        }
        else if(prevDist <= 0.0f){
            const float denom = prevDist - curDist;
            const float t = abs(denom) > 1.0e-12f ? (prevDist / denom) : 0.0f;
            if(outCount < 16u){
                const uint feat = 0x8000u | ((planeId & 0x3fu) << 8u) | (prevFeat & 0xffu);
                outPts[outCount] = float4(lerp(prevPt.xyz, curPt.xyz, clamp(t, 0.0f, 1.0f)), asfloat(feat));
                ++outCount;
            }
        }

        prevPt = curPt;
        prevFeat = curFeat;
        prevDist = curDist;
    }
    return outCount;
}
#endif

#if NWB_PHYSICS_NP_PROFILE != 0

void nwbPhysicsSolveSimplex2(inout float3 sW[4], inout float3 sWA[4], inout float3 sWB[4], inout uint sIdxA[4], inout uint sIdxB[4], inout float sU[4], inout uint count){
    const float3 a = sW[0];
    const float3 b = sW[1];
    const float3 ab = b - a;
    const float lenSq = dot(ab, ab);
    if(lenSq <= 1.0e-12f){
        sU[0] = 1.0f;
        count = 1u;
        return;
    }
    const float t = clamp(-dot(a, ab) / lenSq, 0.0f, 1.0f);
    if(t <= 0.0f){
        sU[0] = 1.0f;
        count = 1u;
    }
    else if(t >= 1.0f){
        sW[0] = b; sWA[0] = sWA[1]; sWB[0] = sWB[1]; sIdxA[0] = sIdxA[1]; sIdxB[0] = sIdxB[1];
        sU[0] = 1.0f;
        count = 1u;
    }
    else{
        sU[0] = 1.0f - t;
        sU[1] = t;
        count = 2u;
    }
}

void nwbPhysicsSolveSimplex3(inout float3 sW[4], inout float3 sWA[4], inout float3 sWB[4], inout uint sIdxA[4], inout uint sIdxB[4], inout float sU[4], inout uint count){
    const float3 a = sW[0];
    const float3 b = sW[1];
    const float3 c = sW[2];
    const float d1 = -dot(a, b - a);
    const float d2 = -dot(a, c - a);
    const float d3 = -dot(b, b - a);
    const float d4 = -dot(b, c - a);
    const float d5 = -dot(c, b - a);
    const float d6 = -dot(c, c - a);
    const float dAB_B = d1;
    const float dAB_A = -d3;
    const float dAC_C = d2;
    const float dAC_A = -d6;
    const float dBC_C = d4 - d3;
    const float dBC_B = d5 - d6;
    const float dABC_C = d1 * d4 - d3 * d2;
    const float dABC_B = d5 * d2 - d1 * d6;
    const float dABC_A = d3 * d6 - d5 * d4;

    if(dAB_B <= 0.0f && dAC_C <= 0.0f){
        sU[0] = 1.0f;
        count = 1u;
        return;
    }
    if(dAB_A <= 0.0f && dBC_C <= 0.0f){
        sW[0] = b; sWA[0] = sWA[1]; sWB[0] = sWB[1]; sIdxA[0] = sIdxA[1]; sIdxB[0] = sIdxB[1];
        sU[0] = 1.0f;
        count = 1u;
        return;
    }
    if(dAC_A <= 0.0f && dBC_B <= 0.0f){
        sW[0] = c; sWA[0] = sWA[2]; sWB[0] = sWB[2]; sIdxA[0] = sIdxA[2]; sIdxB[0] = sIdxB[2];
        sU[0] = 1.0f;
        count = 1u;
        return;
    }
    if(dABC_C <= 0.0f && dAB_A >= 0.0f && dAB_B >= 0.0f && (dAB_A + dAB_B) > 1.0e-12f){
        const float inv = 1.0f / (dAB_A + dAB_B);
        sU[0] = dAB_A * inv;
        sU[1] = dAB_B * inv;
        count = 2u;
        return;
    }
    if(dABC_B <= 0.0f && dAC_A >= 0.0f && dAC_C >= 0.0f && (dAC_A + dAC_C) > 1.0e-12f){
        const float inv = 1.0f / (dAC_A + dAC_C);
        sW[1] = c; sWA[1] = sWA[2]; sWB[1] = sWB[2]; sIdxA[1] = sIdxA[2]; sIdxB[1] = sIdxB[2];
        sU[0] = dAC_A * inv;
        sU[1] = dAC_C * inv;
        count = 2u;
        return;
    }
    if(dABC_A <= 0.0f && dBC_B >= 0.0f && dBC_C >= 0.0f && (dBC_B + dBC_C) > 1.0e-12f){
        const float inv = 1.0f / (dBC_B + dBC_C);
        sW[0] = b; sWA[0] = sWA[1]; sWB[0] = sWB[1]; sIdxA[0] = sIdxA[1]; sIdxB[0] = sIdxB[1];
        sW[1] = c; sWA[1] = sWA[2]; sWB[1] = sWB[2]; sIdxA[1] = sIdxA[2]; sIdxB[1] = sIdxB[2];
        sU[0] = dBC_B * inv;
        sU[1] = dBC_C * inv;
        count = 2u;
        return;
    }
    const float sum = dABC_A + dABC_B + dABC_C;
    if(sum <= 1.0e-12f){
        nwbPhysicsSolveSimplex2(sW, sWA, sWB, sIdxA, sIdxB, sU, count);
        return;
    }
    const float inv = 1.0f / sum;
    sU[0] = dABC_A * inv;
    sU[1] = dABC_B * inv;
    sU[2] = dABC_C * inv;
    count = 3u;
}

void nwbPhysicsSolveSimplex4(
    inout float3 sW[4],
    inout float3 sWA[4],
    inout float3 sWB[4],
    inout uint sIdxA[4],
    inout uint sIdxB[4],
    inout float sU[4],
    inout uint count
){
    const float3 a = sW[0];
    const float3 b = sW[1];
    const float3 c = sW[2];
    const float3 d = sW[3];
    const float3 wAA = sWA[0];
    const float3 wAB = sWA[1];
    const float3 wAC = sWA[2];
    const float3 wAD = sWA[3];
    const float3 wBA = sWB[0];
    const float3 wBB = sWB[1];
    const float3 wBC = sWB[2];
    const float3 wBD = sWB[3];
    const uint iAA = sIdxA[0];
    const uint iAB = sIdxA[1];
    const uint iAC = sIdxA[2];
    const uint iAD = sIdxA[3];
    const uint iBA = sIdxB[0];
    const uint iBB = sIdxB[1];
    const uint iBC = sIdxB[2];
    const uint iBD = sIdxB[3];

    const float signDet = dot(b - a, cross(c - a, d - a));
    const float sign = (signDet >= 0.0f) ? 1.0f : -1.0f;

    const float dABCD_D = sign * dot(a, cross(b, c));
    const float dABCD_C = sign * dot(d, cross(b, a));
    const float dABCD_B = sign * dot(c, cross(d, a));
    const float dABCD_A = sign * dot(b, cross(d, c));

    const float testABC = dot(d - a, cross(b - a, c - a));
    const float oriABC = dot(-a, cross(b - a, c - a));
    const float testABD = dot(c - a, cross(b - a, d - a));
    const float oriABD = dot(-a, cross(b - a, d - a));
    const float testACD = dot(b - a, cross(c - a, d - a));
    const float oriACD = dot(-a, cross(c - a, d - a));
    const float testBCD = dot(a - b, cross(c - b, d - b));
    const float oriBCD = dot(-b, cross(c - b, d - b));

    bool outABC = (oriABC * testABC) < 0.0f;
    bool outABD = (oriABD * testABD) < 0.0f;
    bool outACD = (oriACD * testACD) < 0.0f;
    bool outBCD = (oriBCD * testBCD) < 0.0f;

    if(abs(signDet) <= 1.0e-10f){
        outABC = true;
        outABD = true;
        outACD = true;
        outBCD = true;
    }

    if(!outABC && !outABD && !outACD && !outBCD){
        const float total = dABCD_A + dABCD_B + dABCD_C + dABCD_D;
        if(abs(total) > 1.0e-12f){
            const float inv = 1.0f / total;
            sU[0] = dABCD_A * inv;
            sU[1] = dABCD_B * inv;
            sU[2] = dABCD_C * inv;
            sU[3] = dABCD_D * inv;
        }
        else{
            sU[0] = 0.25f;
            sU[1] = 0.25f;
            sU[2] = 0.25f;
            sU[3] = 0.25f;
        }
        count = 4u;
        return;
    }

    float3 bestW[4];
    float3 bestWA[4];
    float3 bestWB[4];
    uint bestIdxA[4];
    uint bestIdxB[4];
    float bestU[4];
    for(uint k = 0u; k < 4u; ++k){
        bestW[k] = float3(0.0f, 0.0f, 0.0f);
        bestWA[k] = float3(0.0f, 0.0f, 0.0f);
        bestWB[k] = float3(0.0f, 0.0f, 0.0f);
        bestIdxA[k] = 0u;
        bestIdxB[k] = 0u;
        bestU[k] = 0.0f;
    }
    bestW[0] = d; bestWA[0] = wAD; bestWB[0] = wBD; bestIdxA[0] = iAD; bestIdxB[0] = iBD; bestU[0] = 1.0f;
    uint bestCount = 1u;
    float bestDistSq = dot(d, d);

    float3 cW[4];
    float3 cWA[4];
    float3 cWB[4];
    uint cIdxA[4];
    uint cIdxB[4];
    float cU[4];
    const bool faceActive[4] = { outABD, outACD, outBCD, abs(signDet) <= 1.0e-10f && outABC };
    for(uint face = 0u; face < 4u; ++face){
        if(!faceActive[face])
            continue;
        if(face == 0u){
            cW[0] = a; cW[1] = b; cW[2] = d;
            cWA[0] = wAA; cWA[1] = wAB; cWA[2] = wAD;
            cWB[0] = wBA; cWB[1] = wBB; cWB[2] = wBD;
            cIdxA[0] = iAA; cIdxA[1] = iAB; cIdxA[2] = iAD;
            cIdxB[0] = iBA; cIdxB[1] = iBB; cIdxB[2] = iBD;
        }
        else if(face == 1u){
            cW[0] = a; cW[1] = c; cW[2] = d;
            cWA[0] = wAA; cWA[1] = wAC; cWA[2] = wAD;
            cWB[0] = wBA; cWB[1] = wBC; cWB[2] = wBD;
            cIdxA[0] = iAA; cIdxA[1] = iAC; cIdxA[2] = iAD;
            cIdxB[0] = iBA; cIdxB[1] = iBC; cIdxB[2] = iBD;
        }
        else if(face == 2u){
            cW[0] = b; cW[1] = c; cW[2] = d;
            cWA[0] = wAB; cWA[1] = wAC; cWA[2] = wAD;
            cWB[0] = wBB; cWB[1] = wBC; cWB[2] = wBD;
            cIdxA[0] = iAB; cIdxA[1] = iAC; cIdxA[2] = iAD;
            cIdxB[0] = iBB; cIdxB[1] = iBC; cIdxB[2] = iBD;
        }
        else{
            cW[0] = a; cW[1] = b; cW[2] = c;
            cWA[0] = wAA; cWA[1] = wAB; cWA[2] = wAC;
            cWB[0] = wBA; cWB[1] = wBB; cWB[2] = wBC;
            cIdxA[0] = iAA; cIdxA[1] = iAB; cIdxA[2] = iAC;
            cIdxB[0] = iBA; cIdxB[1] = iBB; cIdxB[2] = iBC;
        }
        cW[3] = float3(0.0f, 0.0f, 0.0f);
        cWA[3] = float3(0.0f, 0.0f, 0.0f);
        cWB[3] = float3(0.0f, 0.0f, 0.0f);
        cIdxA[3] = 0u;
        cIdxB[3] = 0u;
        cU[0] = 0.0f; cU[1] = 0.0f; cU[2] = 0.0f; cU[3] = 0.0f;
        uint cCount = 3u;
        nwbPhysicsSolveSimplex3(cW, cWA, cWB, cIdxA, cIdxB, cU, cCount);
        float3 p = float3(0.0f, 0.0f, 0.0f);
        for(uint k = 0u; k < cCount; ++k)
            p += cW[k] * cU[k];
        const float dSq = dot(p, p);
        if(dSq < bestDistSq){
            bestDistSq = dSq;
            bestCount = cCount;
            for(uint k = 0u; k < 4u; ++k){
                bestW[k] = cW[k]; bestWA[k] = cWA[k]; bestWB[k] = cWB[k];
                bestIdxA[k] = cIdxA[k]; bestIdxB[k] = cIdxB[k]; bestU[k] = cU[k];
            }
        }
    }

    count = bestCount;
    for(uint k = 0u; k < 4u; ++k){
        sW[k] = bestW[k]; sWA[k] = bestWA[k]; sWB[k] = bestWB[k];
        sIdxA[k] = bestIdxA[k]; sIdxB[k] = bestIdxB[k]; sU[k] = bestU[k];
    }
}

float3 nwbPhysicsSupportWorld(
    const NwbPhysicsGpuShapeProxy proxy,
    const float3 origin,
    const float4 rot,
    const uint hullVertexSlot,
    const float3 worldDir,
    out uint outIdx
){
    const uint shapeType = proxy.bodyIndexTypeShape.y;
    const bool isBox = (shapeType == NWB_PHYSICS_SHAPE_TYPE_HULL) && ((proxy.bodyIndexTypeShape.w & NWB_PHYSICS_SHAPE_FLAG_IS_BOX) != 0u);

    if(shapeType == NWB_PHYSICS_SHAPE_TYPE_SPHERE){
        outIdx = 0u;
        return origin + nwbPhysicsQuatRotate(proxy.localCenterRadius.xyz, rot);
    }
    if(shapeType == NWB_PHYSICS_SHAPE_TYPE_CAPSULE){
        const float3 p0 = origin + nwbPhysicsQuatRotate(proxy.localCenterRadius.xyz, rot);
        const float3 p1 = origin + nwbPhysicsQuatRotate(proxy.localExtentOrP1.xyz, rot);
        if(dot(p0, worldDir) >= dot(p1, worldDir)){
            outIdx = 0u;
            return p0;
        }
        outIdx = 1u;
        return p1;
    }
    if(isBox){
        const float4 qBox = nwbPhysicsQuatNormalize(nwbPhysicsQuatMul(rot, proxy.localRotation));
        const float3 cBox = origin + nwbPhysicsQuatRotate(proxy.localCenterRadius.xyz, rot);
        const float3 ld = nwbPhysicsQuatInvRotate(worldDir, qBox);
        const float3 h = proxy.localExtentOrP1.xyz;
        const float3 s = float3(ld.x >= 0.0f ? h.x : -h.x, ld.y >= 0.0f ? h.y : -h.y, ld.z >= 0.0f ? h.z : -h.z);
        outIdx = (ld.x >= 0.0f ? 1u : 0u) | (ld.y >= 0.0f ? 2u : 0u) | (ld.z >= 0.0f ? 4u : 0u);
        return cBox + nwbPhysicsQuatRotate(s, qBox);
    }

    const uint vOffset = proxy.filterData.w;
    const uint metaPack = asuint(proxy.aabbMaxPad.w);
    const uint vCount = min(metaPack & 0xffu, NWB_PHYSICS_MAX_HULL_VERTICES);
    if(vCount == 0u){
        outIdx = 0u;
        return origin;
    }
    const float3 localDir = nwbPhysicsQuatInvRotate(worldDir, rot);
    uint bestIdx = 0u;
    float3 bestLocal = g_NwbPhysicsFloat4Buffers[hullVertexSlot][vOffset].xyz;
    float bestDot = dot(bestLocal, localDir);
    for(uint i = 1u; i < vCount; ++i){
        const float3 v = g_NwbPhysicsFloat4Buffers[hullVertexSlot][vOffset + i].xyz;
        const float d = dot(v, localDir);
        if(d > bestDot){
            bestDot = d;
            bestIdx = i;
            bestLocal = v;
        }
    }
    outIdx = bestIdx;
    return origin + nwbPhysicsQuatRotate(bestLocal, rot);
}

#if NWB_PHYSICS_NP_PROFILE == 1
void nwbPhysicsReduceManifoldTo4(
    const float3 clipRefN,
    const float3 candWRef[16],
    const float3 candWInc[16],
    const float3 candPos[16],
    const float candSep[16],
    const uint candFeat[16],
    const uint candCount,
    const bool refIsA,
    const float3 bodyPosA,
    const float3 bodyPosB,
    inout NwbPhysicsGpuNarrowphaseManifold result
){
    uint outIndices[4] = { 0u, 1u, 2u, 3u };
    uint outCount = candCount;
    if(candCount > NWB_PHYSICS_MAX_MANIFOLD_POINTS){
        uint idx0 = 0u;
        for(uint i = 1u; i < candCount; ++i){
            if(candSep[i] < candSep[idx0])
                idx0 = i;
        }

        uint idx1 = 0u;
        float bestDistSq1 = -1.0f;
        const float3 p0 = candPos[idx0];
        for(uint i = 0u; i < candCount; ++i){
            const float3 diff = candPos[i] - p0;
            const float dSq = dot(diff, diff);
            if(dSq > bestDistSq1){
                bestDistSq1 = dSq;
                idx1 = i;
            }
        }

        uint idx2 = 0u;
        float bestArea2 = -1.0f;
        const float3 p1 = candPos[idx1];
        const float3 e01 = p1 - p0;
        for(uint i = 0u; i < candCount; ++i){
            if(i == idx0 || i == idx1)
                continue;
            const float area = abs(dot(cross(e01, candPos[i] - p0), clipRefN));
            if(area > bestArea2){
                bestArea2 = area;
                idx2 = i;
            }
        }

        uint idx3 = 0u;
        float bestOutsideArea = -1.0f;
        uint fallbackIdx3 = 0u;
        float bestFallbackArea = -1.0f;
        const float3 p2 = candPos[idx2];
        const float baseSign = dot(cross(e01, p2 - p0), clipRefN);
        for(uint i = 0u; i < candCount; ++i){
            if(i == idx0 || i == idx1 || i == idx2)
                continue;
            const float3 pi = candPos[i];
            const float a0 = dot(cross(p1 - p0, pi - p0), clipRefN);
            const float a1 = dot(cross(p2 - p1, pi - p1), clipRefN);
            const float a2 = dot(cross(p0 - p2, pi - p2), clipRefN);
            const float maxAbs = max(abs(a0), max(abs(a1), abs(a2)));
            if(maxAbs > bestFallbackArea){
                bestFallbackArea = maxAbs;
                fallbackIdx3 = i;
            }
            float outsideArea = -1.0f;
            if(baseSign >= 0.0f){
                if(a0 < 0.0f && -a0 > outsideArea) outsideArea = -a0;
                if(a1 < 0.0f && -a1 > outsideArea) outsideArea = -a1;
                if(a2 < 0.0f && -a2 > outsideArea) outsideArea = -a2;
            }
            else{
                if(a0 > 0.0f && a0 > outsideArea) outsideArea = a0;
                if(a1 > 0.0f && a1 > outsideArea) outsideArea = a1;
                if(a2 > 0.0f && a2 > outsideArea) outsideArea = a2;
            }
            if(outsideArea > bestOutsideArea){
                bestOutsideArea = outsideArea;
                idx3 = i;
            }
        }
        if(bestOutsideArea < 0.0f)
            idx3 = fallbackIdx3;

        outIndices[0] = idx0;
        outIndices[1] = idx1;
        outIndices[2] = idx2;
        outIndices[3] = idx3;
        outCount = NWB_PHYSICS_MAX_MANIFOLD_POINTS;
    }

    for(uint k = 0u; k < outCount; ++k){
        const uint ci = outIndices[k];
        const float3 wA = refIsA ? candWRef[ci] : candWInc[ci];
        const float3 wB = refIsA ? candWInc[ci] : candWRef[ci];
        result.anchorAFeature[k] = float4(wA - bodyPosA, asfloat(candFeat[ci]));
        result.anchorBSeparation[k] = float4(wB - bodyPosB, candSep[ci]);
    }
    result.pairInfo.z = outCount;
}

float3 nwbPhysicsClosestPointOnTriangle(const float3 p, const float3 a, const float3 b, const float3 c){
    const float3 ab = b - a;
    const float3 ac = c - a;
    const float3 ap = p - a;
    const float d1 = dot(ab, ap);
    const float d2 = dot(ac, ap);
    if(d1 <= 0.0f && d2 <= 0.0f)
        return a;

    const float3 bp = p - b;
    const float d3 = dot(ab, bp);
    const float d4 = dot(ac, bp);
    if(d3 >= 0.0f && d4 <= d3)
        return b;

    const float vc = d1 * d4 - d3 * d2;
    if(vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f)
        return a + ab * (d1 / (d1 - d3));

    const float3 cp = p - c;
    const float d5 = dot(ab, cp);
    const float d6 = dot(ac, cp);
    if(d6 >= 0.0f && d5 <= d6)
        return c;

    const float vb = d5 * d2 - d1 * d6;
    if(vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f)
        return a + ac * (d2 / (d2 - d6));

    const float va = d3 * d6 - d5 * d4;
    if(va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f)
        return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));

    const float denom = 1.0f / (va + vb + vc);
    return a + ab * (vb * denom) + ac * (vc * denom);
}
#endif

#if NWB_PHYSICS_NP_PROFILE == 1
void nwbPhysicsPushCandPoint(
    const float3 wA,
    const float3 wB,
    const float sep,
    const uint feat,
    const float3 bodyPosA,
    const float3 bodyPosB,
    inout NwbPhysicsGpuNarrowphaseManifold result,
    inout float seps[4]
){
    const uint cnt = result.pairInfo.z;
    if(cnt < NWB_PHYSICS_MAX_MANIFOLD_POINTS){
        result.anchorAFeature[cnt] = float4(wA - bodyPosA, asfloat(feat));
        result.anchorBSeparation[cnt] = float4(wB - bodyPosB, sep);
        seps[cnt] = sep;
        result.pairInfo.z = cnt + 1u;
    }
    else{
        uint worstIdx = 0u;
        for(uint i = 1u; i < 4u; ++i){
            if(seps[i] > seps[worstIdx])
                worstIdx = i;
        }
        if(sep < seps[worstIdx]){
            result.anchorAFeature[worstIdx] = float4(wA - bodyPosA, asfloat(feat));
            result.anchorBSeparation[worstIdx] = float4(wB - bodyPosB, sep);
            seps[worstIdx] = sep;
        }
    }
}
#endif

void nwbPhysicsSupportProxyOrTri(
    const NwbPhysicsGpuShapeProxy proxy,
    const float3 origin,
    const float4 rot,
    const uint hullVertexSlot,
    const bool useTri,
    const float3 tv0,
    const float3 tv1,
    const float3 tv2,
    const float3 worldDir,
    out uint outIdx,
    out float3 outPt
){
    if(useTri){
        const float d0 = dot(tv0, worldDir);
        const float d1 = dot(tv1, worldDir);
        const float d2 = dot(tv2, worldDir);
        if(d0 >= d1 && d0 >= d2){
            outIdx = 0u;
            outPt = tv0;
        }
        else if(d1 >= d2){
            outIdx = 1u;
            outPt = tv1;
        }
        else{
            outIdx = 2u;
            outPt = tv2;
        }
        return;
    }
    outPt = nwbPhysicsSupportWorld(proxy, origin, rot, hullVertexSlot, worldDir, outIdx);
}

bool nwbPhysicsRunGjkDistanceEx(
    const NwbPhysicsGpuShapeProxy proxyA,
    const float3 originA,
    const float4 rotA,
    const bool triA,
    const float3 av0,
    const float3 av1,
    const float3 av2,
    const NwbPhysicsGpuShapeProxy proxyB,
    const float3 originB,
    const float4 rotB,
    const bool triB,
    const float3 bv0,
    const float3 bv1,
    const float3 bv2,
    const uint hullVertexSlot,
    out float3 outPtA,
    out float3 outPtB,
    out float outDist,
    out bool outHitDeep
){
    float3 sW[4];
    float3 sWA[4];
    float3 sWB[4];
    uint sIdxA[4];
    uint sIdxB[4];
    float sU[4];
    for(uint k = 0u; k < 4u; ++k){
        sW[k] = float3(0.0f, 0.0f, 0.0f);
        sWA[k] = float3(0.0f, 0.0f, 0.0f);
        sWB[k] = float3(0.0f, 0.0f, 0.0f);
        sIdxA[k] = 0u;
        sIdxB[k] = 0u;
        sU[k] = 0.0f;
    }

    uint sCount = 1u;
    nwbPhysicsSupportProxyOrTri(proxyA, originA, rotA, hullVertexSlot, triA, av0, av1, av2, float3(1.0f, 0.0f, 0.0f), sIdxA[0], sWA[0]);
    nwbPhysicsSupportProxyOrTri(proxyB, originB, rotB, hullVertexSlot, triB, bv0, bv1, bv2, float3(-1.0f, 0.0f, 0.0f), sIdxB[0], sWB[0]);
    sW[0] = sWB[0] - sWA[0];
    sU[0] = 1.0f;

    outHitDeep = false;
    for(uint iter = 0u; iter < 25u; ++iter){
        if(sCount == 2u)
            nwbPhysicsSolveSimplex2(sW, sWA, sWB, sIdxA, sIdxB, sU, sCount);
        else if(sCount == 3u)
            nwbPhysicsSolveSimplex3(sW, sWA, sWB, sIdxA, sIdxB, sU, sCount);
        else if(sCount == 4u)
            nwbPhysicsSolveSimplex4(sW, sWA, sWB, sIdxA, sIdxB, sU, sCount);

        if(sCount == 4u){
            outHitDeep = true;
            break;
        }
        if(iter == 24u)
            break;

        float3 pClosest = float3(0.0f, 0.0f, 0.0f);
        for(uint k = 0u; k < sCount; ++k)
            pClosest += sW[k] * sU[k];

        const float pLenSq = dot(pClosest, pClosest);
        if(pLenSq <= 1.0e-10f){
            outHitDeep = true;
            break;
        }

        const float3 d = -pClosest;
        uint idxA = 0u;
        uint idxB = 0u;
        float3 wA = float3(0.0f, 0.0f, 0.0f);
        float3 wB = float3(0.0f, 0.0f, 0.0f);
        nwbPhysicsSupportProxyOrTri(proxyA, originA, rotA, hullVertexSlot, triA, av0, av1, av2, -d, idxA, wA);
        nwbPhysicsSupportProxyOrTri(proxyB, originB, rotB, hullVertexSlot, triB, bv0, bv1, bv2, d, idxB, wB);

        bool dup = false;
        for(uint k = 0u; k < sCount; ++k){
            if(sIdxA[k] == idxA && sIdxB[k] == idxB){
                dup = true;
                break;
            }
        }
        if(dup)
            break;

        const float3 wNew = wB - wA;
        if(dot(pClosest - wNew, pClosest) <= 1.0e-5f * pLenSq)
            break;

        sIdxA[sCount] = idxA;
        sIdxB[sCount] = idxB;
        sWA[sCount] = wA;
        sWB[sCount] = wB;
        sW[sCount] = wNew;
        sU[sCount] = 0.0f;
        ++sCount;
    }

    float3 pA = float3(0.0f, 0.0f, 0.0f);
    float3 pB = float3(0.0f, 0.0f, 0.0f);
    for(uint k = 0u; k < sCount; ++k){
        pA += sWA[k] * sU[k];
        pB += sWB[k] * sU[k];
    }
    outPtA = pA;
    outPtB = pB;
    outDist = sqrt(max(0.0f, dot(pB - pA, pB - pA)));
    return true;
}

bool nwbPhysicsRunGjkDistance(
    const NwbPhysicsGpuShapeProxy proxyA,
    const float3 originA,
    const float4 rotA,
    const NwbPhysicsGpuShapeProxy proxyB,
    const float3 originB,
    const float4 rotB,
    const bool triB,
    const float3 tv0,
    const float3 tv1,
    const float3 tv2,
    const uint hullVertexSlot,
    out float3 outPtA,
    out float3 outPtB,
    out float outDist,
    out bool outHitDeep
){
    return nwbPhysicsRunGjkDistanceEx(
        proxyA,
        originA,
        rotA,
        false,
        float3(0.0f, 0.0f, 0.0f),
        float3(0.0f, 0.0f, 0.0f),
        float3(0.0f, 0.0f, 0.0f),
        proxyB,
        originB,
        rotB,
        triB,
        tv0,
        tv1,
        tv2,
        hullVertexSlot,
        outPtA,
        outPtB,
        outDist,
        outHitDeep
    );
}

#if NWB_PHYSICS_NP_PROFILE == 1
uint nwbPhysicsHullVertexCount(const NwbPhysicsGpuShapeProxy proxy){
    if((proxy.bodyIndexTypeShape.w & NWB_PHYSICS_SHAPE_FLAG_IS_BOX) != 0u)
        return 8u;
    return asuint(proxy.aabbMaxPad.w) & 0xffu;
}

uint nwbPhysicsHullFaceCount(const NwbPhysicsGpuShapeProxy proxy){
    if((proxy.bodyIndexTypeShape.w & NWB_PHYSICS_SHAPE_FLAG_IS_BOX) != 0u)
        return 6u;
    return (asuint(proxy.aabbMaxPad.w) >> 8u) & 0xffu;
}

uint nwbPhysicsHullEdgeCount(const NwbPhysicsGpuShapeProxy proxy){
    if((proxy.bodyIndexTypeShape.w & NWB_PHYSICS_SHAPE_FLAG_IS_BOX) != 0u)
        return 12u;
    return asuint(proxy.aabbMaxPad.w) >> 16u;
}

float3 nwbPhysicsHullLocalVertex(const NwbPhysicsGpuShapeProxy proxy, const uint hullVertexSlot, const uint vIdx){
    if((proxy.bodyIndexTypeShape.w & NWB_PHYSICS_SHAPE_FLAG_IS_BOX) != 0u){
        const float3 h = proxy.localExtentOrP1.xyz;
        const float3 corner = float3(
            (vIdx & 1u) != 0u ? h.x : -h.x,
            (vIdx & 2u) != 0u ? h.y : -h.y,
            (vIdx & 4u) != 0u ? h.z : -h.z
        );
        return proxy.localCenterRadius.xyz + nwbPhysicsQuatRotate(corner, proxy.localRotation);
    }
    const uint vOffset = proxy.filterData.w;
    return g_NwbPhysicsFloat4Buffers[hullVertexSlot][vOffset + vIdx].xyz;
}

uint nwbPhysicsReadHullPackedUint(const uint hullVertexSlot, const uint vecBase, const uint uintIdx){
    const float4 packedVec = g_NwbPhysicsFloat4Buffers[hullVertexSlot][vecBase + (uintIdx >> 2u)];
    const uint lane = uintIdx & 3u;
    if(lane == 0u) return asuint(packedVec.x);
    if(lane == 1u) return asuint(packedVec.y);
    if(lane == 2u) return asuint(packedVec.z);
    return asuint(packedVec.w);
}

uint nwbPhysicsHullEdgePacked(const NwbPhysicsGpuShapeProxy proxy, const uint hullVertexSlot, const uint edgeIdx){
    if((proxy.bodyIndexTypeShape.w & NWB_PHYSICS_SHAPE_FLAG_IS_BOX) != 0u){
        const uint kBoxEdges[12] = {
            (0u | (1u << 8u) | (3u << 16u) | (5u << 24u)),
            (2u | (3u << 8u) | (2u << 16u) | (5u << 24u)),
            (4u | (5u << 8u) | (3u << 16u) | (4u << 24u)),
            (6u | (7u << 8u) | (2u << 16u) | (4u << 24u)),
            (0u | (2u << 8u) | (1u << 16u) | (5u << 24u)),
            (1u | (3u << 8u) | (0u << 16u) | (5u << 24u)),
            (4u | (6u << 8u) | (1u << 16u) | (4u << 24u)),
            (5u | (7u << 8u) | (0u << 16u) | (4u << 24u)),
            (0u | (4u << 8u) | (1u << 16u) | (3u << 24u)),
            (1u | (5u << 8u) | (0u << 16u) | (3u << 24u)),
            (2u | (6u << 8u) | (1u << 16u) | (2u << 24u)),
            (3u | (7u << 8u) | (0u << 16u) | (2u << 24u))
        };
        return kBoxEdges[min(edgeIdx, 11u)];
    }
    const uint vOffset = proxy.filterData.w;
    const uint vCount = nwbPhysicsHullVertexCount(proxy);
    const uint fCount = nwbPhysicsHullFaceCount(proxy);
    return nwbPhysicsReadHullPackedUint(hullVertexSlot, vOffset + vCount + fCount, edgeIdx);
}

bool nwbPhysicsIsMinkowskiFace(const float3 a, const float3 b, const float3 edgeBA, const float3 c, const float3 d, const float3 edgeDC){
    const float cba = dot(c, edgeBA);
    const float dba = dot(d, edgeBA);
    const float adc = dot(a, edgeDC);
    const float bdc = dot(b, edgeDC);
    return (cba * dba < 0.0f) && (adc * bdc < 0.0f) && (cba * bdc > 0.0f);
}

uint nwbPhysicsMakeFeatureId(const uint o1, const uint i1, const uint o2, const uint i2){
    return ((o1 & 0xffu) << 24u) | ((i1 & 0xffu) << 16u) | ((o2 & 0xffu) << 8u) | (i2 & 0xffu);
}

bool nwbPhysicsIsPointOnEdge(const float3 p, const float3 a, const float3 b){
    const float3 ab = b - a;
    const float abLenSq = dot(ab, ab);
    if(abLenSq <= 1.0e-12f)
        return false;
    const float t = dot(p - a, ab) / abLenSq;
    if(t <= 0.0f || t >= 1.0f)
        return false;
    const float3 proj = a + ab * t;
    const float3 d = p - proj;
    return dot(d, d) <= 1.0e-8f;
}

bool nwbPhysicsSmoothMeshNormal(
    const float3 triN,
    const uint edgeFlags,
    const float3 localPt,
    const float3 v0,
    const float3 v1,
    const float3 v2,
    const float3 rawMeshToOtherN,
    out float3 outSmoothedN
){
    outSmoothedN = rawMeshToOtherN;
    if(edgeFlags == 0u)
        return false;
    if(dot(rawMeshToOtherN, triN) <= 0.0f)
        return false;

    const float vertEpsSq = 1.0e-10f;
    const float3 dv0 = localPt - v0;
    const float3 dv1 = localPt - v1;
    const float3 dv2 = localPt - v2;
    bool smoothFeature = false;
    if(dot(dv0, dv0) <= vertEpsSq)
        smoothFeature = (edgeFlags & 8u) != 0u;
    else if(dot(dv1, dv1) <= vertEpsSq)
        smoothFeature = (edgeFlags & 16u) != 0u;
    else if(dot(dv2, dv2) <= vertEpsSq)
        smoothFeature = (edgeFlags & 32u) != 0u;
    else if((edgeFlags & 1u) != 0u && nwbPhysicsIsPointOnEdge(localPt, v0, v1))
        smoothFeature = true;
    else if((edgeFlags & 2u) != 0u && nwbPhysicsIsPointOnEdge(localPt, v1, v2))
        smoothFeature = true;
    else if((edgeFlags & 4u) != 0u && nwbPhysicsIsPointOnEdge(localPt, v2, v0))
        smoothFeature = true;

    if(!smoothFeature)
        return false;

    outSmoothedN = triN;
    return true;
}

float4 nwbPhysicsHullLocalFacePlane(const NwbPhysicsGpuShapeProxy proxy, const uint hullVertexSlot, const uint faceIdx){
    if((proxy.bodyIndexTypeShape.w & NWB_PHYSICS_SHAPE_FLAG_IS_BOX) != 0u){
        const float3 h = proxy.localExtentOrP1.xyz;
        float3 localN = float3(1.0f, 0.0f, 0.0f);
        float off = h.x;
        if(faceIdx == 0u){ localN = float3(1.0f, 0.0f, 0.0f); off = h.x; }
        else if(faceIdx == 1u){ localN = float3(-1.0f, 0.0f, 0.0f); off = h.x; }
        else if(faceIdx == 2u){ localN = float3(0.0f, 1.0f, 0.0f); off = h.y; }
        else if(faceIdx == 3u){ localN = float3(0.0f, -1.0f, 0.0f); off = h.y; }
        else if(faceIdx == 4u){ localN = float3(0.0f, 0.0f, 1.0f); off = h.z; }
        else{ localN = float3(0.0f, 0.0f, -1.0f); off = h.z; }
        const float3 rotN = nwbPhysicsQuatRotate(localN, proxy.localRotation);
        return float4(rotN, off + dot(rotN, proxy.localCenterRadius.xyz));
    }
    const uint vOffset = proxy.filterData.w;
    const uint vCount = nwbPhysicsHullVertexCount(proxy);
    return g_NwbPhysicsFloat4Buffers[hullVertexSlot][vOffset + vCount + faceIdx];
}

uint nwbPhysicsGatherHullWorldFacePoly(
    const NwbPhysicsGpuShapeProxy proxy,
    const float3 origin,
    const float4 rot,
    const uint hullVertexSlot,
    const uint faceIdx,
    out float4 outVerts[16],
    out float3 outWorldNormal,
    out float outWorldOffset
){
    if((proxy.bodyIndexTypeShape.w & NWB_PHYSICS_SHAPE_FLAG_IS_BOX) != 0u){
        const float3 cBox = origin + nwbPhysicsQuatRotate(proxy.localCenterRadius.xyz, rot);
        const float4 qBox = nwbPhysicsQuatNormalize(nwbPhysicsQuatMul(rot, proxy.localRotation));
        float3 ax;
        float3 ay;
        float3 az;
        nwbPhysicsQuatToAxes(qBox, ax, ay, az);
        const float3 h = proxy.localExtentOrP1.xyz;
        float3 fv[4];
        nwbPhysicsGetBoxFace(cBox, ax, ay, az, h, int(faceIdx), fv);
        for(uint k = 0u; k < 4u; ++k)
            outVerts[k] = float4(fv[k], asfloat(k));
        if(faceIdx == 0u){ outWorldNormal = ax; outWorldOffset = dot(ax, cBox) + h.x; }
        else if(faceIdx == 1u){ outWorldNormal = -ax; outWorldOffset = dot(-ax, cBox) + h.x; }
        else if(faceIdx == 2u){ outWorldNormal = ay; outWorldOffset = dot(ay, cBox) + h.y; }
        else if(faceIdx == 3u){ outWorldNormal = -ay; outWorldOffset = dot(-ay, cBox) + h.y; }
        else if(faceIdx == 4u){ outWorldNormal = az; outWorldOffset = dot(az, cBox) + h.z; }
        else{ outWorldNormal = -az; outWorldOffset = dot(-az, cBox) + h.z; }
        return 4u;
    }

    const float4 lp = nwbPhysicsHullLocalFacePlane(proxy, hullVertexSlot, faceIdx);
    const float3 lN = lp.xyz;
    const float lOff = lp.w;
    outWorldNormal = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(lN, rot), float3(0.0f, 1.0f, 0.0f));
    outWorldOffset = lOff + dot(outWorldNormal, origin);

    const uint vOffset = proxy.filterData.w;
    const uint vCount = nwbPhysicsHullVertexCount(proxy);
    const uint fCount = nwbPhysicsHullFaceCount(proxy);
    const uint eCount = nwbPhysicsHullEdgeCount(proxy);
    const uint faceDescVecBase = vOffset + vCount + fCount + ((eCount + 3u) >> 2u);
    const uint faceDesc = nwbPhysicsReadHullPackedUint(hullVertexSlot, faceDescVecBase, faceIdx);
    const uint preWoundCount = (faceDesc >> 16u) & 0xffu;
    if(preWoundCount >= 3u && preWoundCount <= 16u){
        const uint startByte = faceDesc & 0xffffu;
        const uint faceIdxVecBase = faceDescVecBase + ((fCount + 3u) >> 2u);
        for(uint i = 0u; i < preWoundCount; ++i){
            const uint byteIdx = startByte + i;
            const uint packedWord = nwbPhysicsReadHullPackedUint(hullVertexSlot, faceIdxVecBase, byteIdx >> 2u);
            const uint vIdx = (packedWord >> ((byteIdx & 3u) * 8u)) & 0xffu;
            const float3 v = g_NwbPhysicsFloat4Buffers[hullVertexSlot][vOffset + vIdx].xyz;
            outVerts[i] = float4(origin + nwbPhysicsQuatRotate(v, rot), asfloat(vIdx));
        }
        return preWoundCount;
    }

    float4 localVerts[16];
    uint count = 0u;
    float3 center = float3(0.0f, 0.0f, 0.0f);
    for(uint i = 0u; i < vCount; ++i){
        const float3 v = g_NwbPhysicsFloat4Buffers[hullVertexSlot][vOffset + i].xyz;
        if(abs(dot(lN, v) - lOff) <= 1.5e-3f){
            if(count >= 16u){
                outVerts[0] = float4(0.0f, 0.0f, 0.0f, 0.0f);
                return 0u;
            }
            localVerts[count] = float4(v, asfloat(i));
            center += v;
            ++count;
        }
    }
    if(count < 3u){
        outVerts[0] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return 0u;
    }
    center /= float(count);

    float3 u1;
    float3 u2;
    nwbPhysicsComputeBasis(lN, u1, u2);
    float angles[16];
    for(uint i = 0u; i < count; ++i){
        const float3 rel = localVerts[i].xyz - center;
        angles[i] = atan2(dot(rel, u2), dot(rel, u1));
    }
    for(uint i = 1u; i < count; ++i){
        const float4 keyV = localVerts[i];
        const float keyA = angles[i];
        uint j = i;
        while(j > 0u && angles[j - 1u] > keyA){
            localVerts[j] = localVerts[j - 1u];
            angles[j] = angles[j - 1u];
            --j;
        }
        localVerts[j] = keyV;
        angles[j] = keyA;
    }

    for(uint i = 0u; i < count; ++i)
        outVerts[i] = float4(origin + nwbPhysicsQuatRotate(localVerts[i].xyz, rot), localVerts[i].w);
    return count;
}

void nwbPhysicsSolveSphereHullDeep(
    const NwbPhysicsGpuShapeProxy proxyA,
    const float3 originA,
    const float4 rotA,
    const float3 bodyPosA,
    const NwbPhysicsGpuShapeProxy proxyB,
    const float3 originB,
    const float4 rotB,
    const float3 bodyPosB,
    const uint hullVertexSlot,
    const float maxDist,
    inout NwbPhysicsGpuNarrowphaseManifold result
){
    const float rA = proxyA.localCenterRadius.w;
    const float rB = proxyB.localCenterRadius.w;
    const float totalR = rA + rB;
    const bool sphereIsA = (proxyA.bodyIndexTypeShape.y == NWB_PHYSICS_SHAPE_TYPE_SPHERE);
    const NwbPhysicsGpuShapeProxy pS = sphereIsA ? proxyA : proxyB;
    const NwbPhysicsGpuShapeProxy pH = sphereIsA ? proxyB : proxyA;
    const float3 origS = sphereIsA ? originA : originB;
    const float4 qS = sphereIsA ? rotA : rotB;
    const float3 origH = sphereIsA ? originB : originA;
    const float4 qH = sphereIsA ? rotB : rotA;
    const uint fCount = nwbPhysicsHullFaceCount(pH);
    const uint vCount = nwbPhysicsHullVertexCount(pH);
    if(fCount == 0u || fCount > NWB_PHYSICS_MAX_HULL_FACES || vCount == 0u){
        result.pairInfo.w = 1u;
        return;
    }
    const float3 cS = origS + nwbPhysicsQuatRotate(pS.localCenterRadius.xyz, qS);
    const float3 localS = nwbPhysicsQuatInvRotate(cS - origH, qH);
    float maxPlaneDist = -1.0e20f;
    uint bestFace = 0u;
    float3 bestLocalN = float3(0.0f, 1.0f, 0.0f);
    for(uint fi = 0u; fi < fCount; ++fi){
        const float4 pl = nwbPhysicsHullLocalFacePlane(pH, hullVertexSlot, fi);
        const float d = dot(pl.xyz, localS) - pl.w;
        if(d > totalR + maxDist)
            return;
        if(d > maxPlaneDist){
            maxPlaneDist = d;
            bestFace = fi;
            bestLocalN = pl.xyz;
        }
    }
    const float rS = pS.localCenterRadius.w;
    const float rH = pH.localCenterRadius.w;
    const float3 worldOut = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(bestLocalN, qH), float3(0.0f, 1.0f, 0.0f));
    const float3 nAtoB = sphereIsA ? -worldOut : worldOut;
    const float3 wS = cS + (sphereIsA ? nAtoB : -nAtoB) * rS;
    const float3 wH = cS + worldOut * (rH - maxPlaneDist);
    result.pairInfo.z = 1u;
    result.normalPad = float4(nAtoB, 0.0f);
    result.anchorAFeature[0] = float4((sphereIsA ? wS : wH) - bodyPosA, asfloat(21u + bestFace));
    result.anchorBSeparation[0] = float4((sphereIsA ? wH : wS) - bodyPosB, maxPlaneDist - totalR);
}

bool nwbPhysicsTryClipCapsuleAgainstHullFace(
    const bool capIsA,
    const NwbPhysicsGpuShapeProxy pH,
    const float3 origH,
    const float4 qH,
    const float3 bodyPosA,
    const float3 bodyPosB,
    const float3 l0,
    const float3 l1,
    const float rC,
    const float rH,
    const float totalR,
    const uint bestFace,
    const float3 bestLocalN,
    const float bestFaceOff,
    const uint hullVertexSlot,
    const float maxDist,
    inout NwbPhysicsGpuNarrowphaseManifold result
){
    float4 faceVerts[16];
    float3 faceNUnused = bestLocalN;
    float faceOffUnused = bestFaceOff;
    const uint faceVertCount = nwbPhysicsGatherHullWorldFacePoly(
        pH,
        float3(0.0f, 0.0f, 0.0f),
        float4(0.0f, 0.0f, 0.0f, 1.0f),
        hullVertexSlot,
        bestFace,
        faceVerts,
        faceNUnused,
        faceOffUnused
    );
    if(faceVertCount < 3u){
        result.pairInfo.w = 1u;
        return true;
    }

    float3 clipA = l0;
    float3 clipB = l1;
    bool clippedOut = false;
    for(uint vi = 0u; vi < faceVertCount; ++vi){
        const float3 v0 = faceVerts[vi].xyz;
        const float3 v1 = faceVerts[(vi + 1u) % faceVertCount].xyz;
        const float3 sideN = nwbPhysicsSafeNormal(cross(v1 - v0, bestLocalN), float3(1.0f, 0.0f, 0.0f));
        const float sideOff = dot(sideN, v0);
        const float dA = dot(sideN, clipA) - sideOff;
        const float dB = dot(sideN, clipB) - sideOff;
        if(dA > 0.0f && dB > 0.0f){
            clippedOut = true;
            break;
        }
        if(dA > 0.0f){
            const float t = dA / max(dA - dB, 1.0e-12f);
            clipA = lerp(clipA, clipB, clamp(t, 0.0f, 1.0f));
        }
        else if(dB > 0.0f){
            const float t = dA / min(dA - dB, -1.0e-12f);
            clipB = lerp(clipA, clipB, clamp(t, 0.0f, 1.0f));
        }
    }
    if(clippedOut)
        return false;

    const float3 worldOut = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(bestLocalN, qH), float3(0.0f, 1.0f, 0.0f));
    const float3 nAtoB = capIsA ? -worldOut : worldOut;
    uint emitted = 0u;
    const float sepA = dot(bestLocalN, clipA) - bestFaceOff - totalR;
    if(sepA <= maxDist){
        const float3 wpA = origH + nwbPhysicsQuatRotate(clipA, qH);
        const float3 wC = wpA - worldOut * rC;
        const float3 wH = wpA + worldOut * (rH - (sepA + totalR));
        result.anchorAFeature[emitted] = float4((capIsA ? wC : wH) - bodyPosA, asfloat(22u + bestFace));
        result.anchorBSeparation[emitted] = float4((capIsA ? wH : wC) - bodyPosB, sepA);
        ++emitted;
    }
    const float3 segDelta = clipB - clipA;
    if(dot(segDelta, segDelta) > 2.5e-5f){
        const float sepB = dot(bestLocalN, clipB) - bestFaceOff - totalR;
        if(sepB <= maxDist && emitted < NWB_PHYSICS_MAX_MANIFOLD_POINTS){
            const float3 wpB = origH + nwbPhysicsQuatRotate(clipB, qH);
            const float3 wC = wpB - worldOut * rC;
            const float3 wH = wpB + worldOut * (rH - (sepB + totalR));
            const uint feat = (emitted == 0u) ? (22u + bestFace) : (23u + bestFace);
            result.anchorAFeature[emitted] = float4((capIsA ? wC : wH) - bodyPosA, asfloat(feat));
            result.anchorBSeparation[emitted] = float4((capIsA ? wH : wC) - bodyPosB, sepB);
            ++emitted;
        }
    }
    if(emitted > 0u){
        result.normalPad = float4(nAtoB, 0.0f);
        result.pairInfo.z = emitted;
        return true;
    }
    return false;
}

void nwbPhysicsSolveCapsuleHullDeep(
    const NwbPhysicsGpuShapeProxy proxyA,
    const float3 originA,
    const float4 rotA,
    const float3 bodyPosA,
    const NwbPhysicsGpuShapeProxy proxyB,
    const float3 originB,
    const float4 rotB,
    const float3 bodyPosB,
    const uint hullVertexSlot,
    const float maxDist,
    inout NwbPhysicsGpuNarrowphaseManifold result
){
    const float rA = proxyA.localCenterRadius.w;
    const float rB = proxyB.localCenterRadius.w;
    const float totalR = rA + rB;
    const bool capIsA = (proxyA.bodyIndexTypeShape.y == NWB_PHYSICS_SHAPE_TYPE_CAPSULE);
    const NwbPhysicsGpuShapeProxy pC = capIsA ? proxyA : proxyB;
    const NwbPhysicsGpuShapeProxy pH = capIsA ? proxyB : proxyA;
    const float3 origC = capIsA ? originA : originB;
    const float4 qC = capIsA ? rotA : rotB;
    const float3 origH = capIsA ? originB : originA;
    const float4 qH = capIsA ? rotB : rotA;
    const uint fCount = nwbPhysicsHullFaceCount(pH);
    const uint eCount = nwbPhysicsHullEdgeCount(pH);
    const uint vCount = nwbPhysicsHullVertexCount(pH);
    if(fCount == 0u || fCount > NWB_PHYSICS_MAX_HULL_FACES || eCount > NWB_PHYSICS_MAX_HULL_EDGES || vCount == 0u || vCount > NWB_PHYSICS_MAX_HULL_VERTICES){
        result.pairInfo.w = 1u;
        return;
    }
    const float3 wP0 = origC + nwbPhysicsQuatRotate(pC.localCenterRadius.xyz, qC);
    const float3 wP1 = origC + nwbPhysicsQuatRotate(pC.localExtentOrP1.xyz, qC);
    const float3 l0 = nwbPhysicsQuatInvRotate(wP0 - origH, qH);
    const float3 l1 = nwbPhysicsQuatInvRotate(wP1 - origH, qH);

    float maxFaceSep = -1.0e20f;
    uint bestFace = 0u;
    float3 bestLocalN = float3(0.0f, 1.0f, 0.0f);
    float bestFaceOff = 0.0f;
    float bestD0 = 0.0f;
    float bestD1 = 0.0f;
    for(uint fi = 0u; fi < fCount; ++fi){
        const float4 pl = nwbPhysicsHullLocalFacePlane(pH, hullVertexSlot, fi);
        const float d0 = dot(pl.xyz, l0) - pl.w;
        const float d1 = dot(pl.xyz, l1) - pl.w;
        const float faceSep = min(d0, d1);
        if(faceSep > totalR + maxDist)
            return;
        if(faceSep > maxFaceSep){
            maxFaceSep = faceSep;
            bestFace = fi;
            bestLocalN = pl.xyz;
            bestFaceOff = pl.w;
            bestD0 = d0;
            bestD1 = d1;
        }
    }

    const float rC = pC.localCenterRadius.w;
    const float rH = pH.localCenterRadius.w;
    const float3 dSegHull = l1 - l0;
    const float segHullLenSq = dot(dSegHull, dSegHull);
    float bestEdgeSep = -1.0e20f;
    float3 bestEdgeOut = float3(0.0f, 1.0f, 0.0f);
    float3 bestEdgeV0 = float3(0.0f, 0.0f, 0.0f);
    float3 bestEdgeV1 = float3(0.0f, 0.0f, 0.0f);
    uint bestHullEdge = 0u;
    bool hasEdgeCandidate = false;
    if(segHullLenSq > 1.0e-10f){
        for(uint ei = 0u; ei < eCount; ++ei){
            const uint packE = nwbPhysicsHullEdgePacked(pH, hullVertexSlot, ei);
            const uint v0Idx = packE & 0xffu;
            const uint v1Idx = (packE >> 8u) & 0xffu;
            if(v0Idx >= vCount || v1Idx >= vCount)
                continue;
            const float3 e0 = nwbPhysicsHullLocalVertex(pH, hullVertexSlot, v0Idx);
            const float3 e1 = nwbPhysicsHullLocalVertex(pH, hullVertexSlot, v1Idx);
            const float3 eDir = e1 - e0;
            const float3 cr = cross(dSegHull, eDir);
            const float crLenSq = dot(cr, cr);
            const float normTol = NWB_PHYSICS_PARALLEL_EDGE_TOL * NWB_PHYSICS_PARALLEL_EDGE_TOL * max(segHullLenSq * dot(eDir, eDir), 1.0e-12f);
            if(crLenSq <= normTol)
                continue;

            const float3 axis = cr * rsqrt(crLenSq);
            float hMax = -1.0e20f;
            float hMin = 1.0e20f;
            for(uint vi = 0u; vi < vCount; ++vi){
                const float p = dot(axis, nwbPhysicsHullLocalVertex(pH, hullVertexSlot, vi));
                hMax = max(hMax, p);
                hMin = min(hMin, p);
            }
            const float cProj = dot(axis, l0);
            const float sPos = cProj - hMax;
            const float sNeg = hMin - cProj;

            float sepE = sPos;
            float3 outAxis = axis;
            float supVal = hMax;
            if(sNeg > sPos){
                sepE = sNeg;
                outAxis = -axis;
                supVal = -hMin;
            }
            if(sepE - totalR > maxDist)
                return;
            if(abs(dot(outAxis, e0) - supVal) <= 1.0e-4f){
                if(!hasEdgeCandidate || sepE > bestEdgeSep){
                    hasEdgeCandidate = true;
                    bestEdgeSep = sepE;
                    bestEdgeOut = outAxis;
                    bestEdgeV0 = e0;
                    bestEdgeV1 = e1;
                    bestHullEdge = ei;
                }
            }
        }
    }
    if(hasEdgeCandidate && bestEdgeSep > maxFaceSep + 5.0e-4f){
        float sSeg = 0.0f;
        float tSeg = 0.0f;
        float3 cCap = float3(0.0f, 0.0f, 0.0f);
        float3 cEdge = float3(0.0f, 0.0f, 0.0f);
        nwbPhysicsClosestPointsSegments(l0, l1, bestEdgeV0, bestEdgeV1, sSeg, tSeg, cCap, cEdge);
        const float3 worldOut = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(bestEdgeOut, qH), float3(0.0f, 1.0f, 0.0f));
        const float3 nAtoB = capIsA ? -worldOut : worldOut;
        const float3 wCap = origH + nwbPhysicsQuatRotate(cCap, qH);
        const float3 wEdge = origH + nwbPhysicsQuatRotate(cEdge, qH);
        const float3 wC = wCap - worldOut * rC;
        const float3 wH = wEdge + worldOut * rH;
        result.pairInfo.z = 1u;
        result.normalPad = float4(nAtoB, 0.0f);
        result.anchorAFeature[0] = float4((capIsA ? wC : wH) - bodyPosA, asfloat(nwbPhysicsMakeFeatureId(0u, 4u, 2u, bestHullEdge & 0xffu)));
        result.anchorBSeparation[0] = float4((capIsA ? wH : wC) - bodyPosB, bestEdgeSep - totalR);
        return;
    }

    if(nwbPhysicsTryClipCapsuleAgainstHullFace(
        capIsA,
        pH,
        origH,
        qH,
        bodyPosA,
        bodyPosB,
        l0,
        l1,
        rC,
        rH,
        totalR,
        bestFace,
        bestLocalN,
        bestFaceOff,
        hullVertexSlot,
        maxDist,
        result
    ))
        return;

    const float3 worldOut = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(bestLocalN, qH), float3(0.0f, 1.0f, 0.0f));
    const float3 nAtoB = capIsA ? -worldOut : worldOut;
    result.normalPad = float4(nAtoB, 0.0f);

    const float3 deepLocal = (bestD1 < bestD0) ? l1 : l0;
    const float minD = min(bestD0, bestD1);
    const float3 deepWorld = origH + nwbPhysicsQuatRotate(deepLocal, qH);
    const float3 wC = deepWorld - worldOut * rC;
    const float3 wH = deepWorld + worldOut * (rH - minD);
    result.pairInfo.z = 1u;
    result.anchorAFeature[0] = float4((capIsA ? wC : wH) - bodyPosA, asfloat(22u + bestFace));
    result.anchorBSeparation[0] = float4((capIsA ? wH : wC) - bodyPosB, minD - totalR);
}

void nwbPhysicsSolveHullHullDeep(
    const NwbPhysicsGpuShapeProxy proxyA,
    const float3 originA,
    const float4 rotA,
    const float3 bodyPosA,
    const NwbPhysicsGpuShapeProxy proxyB,
    const float3 originB,
    const float4 rotB,
    const float3 bodyPosB,
    const uint hullVertexSlot,
    const float maxDist,
    inout NwbPhysicsGpuNarrowphaseManifold result
){
    const float rA = proxyA.localCenterRadius.w;
    const float rB = proxyB.localCenterRadius.w;
    const float totalR = rA + rB;

    const uint fCountA = nwbPhysicsHullFaceCount(proxyA);
    const uint fCountB = nwbPhysicsHullFaceCount(proxyB);
    const uint vCountA = nwbPhysicsHullVertexCount(proxyA);
    const uint vCountB = nwbPhysicsHullVertexCount(proxyB);
    const uint eCountA = nwbPhysicsHullEdgeCount(proxyA);
    const uint eCountB = nwbPhysicsHullEdgeCount(proxyB);
    if(
        fCountA == 0u
        || fCountB == 0u
        || fCountA > NWB_PHYSICS_MAX_HULL_FACES
        || fCountB > NWB_PHYSICS_MAX_HULL_FACES
        || vCountA == 0u
        || vCountB == 0u
        || vCountA > NWB_PHYSICS_MAX_HULL_VERTICES
        || vCountB > NWB_PHYSICS_MAX_HULL_VERTICES
        || eCountA > NWB_PHYSICS_MAX_HULL_EDGES
        || eCountB > NWB_PHYSICS_MAX_HULL_EDGES
    ){
        result.pairInfo.w = 1u;
        return;
    }

    float bestSepA = -1.0e20f;
    uint bestFaceA = 0u;
    float3 bestWorldNA = float3(0.0f, 1.0f, 0.0f);
    for(uint i = 0u; i < fCountA; ++i){
        const float4 lp = nwbPhysicsHullLocalFacePlane(proxyA, hullVertexSlot, i);
        const float3 wN = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(lp.xyz, rotA), float3(0.0f, 1.0f, 0.0f));
        const float wOff = lp.w + dot(wN, originA);
        uint tmpIdx = 0u;
        const float3 supB = nwbPhysicsSupportWorld(proxyB, originB, rotB, hullVertexSlot, -wN, tmpIdx);
        const float sep = dot(wN, supB) - wOff - totalR;
        if(sep > maxDist)
            return;
        if(sep > bestSepA){
            bestSepA = sep;
            bestFaceA = i;
            bestWorldNA = wN;
        }
    }

    float bestSepB = -1.0e20f;
    uint bestFaceB = 0u;
    float3 bestWorldNB = float3(0.0f, 1.0f, 0.0f);
    for(uint i = 0u; i < fCountB; ++i){
        const float4 lp = nwbPhysicsHullLocalFacePlane(proxyB, hullVertexSlot, i);
        const float3 wN = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(lp.xyz, rotB), float3(0.0f, 1.0f, 0.0f));
        const float wOff = lp.w + dot(wN, originB);
        uint tmpIdx = 0u;
        const float3 supA = nwbPhysicsSupportWorld(proxyA, originA, rotA, hullVertexSlot, -wN, tmpIdx);
        const float sep = dot(wN, supA) - wOff - totalR;
        if(sep > maxDist)
            return;
        if(sep > bestSepB){
            bestSepB = sep;
            bestFaceB = i;
            bestWorldNB = wN;
        }
    }

    const float3 cA = originA + nwbPhysicsQuatRotate(proxyA.localCenterRadius.xyz, rotA);
    float bestEdgeSep = -1.0e20f;
    float3 bestEdgeAxis = float3(0.0f, 1.0f, 0.0f);
    float3 bestEA0 = float3(0.0f, 0.0f, 0.0f);
    float3 bestEA1 = float3(0.0f, 0.0f, 0.0f);
    float3 bestEB0 = float3(0.0f, 0.0f, 0.0f);
    float3 bestEB1 = float3(0.0f, 0.0f, 0.0f);
    uint bestEdgeIdxA = 0u;
    uint bestEdgeIdxB = 0u;

    for(uint eA = 0u; eA < eCountA; ++eA){
        const uint packA = nwbPhysicsHullEdgePacked(proxyA, hullVertexSlot, eA);
        const uint vA0Idx = packA & 0xffu;
        const uint vA1Idx = (packA >> 8u) & 0xffu;
        const uint fA0Idx = (packA >> 16u) & 0xffu;
        const uint fA1Idx = (packA >> 24u) & 0xffu;
        if(vA0Idx >= vCountA || vA1Idx >= vCountA || fA0Idx >= fCountA || fA1Idx >= fCountA)
            continue;
        const float3 pA0 = originA + nwbPhysicsQuatRotate(nwbPhysicsHullLocalVertex(proxyA, hullVertexSlot, vA0Idx), rotA);
        const float3 pA1 = originA + nwbPhysicsQuatRotate(nwbPhysicsHullLocalVertex(proxyA, hullVertexSlot, vA1Idx), rotA);
        const float3 dirA = pA1 - pA0;
        const float dirALenSq = dot(dirA, dirA);
        const float3 nA0 = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(nwbPhysicsHullLocalFacePlane(proxyA, hullVertexSlot, fA0Idx).xyz, rotA), float3(1.0f, 0.0f, 0.0f));
        const float3 nA1 = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(nwbPhysicsHullLocalFacePlane(proxyA, hullVertexSlot, fA1Idx).xyz, rotA), float3(0.0f, 1.0f, 0.0f));
        const float3 edgeBA = cross(nA0, nA1);

        for(uint eB = 0u; eB < eCountB; ++eB){
            const uint packB = nwbPhysicsHullEdgePacked(proxyB, hullVertexSlot, eB);
            const uint vB0Idx = packB & 0xffu;
            const uint vB1Idx = (packB >> 8u) & 0xffu;
            const uint fB0Idx = (packB >> 16u) & 0xffu;
            const uint fB1Idx = (packB >> 24u) & 0xffu;
            if(vB0Idx >= vCountB || vB1Idx >= vCountB || fB0Idx >= fCountB || fB1Idx >= fCountB)
                continue;
            const float3 pB0 = originB + nwbPhysicsQuatRotate(nwbPhysicsHullLocalVertex(proxyB, hullVertexSlot, vB0Idx), rotB);
            const float3 pB1 = originB + nwbPhysicsQuatRotate(nwbPhysicsHullLocalVertex(proxyB, hullVertexSlot, vB1Idx), rotB);
            const float3 dirB = pB1 - pB0;
            const float3 nB0 = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(nwbPhysicsHullLocalFacePlane(proxyB, hullVertexSlot, fB0Idx).xyz, rotB), float3(1.0f, 0.0f, 0.0f));
            const float3 nB1 = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(nwbPhysicsHullLocalFacePlane(proxyB, hullVertexSlot, fB1Idx).xyz, rotB), float3(0.0f, 1.0f, 0.0f));
            const float3 edgeDC = cross(nB0, nB1);

            if(!nwbPhysicsIsMinkowskiFace(nA0, nA1, edgeBA, -nB0, -nB1, edgeDC))
                continue;

            const float3 rawCross = cross(dirA, dirB);
            const float lenSq = dot(rawCross, rawCross);
            const float normTol = 1.0e-6f * max(dirALenSq * dot(dirB, dirB), 1.0e-12f);
            if(lenSq <= normTol)
                continue;

            float3 axis = rawCross * rsqrt(lenSq);
            if(dot(axis, pA0 - cA) < 0.0f)
                axis = -axis;

            const float sep = dot(axis, pB0 - pA0) - totalR;
            if(sep > maxDist)
                return;
            if(sep > bestEdgeSep){
                bestEdgeSep = sep;
                bestEdgeAxis = axis;
                bestEA0 = pA0;
                bestEA1 = pA1;
                bestEB0 = pB0;
                bestEB1 = pB1;
                bestEdgeIdxA = eA;
                bestEdgeIdxB = eB;
            }
        }
    }

    if(bestEdgeSep > max(bestSepA, bestSepB) + 5.0e-4f){
        float sSeg = 0.0f;
        float tSeg = 0.0f;
        float3 segPtA = float3(0.0f, 0.0f, 0.0f);
        float3 segPtB = float3(0.0f, 0.0f, 0.0f);
        nwbPhysicsClosestPointsSegments(bestEA0, bestEA1, bestEB0, bestEB1, sSeg, tSeg, segPtA, segPtB);
        result.pairInfo.z = 1u;
        result.normalPad = float4(bestEdgeAxis, 0.0f);
        result.anchorAFeature[0] = float4(segPtA + bestEdgeAxis * rA - bodyPosA, asfloat(50u + (bestEdgeIdxA & 0xffu) + ((bestEdgeIdxB & 0xffu) << 8u)));
        result.anchorBSeparation[0] = float4(segPtB - bestEdgeAxis * rB - bodyPosB, bestEdgeSep);
        return;
    }

    const bool refIsA = (bestSepB <= bestSepA + 1.0e-4f);
    const NwbPhysicsGpuShapeProxy refProxy = refIsA ? proxyA : proxyB;
    const NwbPhysicsGpuShapeProxy incProxy = refIsA ? proxyB : proxyA;
    const float3 refOrig = refIsA ? originA : originB;
    const float4 refRot = refIsA ? rotA : rotB;
    const float3 incOrig = refIsA ? originB : originA;
    const float4 incRot = refIsA ? rotB : rotA;
    const uint refFace = refIsA ? bestFaceA : bestFaceB;
    const uint incFaceCount = refIsA ? fCountB : fCountA;
    const float3 refWorldN = refIsA ? bestWorldNA : bestWorldNB;
    const float refR = refIsA ? rA : rB;
    const float incR = refIsA ? rB : rA;

    uint incFace = 0u;
    float minIncDot = 2.0f;
    for(uint i = 0u; i < incFaceCount; ++i){
        const float4 lp = nwbPhysicsHullLocalFacePlane(incProxy, hullVertexSlot, i);
        const float3 wN = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(lp.xyz, incRot), float3(0.0f, 1.0f, 0.0f));
        const float d = dot(wN, refWorldN);
        if(d < minIncDot){
            minIncDot = d;
            incFace = i;
        }
    }

    float4 refPoly[16];
    float3 refNUnused = refWorldN;
    float refOff = 0.0f;
    const uint refPolyCount = nwbPhysicsGatherHullWorldFacePoly(refProxy, refOrig, refRot, hullVertexSlot, refFace, refPoly, refNUnused, refOff);

    float4 poly0[16];
    float4 poly1[16];
    for(uint k = 0u; k < 16u; ++k)
        poly1[k] = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float3 incNUnused = refWorldN;
    float incOffUnused = 0.0f;
    uint polyCount = nwbPhysicsGatherHullWorldFacePoly(incProxy, incOrig, incRot, hullVertexSlot, incFace, poly0, incNUnused, incOffUnused);

    if(refPolyCount < 3u || polyCount < 3u){
        result.pairInfo.w = 1u;
        return;
    }

    for(uint i = 0u; i < refPolyCount; ++i){
        const float3 v0 = refPoly[i].xyz;
        const float3 v1 = refPoly[(i + 1u) % refPolyCount].xyz;
        const float3 sideN = nwbPhysicsSafeNormal(cross(v1 - v0, refNUnused), float3(1.0f, 0.0f, 0.0f));
        const float sideOff = dot(sideN, v0);
        polyCount = nwbPhysicsClipPolygonPlane(poly0, polyCount, sideN, sideOff, i, poly1);
        if(polyCount == 0u){
            result.pairInfo.w = 1u;
            return;
        }
        for(uint k = 0u; k < polyCount; ++k)
            poly0[k] = poly1[k];
    }

    const uint hullFacePack = ((refIsA ? 1u : 0u) << 28u) | ((refFace & 0x3fu) << 22u) | ((incFace & 0x3fu) << 16u);
    float3 candWRef[16];
    float3 candWInc[16];
    float3 candPos[16];
    float candSep[16];
    uint candFeat[16];
    uint candCount = 0u;
    for(uint k = 0u; k < polyCount && candCount < 16u; ++k){
        const float3 pt = poly0[k].xyz;
        const float sep = dot(refNUnused, pt) - refOff - totalR;
        if(sep <= maxDist){
            candWRef[candCount] = pt - refNUnused * (sep + totalR - refR);
            candWInc[candCount] = pt - refNUnused * incR;
            candPos[candCount] = pt;
            candSep[candCount] = sep;
            candFeat[candCount] = hullFacePack | (asuint(poly0[k].w) & 0xffffu);
            ++candCount;
        }
    }
    if(candCount == 0u){
        result.pairInfo.w = 1u;
        return;
    }

    result.normalPad = float4(refIsA ? refNUnused : -refNUnused, 0.0f);
    nwbPhysicsReduceManifoldTo4(refNUnused, candWRef, candWInc, candPos, candSep, candFeat, candCount, refIsA, bodyPosA, bodyPosB, result);
}

void nwbPhysicsPushMeshCand16(
    const float3 wA,
    const float3 wB,
    const float3 wNormalAtoB,
    const float sep,
    const uint feat,
    inout float3 candWA[16],
    inout float3 candWB[16],
    inout float3 candNorm[16],
    inout float candSep[16],
    inout uint candFeat[16],
    inout uint candCount
){
    uint slot = candCount;
    if(candCount < 16u){
        ++candCount;
    }
    else{
        uint worstIdx = 0u;
        for(uint i = 1u; i < 16u; ++i){
            if(candSep[i] > candSep[worstIdx])
                worstIdx = i;
        }
        if(sep >= candSep[worstIdx])
            return;
        slot = worstIdx;
    }
    candWA[slot] = wA;
    candWB[slot] = wB;
    candNorm[slot] = wNormalAtoB;
    candSep[slot] = sep;
    candFeat[slot] = feat;
}

void nwbPhysicsFinalizeMeshCand16(
    inout float3 candWA[16],
    inout float3 candWB[16],
    const float3 candNorm[16],
    inout float candSep[16],
    inout uint candFeat[16],
    const uint candCount,
    const float3 chosenWorldN,
    const float3 bodyPosA,
    const float3 bodyPosB,
    inout NwbPhysicsGpuNarrowphaseManifold result
){
    if(candCount == 0u)
        return;

    float3 useNormal = chosenWorldN;
    float3 filtPos[16];

    uint filtCount = 0u;
    for(uint i = 0u; i < candCount; ++i){
        if(dot(candNorm[i], useNormal) > 0.7f){
            candWA[filtCount] = candWA[i];
            candWB[filtCount] = candWB[i];
            filtPos[filtCount] = (candWA[i] + candWB[i]) * 0.5f;
            candSep[filtCount] = candSep[i];
            candFeat[filtCount] = candFeat[i];
            ++filtCount;
        }
    }

    if(filtCount == 0u){
        uint bestIdx = 0u;
        for(uint i = 1u; i < candCount; ++i){
            if(candSep[i] < candSep[bestIdx])
                bestIdx = i;
        }
        useNormal = candNorm[bestIdx];
        candWA[0] = candWA[bestIdx];
        candWB[0] = candWB[bestIdx];
        filtPos[0] = (candWA[bestIdx] + candWB[bestIdx]) * 0.5f;
        candSep[0] = candSep[bestIdx];
        candFeat[0] = candFeat[bestIdx];
        filtCount = 1u;
    }

    result.normalPad = float4(useNormal, 0.0f);
    nwbPhysicsReduceManifoldTo4(useNormal, candWA, candWB, filtPos, candSep, candFeat, filtCount, true, bodyPosA, bodyPosB, result);
}

uint nwbPhysicsClosedMeshRayVote(
    const uint nodeBase,
    const uint triBase,
    const uint meshNodeSlot,
    const uint meshTriSlot,
    const float3 localPoint,
    const float3 rayDir,
    out bool outOverflow
){
    outOverflow = false;
    const float3 invDir = 1.0f / rayDir;
    uint hitCount = 0u;
    uint stack[64];
    uint sp = 0u;
    stack[sp++] = 0u;
    while(sp > 0u){
        const uint nodeRel = stack[--sp];
        const float4 n0 = g_NwbPhysicsFloat4Buffers[meshNodeSlot][(nodeBase + nodeRel) * 2u + 0u];
        const float4 n1 = g_NwbPhysicsFloat4Buffers[meshNodeSlot][(nodeBase + nodeRel) * 2u + 1u];
        const float3 t0 = (n0.xyz - localPoint) * invDir;
        const float3 t1 = (n1.xyz - localPoint) * invDir;
        const float3 tSmall = min(t0, t1);
        const float3 tLarge = max(t0, t1);
        const float tEnter = max(0.0f, max(tSmall.x, max(tSmall.y, tSmall.z)));
        const float tExit = min(tLarge.x, min(tLarge.y, tLarge.z));
        if(tExit < tEnter)
            continue;

        const uint meta = asuint(n1.w);
        if((meta & 0x80000000u) != 0u){
            const uint firstTri = asuint(n0.w);
            const uint triCount = meta & 0x7fffffffu;
            for(uint ti = 0u; ti < triCount; ++ti){
                const uint triIdx = triBase + firstTri + ti;
                const float3 v0 = g_NwbPhysicsFloat4Buffers[meshTriSlot][triIdx * 3u + 0u].xyz;
                const float3 v1 = g_NwbPhysicsFloat4Buffers[meshTriSlot][triIdx * 3u + 1u].xyz;
                const float3 v2 = g_NwbPhysicsFloat4Buffers[meshTriSlot][triIdx * 3u + 2u].xyz;
                const float3 e1 = v1 - v0;
                const float3 e2 = v2 - v0;
                const float3 pvec = cross(rayDir, e2);
                const float det = dot(e1, pvec);
                if(abs(det) <= 1.0e-8f)
                    continue;
                const float invDet = 1.0f / det;
                const float3 tvec = localPoint - v0;
                const float u = dot(tvec, pvec) * invDet;
                if(u < -1.0e-5f || u > 1.0f + 1.0e-5f)
                    continue;
                const float3 qvec = cross(tvec, e1);
                const float v = dot(rayDir, qvec) * invDet;
                if(v < -1.0e-5f || u + v > 1.0f + 1.0e-5f)
                    continue;
                const float t = dot(e2, qvec) * invDet;
                if(t > 1.0e-6f)
                    ++hitCount;
            }
        }
        else{
            if(sp + 2u > 64u){
                outOverflow = true;
                return 0u;
            }
            stack[sp++] = asuint(n0.w);
            stack[sp++] = meta & 0x7fffffffu;
        }
    }
    return hitCount & 1u;
}

bool nwbPhysicsIsPointInsideClosedMesh(
    const uint nodeBase,
    const uint triBase,
    const uint meshNodeSlot,
    const uint meshTriSlot,
    const float3 localPoint,
    out bool outOverflow
){
    outOverflow = false;
    const float4 rootN0 = g_NwbPhysicsFloat4Buffers[meshNodeSlot][nodeBase * 2u + 0u];
    const float4 rootN1 = g_NwbPhysicsFloat4Buffers[meshNodeSlot][nodeBase * 2u + 1u];
    if(any(localPoint < rootN0.xyz) || any(localPoint > rootN1.xyz))
        return false;

    const uint vote0 = nwbPhysicsClosedMeshRayVote(nodeBase, triBase, meshNodeSlot, meshTriSlot, localPoint, normalize(float3(1.0f, 0.37139067f, 0.21821789f)), outOverflow);
    if(outOverflow)
        return false;
    const uint vote1 = nwbPhysicsClosedMeshRayVote(nodeBase, triBase, meshNodeSlot, meshTriSlot, localPoint, normalize(float3(-0.29317123f, 1.0f, 0.51791341f)), outOverflow);
    if(outOverflow)
        return false;
    if(vote0 == vote1)
        return vote0 != 0u;
    const uint vote2 = nwbPhysicsClosedMeshRayVote(nodeBase, triBase, meshNodeSlot, meshTriSlot, localPoint, normalize(float3(0.43189211f, -0.61923417f, 1.0f)), outOverflow);
    if(outOverflow)
        return false;
    return (vote0 + vote1 + vote2) >= 2u;
}

bool nwbPhysicsFindClosestPointOnClosedMesh(
    const uint nodeBase,
    const uint triBase,
    const uint meshNodeSlot,
    const uint meshTriSlot,
    const float3 localPoint,
    out float3 outPt,
    out float3 outTriN,
    out uint outLocalTriIdx,
    out float outDist,
    out bool outOverflow
){
    outPt = float3(0.0f, 0.0f, 0.0f);
    outTriN = float3(0.0f, 1.0f, 0.0f);
    outLocalTriIdx = 0u;
    outDist = 0.0f;
    outOverflow = false;

    bool found = false;
    float bestDistSq = 1.0e30f;
    uint stack[64];
    uint sp = 0u;
    stack[sp++] = 0u;
    while(sp > 0u){
        const uint nodeRel = stack[--sp];
        const float4 n0 = g_NwbPhysicsFloat4Buffers[meshNodeSlot][(nodeBase + nodeRel) * 2u + 0u];
        const float4 n1 = g_NwbPhysicsFloat4Buffers[meshNodeSlot][(nodeBase + nodeRel) * 2u + 1u];
        const float3 dAabb = max(float3(0.0f, 0.0f, 0.0f), max(n0.xyz - localPoint, localPoint - n1.xyz));
        if(dot(dAabb, dAabb) >= bestDistSq)
            continue;

        const uint meta = asuint(n1.w);
        if((meta & 0x80000000u) != 0u){
            const uint firstTri = asuint(n0.w);
            const uint triCount = meta & 0x7fffffffu;
            for(uint ti = 0u; ti < triCount; ++ti){
                const uint localTri = firstTri + ti;
                const uint triIdx = triBase + localTri;
                const float3 v0 = g_NwbPhysicsFloat4Buffers[meshTriSlot][triIdx * 3u + 0u].xyz;
                const float3 v1 = g_NwbPhysicsFloat4Buffers[meshTriSlot][triIdx * 3u + 1u].xyz;
                const float3 v2 = g_NwbPhysicsFloat4Buffers[meshTriSlot][triIdx * 3u + 2u].xyz;
                const float3 tMin = min(v0, min(v1, v2));
                const float3 tMax = max(v0, max(v1, v2));
                const float3 dTri = max(float3(0.0f, 0.0f, 0.0f), max(tMin - localPoint, localPoint - tMax));
                if(dot(dTri, dTri) >= bestDistSq)
                    continue;
                const float3 q = nwbPhysicsClosestPointOnTriangle(localPoint, v0, v1, v2);
                const float3 diff = q - localPoint;
                const float dSq = dot(diff, diff);
                if(dSq < bestDistSq){
                    bestDistSq = dSq;
                    found = true;
                    outPt = q;
                    outTriN = nwbPhysicsSafeNormal(cross(v1 - v0, v2 - v0), float3(0.0f, 1.0f, 0.0f));
                    outLocalTriIdx = localTri;
                }
            }
        }
        else{
            const uint leftRel = asuint(n0.w);
            const uint rightRel = meta & 0x7fffffffu;
            const float4 ln0 = g_NwbPhysicsFloat4Buffers[meshNodeSlot][(nodeBase + leftRel) * 2u + 0u];
            const float4 ln1 = g_NwbPhysicsFloat4Buffers[meshNodeSlot][(nodeBase + leftRel) * 2u + 1u];
            const float4 rn0 = g_NwbPhysicsFloat4Buffers[meshNodeSlot][(nodeBase + rightRel) * 2u + 0u];
            const float4 rn1 = g_NwbPhysicsFloat4Buffers[meshNodeSlot][(nodeBase + rightRel) * 2u + 1u];
            const float3 dl = max(float3(0.0f, 0.0f, 0.0f), max(ln0.xyz - localPoint, localPoint - ln1.xyz));
            const float3 dr = max(float3(0.0f, 0.0f, 0.0f), max(rn0.xyz - localPoint, localPoint - rn1.xyz));
            const float dLeftSq = dot(dl, dl);
            const float dRightSq = dot(dr, dr);
            if(dLeftSq <= dRightSq){
                if(dRightSq < bestDistSq){
                    if(sp >= 64u){
                        outOverflow = true;
                        return false;
                    }
                    stack[sp++] = rightRel;
                }
                if(dLeftSq < bestDistSq){
                    if(sp >= 64u){
                        outOverflow = true;
                        return false;
                    }
                    stack[sp++] = leftRel;
                }
            }
            else{
                if(dLeftSq < bestDistSq){
                    if(sp >= 64u){
                        outOverflow = true;
                        return false;
                    }
                    stack[sp++] = leftRel;
                }
                if(dRightSq < bestDistSq){
                    if(sp >= 64u){
                        outOverflow = true;
                        return false;
                    }
                    stack[sp++] = rightRel;
                }
            }
        }
    }
    if(found)
        outDist = sqrt(bestDistSq);
    return found;
}

void nwbPhysicsClipSegmentTrianglePlane(
    const float3 a,
    const float3 b,
    const float3 n,
    const float off,
    inout float tMin,
    inout float tMax
){
    const float da = dot(n, a) - off;
    const float db = dot(n, b) - off;
    if(da > 0.0f && db > 0.0f){
        tMax = -1.0f;
        return;
    }
    const float denom = db - da;
    if(abs(denom) <= 1.0e-12f)
        return;
    const float tHit = -da / denom;
    if(da > 0.0f)
        tMin = max(tMin, tHit);
    else if(db > 0.0f)
        tMax = min(tMax, tHit);
}

void nwbPhysicsCollideMeshMeshPair(
    const NwbPhysicsGpuShapeProxy proxyA,
    const float3 originA,
    const float4 rotA,
    const float3 bodyPosA,
    const NwbPhysicsGpuShapeProxy proxyB,
    const float3 originB,
    const float4 rotB,
    const float3 bodyPosB,
    const uint meshNodeSlot,
    const uint meshTriSlot,
    const uint hullVertexSlot,
    const float maxDist,
    inout NwbPhysicsGpuNarrowphaseManifold result
){
    const uint nodeBaseA = proxyA.filterData.w;
    const uint triBaseA = asuint(proxyA.aabbMaxPad.w);
    const uint nodeBaseB = proxyB.filterData.w;
    const uint triBaseB = asuint(proxyB.aabbMaxPad.w);
    if(nodeBaseA == NWB_PHYSICS_INVALID_INDEX || triBaseA == NWB_PHYSICS_INVALID_INDEX || nodeBaseB == NWB_PHYSICS_INVALID_INDEX || triBaseB == NWB_PHYSICS_INVALID_INDEX){
        result.pairInfo.w = 1u;
        return;
    }

    const float rA = proxyA.localCenterRadius.w;
    const float rB = proxyB.localCenterRadius.w;
    const float totalR = rA + rB;
    const float marginScalar = totalR + maxDist;
    const float3 marginVec = float3(marginScalar, marginScalar, marginScalar);

    const float4 invRotA = float4(-rotA.xyz, rotA.w);
    const float3 posBInA = nwbPhysicsQuatRotate(originB - originA, invRotA);
    const float4 rotBToA = nwbPhysicsQuatNormalize(nwbPhysicsQuatMul(invRotA, rotB));
    float3 bAxX;
    float3 bAxY;
    float3 bAxZ;
    nwbPhysicsQuatToAxes(rotBToA, bAxX, bAxY, bAxZ);
    bAxX = abs(bAxX);
    bAxY = abs(bAxY);
    bAxZ = abs(bAxZ);

    float3 candWA[16];
    float3 candWB[16];
    float3 candNorm[16];
    float candSep[16];
    uint candFeat[16];
    uint candCount = 0u;
    float bestSep = 1.0e20f;
    float3 chosenWorldN = float3(0.0f, -1.0f, 0.0f);

    uint2 stack[64];
    uint sp = 0u;
    stack[sp++] = uint2(0u, 0u);
    while(sp > 0u){
        const uint2 pairNode = stack[--sp];
        const float4 nA0 = g_NwbPhysicsFloat4Buffers[meshNodeSlot][(nodeBaseA + pairNode.x) * 2u + 0u];
        const float4 nA1 = g_NwbPhysicsFloat4Buffers[meshNodeSlot][(nodeBaseA + pairNode.x) * 2u + 1u];
        const float4 nB0 = g_NwbPhysicsFloat4Buffers[meshNodeSlot][(nodeBaseB + pairNode.y) * 2u + 0u];
        const float4 nB1 = g_NwbPhysicsFloat4Buffers[meshNodeSlot][(nodeBaseB + pairNode.y) * 2u + 1u];

        const float3 bMid = (nB0.xyz + nB1.xyz) * 0.5f;
        const float3 bHalf = (nB1.xyz - nB0.xyz) * 0.5f;
        const float3 bCenterA = posBInA + nwbPhysicsQuatRotate(bMid, rotBToA);
        const float3 bExtA = bAxX * bHalf.x + bAxY * bHalf.y + bAxZ * bHalf.z + marginVec;
        if(!nwbPhysicsAabbOverlaps(nA0.xyz, nA1.xyz, bCenterA - bExtA, bCenterA + bExtA))
            continue;

        const uint metaA = asuint(nA1.w);
        const uint metaB = asuint(nB1.w);
        const bool leafA = (metaA & 0x80000000u) != 0u;
        const bool leafB = (metaB & 0x80000000u) != 0u;

        if(!leafA && !leafB){
            if(sp + 2u > 64u){
                result.pairInfo.w = 1u;
                return;
            }
            const float3 szA = nA1.xyz - nA0.xyz;
            const float3 szB = nB1.xyz - nB0.xyz;
            if(dot(szA, szA) >= dot(szB, szB)){
                stack[sp++] = uint2(asuint(nA0.w), pairNode.y);
                stack[sp++] = uint2(metaA & 0x7fffffffu, pairNode.y);
            }
            else{
                stack[sp++] = uint2(pairNode.x, asuint(nB0.w));
                stack[sp++] = uint2(pairNode.x, metaB & 0x7fffffffu);
            }
        }
        else if(!leafA){
            if(sp + 2u > 64u){
                result.pairInfo.w = 1u;
                return;
            }
            stack[sp++] = uint2(asuint(nA0.w), pairNode.y);
            stack[sp++] = uint2(metaA & 0x7fffffffu, pairNode.y);
        }
        else if(!leafB){
            if(sp + 2u > 64u){
                result.pairInfo.w = 1u;
                return;
            }
            stack[sp++] = uint2(pairNode.x, asuint(nB0.w));
            stack[sp++] = uint2(pairNode.x, metaB & 0x7fffffffu);
        }
        else{
            const uint firstTriA = asuint(nA0.w);
            const uint triCountA = metaA & 0x7fffffffu;
            const uint firstTriB = asuint(nB0.w);
            const uint triCountB = metaB & 0x7fffffffu;
            const float3 nodeBMinA = bCenterA - bExtA;
            const float3 nodeBMaxA = bCenterA + bExtA;

            for(uint tiA = 0u; tiA < triCountA; ++tiA){
                const uint localTriA = firstTriA + tiA;
                const uint triIdxA = triBaseA + localTriA;
                const float4 aT0 = g_NwbPhysicsFloat4Buffers[meshTriSlot][triIdxA * 3u + 0u];
                const float4 aT1 = g_NwbPhysicsFloat4Buffers[meshTriSlot][triIdxA * 3u + 1u];
                const float4 aT2 = g_NwbPhysicsFloat4Buffers[meshTriSlot][triIdxA * 3u + 2u];
                const float3 av0 = aT0.xyz;
                const float3 av1 = aT1.xyz;
                const float3 av2 = aT2.xyz;
                const bool closedA = (aT1.w != 0.0f);
                const float3 tMinA = min(av0, min(av1, av2));
                const float3 tMaxA = max(av0, max(av1, av2));
                if(!nwbPhysicsAabbOverlaps(tMinA, tMaxA, nodeBMinA, nodeBMaxA))
                    continue;

                const uint edgeFlagsA = asuint(aT0.w);
                const float3 rawTriNA = nwbPhysicsSafeNormal(cross(av1 - av0, av2 - av0), float3(0.0f, 1.0f, 0.0f));
                const float3 wA0 = originA + nwbPhysicsQuatRotate(av0, rotA);
                const float3 wA1 = originA + nwbPhysicsQuatRotate(av1, rotA);
                const float3 wA2 = originA + nwbPhysicsQuatRotate(av2, rotA);
                const float3 rawWTriNA = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(rawTriNA, rotA), float3(0.0f, 1.0f, 0.0f));
                const float3 wCA = (wA0 + wA1 + wA2) * (1.0f / 3.0f);

                for(uint tiB = 0u; tiB < triCountB; ++tiB){
                    const uint localTriB = firstTriB + tiB;
                    const uint triIdxB = triBaseB + localTriB;
                    const float4 bT0 = g_NwbPhysicsFloat4Buffers[meshTriSlot][triIdxB * 3u + 0u];
                    const float4 bT1 = g_NwbPhysicsFloat4Buffers[meshTriSlot][triIdxB * 3u + 1u];
                    const float4 bT2 = g_NwbPhysicsFloat4Buffers[meshTriSlot][triIdxB * 3u + 2u];
                    const float3 bv0 = bT0.xyz;
                    const float3 bv1 = bT1.xyz;
                    const float3 bv2 = bT2.xyz;
                    const bool closedB = (bT1.w != 0.0f);

                    const float3 b0InA = posBInA + nwbPhysicsQuatRotate(bv0, rotBToA);
                    const float3 b1InA = posBInA + nwbPhysicsQuatRotate(bv1, rotBToA);
                    const float3 b2InA = posBInA + nwbPhysicsQuatRotate(bv2, rotBToA);
                    const float3 tMinBInA = min(b0InA, min(b1InA, b2InA)) - marginVec;
                    const float3 tMaxBInA = max(b0InA, max(b1InA, b2InA)) + marginVec;
                    if(!nwbPhysicsAabbOverlaps(tMinA, tMaxA, tMinBInA, tMaxBInA))
                        continue;

                    const uint edgeFlagsB = asuint(bT0.w);
                    const float3 rawTriNB = nwbPhysicsSafeNormal(cross(bv1 - bv0, bv2 - bv0), float3(0.0f, 1.0f, 0.0f));
                    const float3 wB0 = originB + nwbPhysicsQuatRotate(bv0, rotB);
                    const float3 wB1 = originB + nwbPhysicsQuatRotate(bv1, rotB);
                    const float3 wB2 = originB + nwbPhysicsQuatRotate(bv2, rotB);
                    const float3 rawWTriNB = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(rawTriNB, rotB), float3(0.0f, 1.0f, 0.0f));
                    const float3 wCB = (wB0 + wB1 + wB2) * (1.0f / 3.0f);

                    float3 gjkPtA = float3(0.0f, 0.0f, 0.0f);
                    float3 gjkPtB = float3(0.0f, 0.0f, 0.0f);
                    float gjkDist = 0.0f;
                    bool gjkDeep = false;
                    nwbPhysicsRunGjkDistanceEx(proxyA, originA, rotA, true, wA0, wA1, wA2, proxyB, originB, rotB, true, wB0, wB1, wB2, hullVertexSlot, gjkPtA, gjkPtB, gjkDist, gjkDeep);

                    if(!gjkDeep && gjkDist > 1.0e-5f){
                        float sep = gjkDist - totalR;
                        if(sep > maxDist)
                            continue;
                        float3 nAtoB = nwbPhysicsSafeNormal(gjkPtB - gjkPtA, rawWTriNA);
                        if(closedA && dot(nAtoB, rawWTriNA) <= 0.0f)
                            continue;
                        if(closedB && dot(nAtoB, rawWTriNB) >= 0.0f)
                            continue;
                        const float3 wAOut = gjkPtA + nAtoB * rA;
                        const float3 wBOut = gjkPtB - nAtoB * rB;
                        if(sep < bestSep){
                            bestSep = sep;
                            chosenWorldN = nAtoB;
                        }
                        nwbPhysicsPushMeshCand16(wAOut, wBOut, nAtoB, sep, nwbPhysicsMakeFeatureId(3u, localTriA & 0xffu, 3u, localTriB & 0xffu), candWA, candWB, candNorm, candSep, candFeat, candCount);
                    }
                    else{
                        float3 wTriNA = rawWTriNA;
                        const float dA = dot(rawWTriNA, wA0);
                        const float b0A = dot(rawWTriNA, wB0);
                        const float b1A = dot(rawWTriNA, wB1);
                        const float b2A = dot(rawWTriNA, wB2);
                        const float minBOnA = min(b0A, min(b1A, b2A));
                        float sepA = minBOnA - dA - totalR;
                        if(!closedA){
                            const float maxBOnA = max(b0A, max(b1A, b2A));
                            const float sepANeg = dA - maxBOnA - totalR;
                            if(sepANeg > sepA){
                                sepA = sepANeg;
                                wTriNA = -rawWTriNA;
                            }
                        }
                        if(sepA > maxDist)
                            continue;

                        float3 nBtoA = -rawWTriNB;
                        const float dB = dot(nBtoA, wB0);
                        const float a0B = dot(nBtoA, wA0);
                        const float a1B = dot(nBtoA, wA1);
                        const float a2B = dot(nBtoA, wA2);
                        const float maxAOnB = max(a0B, max(a1B, a2B));
                        float sepB = dB - maxAOnB - totalR;
                        if(!closedB){
                            const float minAOnB = min(a0B, min(a1B, a2B));
                            const float sepBNeg = minAOnB - dB - totalR;
                            if(sepBNeg > sepB){
                                sepB = sepBNeg;
                                nBtoA = rawWTriNB;
                            }
                        }
                        if(sepB > maxDist)
                            continue;

                        const float3 edgesA[3] = { wA1 - wA0, wA2 - wA1, wA0 - wA2 };
                        const float3 vertsA[3] = { wA0, wA1, wA2 };
                        const float3 edgesB[3] = { wB1 - wB0, wB2 - wB1, wB0 - wB2 };
                        const float3 vertsB[3] = { wB0, wB1, wB2 };

                        float bestEdgeSep = -1.0e20f;
                        float3 bestEdgeAxis = wTriNA;
                        uint bestEA = 0u;
                        uint bestEB = 0u;
                        bool separated = false;
                        for(uint ei = 0u; ei < 3u; ++ei){
                            const float3 eA = edgesA[ei];
                            const float lenSqA = dot(eA, eA);
                            for(uint ej = 0u; ej < 3u; ++ej){
                                const float3 eB = edgesB[ej];
                                const float3 rawCross = cross(eA, eB);
                                const float lenSq = dot(rawCross, rawCross);
                                const float normTol = 1.0e-6f * max(lenSqA * dot(eB, eB), 1.0e-12f);
                                if(lenSq <= normTol)
                                    continue;
                                const float3 axis = rawCross * rsqrt(lenSq);
                                const float a0 = dot(axis, wA0);
                                const float a1 = dot(axis, wA1);
                                const float a2 = dot(axis, wA2);
                                const float minA = min(a0, min(a1, a2));
                                const float maxA = max(a0, max(a1, a2));
                                const float b0 = dot(axis, wB0);
                                const float b1 = dot(axis, wB1);
                                const float b2 = dot(axis, wB2);
                                const float minB = min(b0, min(b1, b2));
                                const float maxB = max(b0, max(b1, b2));
                                const float sepPos = minB - maxA - totalR;
                                const float sepNeg = minA - maxB - totalR;
                                float sepE = sepPos;
                                float3 orientedAxis = axis;
                                if(sepNeg > sepPos){
                                    sepE = sepNeg;
                                    orientedAxis = -axis;
                                }
                                if(sepE > maxDist){
                                    separated = true;
                                    break;
                                }
                                if(sepE > bestEdgeSep){
                                    bestEdgeSep = sepE;
                                    bestEdgeAxis = orientedAxis;
                                    bestEA = ei;
                                    bestEB = ej;
                                }
                            }
                            if(separated)
                                break;
                        }
                        if(separated)
                            continue;

                        uint winKind = 0u;
                        float winSep = sepA;
                        float3 winN = wTriNA;
                        if(sepB > winSep + 1.0e-4f){
                            winKind = 1u;
                            winSep = sepB;
                            winN = nBtoA;
                        }
                        if(bestEdgeSep > winSep + 5.0e-4f){
                            const bool internalA = (edgeFlagsA & (1u << bestEA)) != 0u;
                            const bool internalB = (edgeFlagsB & (1u << bestEB)) != 0u;
                            if(internalA && sepA >= bestEdgeSep - 0.05f){
                                winKind = 0u;
                                winSep = sepA;
                                winN = wTriNA;
                            }
                            else if(internalB && sepB >= bestEdgeSep - 0.05f){
                                winKind = 1u;
                                winSep = sepB;
                                winN = nBtoA;
                            }
                            else{
                                winKind = 2u;
                                winSep = bestEdgeSep;
                                winN = bestEdgeAxis;
                            }
                        }

                        if(closedA && dot(winN, rawWTriNA) <= 0.0f)
                            continue;
                        if(closedB && dot(winN, rawWTriNB) >= 0.0f)
                            continue;

                        if(winKind == 2u){
                            const float3 a0 = vertsA[bestEA];
                            const float3 a1 = vertsA[(bestEA + 1u) % 3u];
                            const float3 b0 = vertsB[bestEB];
                            const float3 b1 = vertsB[(bestEB + 1u) % 3u];
                            float sSeg = 0.0f;
                            float tSeg = 0.0f;
                            float3 cA = float3(0.0f, 0.0f, 0.0f);
                            float3 cB = float3(0.0f, 0.0f, 0.0f);
                            nwbPhysicsClosestPointsSegments(a0, a1, b0, b1, sSeg, tSeg, cA, cB);
                            const float3 wAOut = cA + winN * rA;
                            const float3 wBOut = cB - winN * rB;
                            if(winSep < bestSep){
                                bestSep = winSep;
                                chosenWorldN = winN;
                            }
                            nwbPhysicsPushMeshCand16(wAOut, wBOut, winN, winSep, nwbPhysicsMakeFeatureId(3u, (localTriA & 0xffu) ^ bestEA, 3u, (localTriB & 0xffu) ^ bestEB), candWA, candWB, candNorm, candSep, candFeat, candCount);
                        }
                        else if(winKind == 0u){
                            float4 poly0[16];
                            float4 poly1[16];
                            poly0[0] = float4(wB0, 0.0f);
                            poly0[1] = float4(wB1, 0.0f);
                            poly0[2] = float4(wB2, 0.0f);
                            uint polyCount = 3u;
                            for(uint i = 0u; i < 3u; ++i){
                                const float3 sideN = nwbPhysicsSafeNormal(cross(edgesA[i], rawWTriNA), float3(1.0f, 0.0f, 0.0f));
                                polyCount = nwbPhysicsClipPolygonPlane(poly0, polyCount, sideN, dot(sideN, vertsA[i]) + 5.0e-3f, i, poly1);
                                if(polyCount == 0u)
                                    break;
                                for(uint k = 0u; k < polyCount; ++k)
                                    poly0[k] = poly1[k];
                            }
                            for(uint k = 0u; k < polyCount; ++k){
                                const float3 ptB = poly0[k].xyz;
                                const float sep = dot(winN, ptB - wA0) - totalR;
                                if(sep > maxDist)
                                    continue;
                                const float3 wAOut = ptB - winN * (sep + rB);
                                const float3 wBOut = ptB - winN * rB;
                                if(sep < bestSep){
                                    bestSep = sep;
                                    chosenWorldN = winN;
                                }
                                nwbPhysicsPushMeshCand16(wAOut, wBOut, winN, sep, nwbPhysicsMakeFeatureId(3u, localTriA & 0xffu, 3u, (localTriB & 0xffu) ^ k), candWA, candWB, candNorm, candSep, candFeat, candCount);
                            }
                        }
                        else{
                            float4 poly0[16];
                            float4 poly1[16];
                            poly0[0] = float4(wA0, 0.0f);
                            poly0[1] = float4(wA1, 0.0f);
                            poly0[2] = float4(wA2, 0.0f);
                            uint polyCount = 3u;
                            for(uint i = 0u; i < 3u; ++i){
                                const float3 sideN = nwbPhysicsSafeNormal(cross(edgesB[i], rawWTriNB), float3(1.0f, 0.0f, 0.0f));
                                polyCount = nwbPhysicsClipPolygonPlane(poly0, polyCount, sideN, dot(sideN, vertsB[i]) + 5.0e-3f, i, poly1);
                                if(polyCount == 0u)
                                    break;
                                for(uint k = 0u; k < polyCount; ++k)
                                    poly0[k] = poly1[k];
                            }
                            for(uint k = 0u; k < polyCount; ++k){
                                const float3 ptA = poly0[k].xyz;
                                const float sep = dot(winN, wB0 - ptA) - totalR;
                                if(sep > maxDist)
                                    continue;
                                const float3 wAOut = ptA + winN * rA;
                                const float3 wBOut = ptA + winN * (sep + rA);
                                if(sep < bestSep){
                                    bestSep = sep;
                                    chosenWorldN = winN;
                                }
                                nwbPhysicsPushMeshCand16(wAOut, wBOut, winN, sep, nwbPhysicsMakeFeatureId(3u, (localTriA & 0xffu) ^ k, 3u, localTriB & 0xffu), candWA, candWB, candNorm, candSep, candFeat, candCount);
                            }
                        }
                    }
                }
            }
        }
    }

    if(candCount == 0u){
        const bool closedB = (g_NwbPhysicsFloat4Buffers[meshTriSlot][triBaseB * 3u + 1u].w != 0.0f);
        if(closedB){
            const float4 invRotB = float4(-rotB.xyz, rotB.w);
            const float3 posAInB = nwbPhysicsQuatRotate(originA - originB, invRotB);
            const float4 rotAToB = nwbPhysicsQuatNormalize(nwbPhysicsQuatMul(invRotB, rotA));
            const float3 rootAN0 = g_NwbPhysicsFloat4Buffers[meshNodeSlot][nodeBaseA * 2u + 0u].xyz;
            const float3 rootAN1 = g_NwbPhysicsFloat4Buffers[meshNodeSlot][nodeBaseA * 2u + 1u].xyz;
            const float3 cAInB = posAInB + nwbPhysicsQuatRotate((rootAN0 + rootAN1) * 0.5f, rotAToB);
            const float3 vAInB = posAInB + nwbPhysicsQuatRotate(g_NwbPhysicsFloat4Buffers[meshTriSlot][triBaseA * 3u + 0u].xyz, rotAToB);
            float3 probeB = float3(0.0f, 0.0f, 0.0f);
            bool overflow = false;
            bool insideB = nwbPhysicsIsPointInsideClosedMesh(nodeBaseB, triBaseB, meshNodeSlot, meshTriSlot, cAInB, overflow);
            if(overflow){
                result.pairInfo.w = 1u;
                return;
            }
            if(insideB){
                probeB = cAInB;
            }
            else{
                insideB = nwbPhysicsIsPointInsideClosedMesh(nodeBaseB, triBaseB, meshNodeSlot, meshTriSlot, vAInB, overflow);
                if(overflow){
                    result.pairInfo.w = 1u;
                    return;
                }
                if(insideB)
                    probeB = vAInB;
            }
            if(insideB){
                float3 closePt = float3(0.0f, 0.0f, 0.0f);
                float3 closeN = float3(0.0f, 1.0f, 0.0f);
                uint closeTri = 0u;
                float dist = 0.0f;
                const bool foundClosest = nwbPhysicsFindClosestPointOnClosedMesh(nodeBaseB, triBaseB, meshNodeSlot, meshTriSlot, probeB, closePt, closeN, closeTri, dist, overflow);
                if(overflow){
                    result.pairInfo.w = 1u;
                    return;
                }
                if(foundClosest){
                    const float3 worldOut = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(closeN, rotB), float3(0.0f, 1.0f, 0.0f));
                    const float3 nAtoB = -worldOut;
                    const float3 wProbe = originB + nwbPhysicsQuatRotate(probeB, rotB);
                    const float3 wSurf = originB + nwbPhysicsQuatRotate(closePt, rotB);
                    const float sep = -(dist + totalR);
                    const float3 wAOut = wProbe + nAtoB * rA;
                    const float3 wBOut = wSurf - nAtoB * rB;
                    if(sep <= maxDist){
                        chosenWorldN = nAtoB;
                        nwbPhysicsPushMeshCand16(wAOut, wBOut, nAtoB, sep, nwbPhysicsMakeFeatureId(3u, 0u, 3u, closeTri & 0xffu), candWA, candWB, candNorm, candSep, candFeat, candCount);
                    }
                }
            }
        }
        const bool closedA = (g_NwbPhysicsFloat4Buffers[meshTriSlot][triBaseA * 3u + 1u].w != 0.0f);
        if(candCount == 0u && closedA){
            const float3 rootBN0 = g_NwbPhysicsFloat4Buffers[meshNodeSlot][nodeBaseB * 2u + 0u].xyz;
            const float3 rootBN1 = g_NwbPhysicsFloat4Buffers[meshNodeSlot][nodeBaseB * 2u + 1u].xyz;
            const float3 cBInA = posBInA + nwbPhysicsQuatRotate((rootBN0 + rootBN1) * 0.5f, rotBToA);
            const float3 vBInA = posBInA + nwbPhysicsQuatRotate(g_NwbPhysicsFloat4Buffers[meshTriSlot][triBaseB * 3u + 0u].xyz, rotBToA);
            float3 probeA = float3(0.0f, 0.0f, 0.0f);
            bool overflow = false;
            bool insideA = nwbPhysicsIsPointInsideClosedMesh(nodeBaseA, triBaseA, meshNodeSlot, meshTriSlot, cBInA, overflow);
            if(overflow){
                result.pairInfo.w = 1u;
                return;
            }
            if(insideA){
                probeA = cBInA;
            }
            else{
                insideA = nwbPhysicsIsPointInsideClosedMesh(nodeBaseA, triBaseA, meshNodeSlot, meshTriSlot, vBInA, overflow);
                if(overflow){
                    result.pairInfo.w = 1u;
                    return;
                }
                if(insideA)
                    probeA = vBInA;
            }
            if(insideA){
                float3 closePt = float3(0.0f, 0.0f, 0.0f);
                float3 closeN = float3(0.0f, 1.0f, 0.0f);
                uint closeTri = 0u;
                float dist = 0.0f;
                const bool foundClosest = nwbPhysicsFindClosestPointOnClosedMesh(nodeBaseA, triBaseA, meshNodeSlot, meshTriSlot, probeA, closePt, closeN, closeTri, dist, overflow);
                if(overflow){
                    result.pairInfo.w = 1u;
                    return;
                }
                if(foundClosest){
                    const float3 nAtoB = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(closeN, rotA), float3(0.0f, 1.0f, 0.0f));
                    const float3 wProbe = originA + nwbPhysicsQuatRotate(probeA, rotA);
                    const float3 wSurf = originA + nwbPhysicsQuatRotate(closePt, rotA);
                    const float sep = -(dist + totalR);
                    const float3 wAOut = wSurf + nAtoB * rA;
                    const float3 wBOut = wProbe - nAtoB * rB;
                    if(sep <= maxDist){
                        chosenWorldN = nAtoB;
                        nwbPhysicsPushMeshCand16(wAOut, wBOut, nAtoB, sep, nwbPhysicsMakeFeatureId(3u, closeTri & 0xffu, 3u, 0u), candWA, candWB, candNorm, candSep, candFeat, candCount);
                    }
                }
            }
        }
    }

    if(candCount > 0u)
        nwbPhysicsFinalizeMeshCand16(candWA, candWB, candNorm, candSep, candFeat, candCount, chosenWorldN, bodyPosA, bodyPosB, result);
}

void nwbPhysicsCollideSphereCapsuleMeshPair(
    const NwbPhysicsGpuShapeProxy meshP,
    const float3 meshOrig,
    const float4 meshRot,
    const NwbPhysicsGpuShapeProxy otherP,
    const float3 otherOrig,
    const float4 otherRot,
    const float3 bodyPosA,
    const float3 bodyPosB,
    const bool meshIsA,
    const uint nodeBase,
    const uint triBase,
    const uint meshNodeSlot,
    const uint meshTriSlot,
    const uint hullVertexSlot,
    const float maxDist,
    inout NwbPhysicsGpuNarrowphaseManifold result
){
    const uint otherType = otherP.bodyIndexTypeShape.y;
    const float meshR = meshP.localCenterRadius.w;
    const float otherR = otherP.localCenterRadius.w;
    const float totalR = otherR + meshR;
    const float queryR = totalR + maxDist;

    const float4 meshInvRot = float4(-meshRot.xyz, meshRot.w);
    const float3 relOrig = nwbPhysicsQuatRotate(otherOrig - meshOrig, meshInvRot);
    const float4 relRot = nwbPhysicsQuatNormalize(nwbPhysicsQuatMul(meshInvRot, otherRot));

    float3 wOtherCenter = otherOrig + nwbPhysicsQuatRotate(otherP.localCenterRadius.xyz, otherRot);
    float3 queryMin = float3(0.0f, 0.0f, 0.0f);
    float3 queryMax = float3(0.0f, 0.0f, 0.0f);
    if(otherType == NWB_PHYSICS_SHAPE_TYPE_SPHERE){
        const float3 lc = relOrig + nwbPhysicsQuatRotate(otherP.localCenterRadius.xyz, relRot);
        queryMin = lc - float3(queryR, queryR, queryR);
        queryMax = lc + float3(queryR, queryR, queryR);
    }
    else{
        const float3 l0 = relOrig + nwbPhysicsQuatRotate(otherP.localCenterRadius.xyz, relRot);
        const float3 l1 = relOrig + nwbPhysicsQuatRotate(otherP.localExtentOrP1.xyz, relRot);
        wOtherCenter = otherOrig + nwbPhysicsQuatRotate((otherP.localCenterRadius.xyz + otherP.localExtentOrP1.xyz) * 0.5f, otherRot);
        queryMin = min(l0, l1) - float3(queryR, queryR, queryR);
        queryMax = max(l0, l1) + float3(queryR, queryR, queryR);
    }

    float bestSep = 1.0e20f;
    float3 chosenWorldN = float3(0.0f, -1.0f, 0.0f);
    float3 bestWorldOther = float3(0.0f, 0.0f, 0.0f);
    float3 bestWorldMesh = float3(0.0f, 0.0f, 0.0f);
    float3 bestTriV0 = float3(0.0f, 0.0f, 0.0f);
    float3 bestTriV1 = float3(0.0f, 0.0f, 0.0f);
    float3 bestTriV2 = float3(0.0f, 0.0f, 0.0f);
    float3 bestTriRawN = float3(0.0f, 1.0f, 0.0f);
    float3 bestNOtherToMesh = float3(0.0f, -1.0f, 0.0f);
    uint bestLocalTriIdx = 0u;
    bool foundSingle = false;
    bool anyClosedMesh = false;

    uint stack[64];
    uint sp = 0u;
    stack[sp++] = 0u;
    while(sp > 0u){
        const uint nodeRel = stack[--sp];
        const float4 n0 = g_NwbPhysicsFloat4Buffers[meshNodeSlot][(nodeBase + nodeRel) * 2u + 0u];
        const float4 n1 = g_NwbPhysicsFloat4Buffers[meshNodeSlot][(nodeBase + nodeRel) * 2u + 1u];
        if(!nwbPhysicsAabbOverlaps(n0.xyz, n1.xyz, queryMin, queryMax))
            continue;

        const uint meta = asuint(n1.w);
        if((meta & 0x80000000u) != 0u){
            const uint firstTri = asuint(n0.w);
            const uint triCount = meta & 0x7fffffffu;
            for(uint ti = 0u; ti < triCount; ++ti){
                const uint localTriIdx = firstTri + ti;
                const uint triIdx = triBase + localTriIdx;
                const float4 mt0 = g_NwbPhysicsFloat4Buffers[meshTriSlot][triIdx * 3u + 0u];
                const float4 mt1 = g_NwbPhysicsFloat4Buffers[meshTriSlot][triIdx * 3u + 1u];
                const float4 mt2 = g_NwbPhysicsFloat4Buffers[meshTriSlot][triIdx * 3u + 2u];
                const float3 v0 = mt0.xyz;
                const float3 v1 = mt1.xyz;
                const float3 v2 = mt2.xyz;
                const uint edgeFlags = asuint(mt0.w);
                const bool isClosed = (mt1.w != 0.0f);
                if(isClosed)
                    anyClosedMesh = true;
                const float3 triN = nwbPhysicsSafeNormal(cross(v1 - v0, v2 - v0), float3(0.0f, 1.0f, 0.0f));
                const float3 tMin = min(v0, min(v1, v2));
                const float3 tMax = max(v0, max(v1, v2));
                if(!nwbPhysicsAabbOverlaps(tMin, tMax, queryMin, queryMax))
                    continue;

                if(otherType == NWB_PHYSICS_SHAPE_TYPE_SPHERE){
                    const float3 lc = relOrig + nwbPhysicsQuatRotate(otherP.localCenterRadius.xyz, relRot);
                    if(isClosed && dot(triN, lc - v0) < -NWB_PHYSICS_LINEAR_SLOP)
                        continue;
                    float3 qL = nwbPhysicsClosestPointOnTriangle(lc, v0, v1, v2);
                    const float3 dL = qL - lc;
                    const float dSq = dot(dL, dL);
                    if(dSq > queryR * queryR)
                        continue;
                    const float dist = sqrt(dSq);
                    float sep = dist - totalR;
                    float3 nL = -triN;
                    if(dist > 1.0e-5f){
                        const float3 candN = dL / dist;
                        if(!isClosed && dot(candN, triN) > -0.95f)
                            nL = candN;
                        else if(isClosed && dot(candN, triN) < 0.0f)
                            nL = candN;
                    }
                    float3 smoothedMeshToSphere = -nL;
                    if(nwbPhysicsSmoothMeshNormal(triN, edgeFlags, qL, v0, v1, v2, -nL, smoothedMeshToSphere)){
                        const float planeDist = dot(smoothedMeshToSphere, lc - v0);
                        if(planeDist - totalR > maxDist)
                            continue;
                        sep = planeDist - totalR;
                        nL = -smoothedMeshToSphere;
                        qL = lc - smoothedMeshToSphere * planeDist;
                    }
                    if(isClosed && dot(nL, triN) >= 0.0f)
                        continue;
                    const float3 wN = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(nL, meshRot), float3(0.0f, -1.0f, 0.0f));
                    const float3 wc = otherOrig + nwbPhysicsQuatRotate(otherP.localCenterRadius.xyz, otherRot);
                    const float3 wQ = meshOrig + nwbPhysicsQuatRotate(qL, meshRot);
                    if(!foundSingle || sep < bestSep){
                        foundSingle = true;
                        bestSep = sep;
                        chosenWorldN = meshIsA ? -wN : wN;
                        bestNOtherToMesh = wN;
                        bestWorldOther = wc + wN * otherR;
                        bestWorldMesh = wQ - wN * meshR;
                        bestLocalTriIdx = localTriIdx;
                    }
                }
                else{
                    const float3 wV0 = meshOrig + nwbPhysicsQuatRotate(v0, meshRot);
                    const float3 wV1 = meshOrig + nwbPhysicsQuatRotate(v1, meshRot);
                    const float3 wV2 = meshOrig + nwbPhysicsQuatRotate(v2, meshRot);
                    float3 wTriN = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(triN, meshRot), float3(0.0f, 1.0f, 0.0f));
                    float3 triNLocal = triN;
                    if(!isClosed && dot(wOtherCenter - wV0, wTriN) < 0.0f){
                        wTriN = -wTriN;
                        triNLocal = -triN;
                    }
                    const float3 capL0 = relOrig + nwbPhysicsQuatRotate(otherP.localCenterRadius.xyz, relRot);
                    const float3 capL1 = relOrig + nwbPhysicsQuatRotate(otherP.localExtentOrP1.xyz, relRot);
                    const float capD0 = dot(triNLocal, capL0 - v0);
                    const float capD1 = dot(triNLocal, capL1 - v0);
                    if(isClosed && capD0 < -NWB_PHYSICS_LINEAR_SLOP && capD1 < -NWB_PHYSICS_LINEAR_SLOP)
                        continue;

                    float3 gjkPtOther = float3(0.0f, 0.0f, 0.0f);
                    float3 gjkPtTri = float3(0.0f, 0.0f, 0.0f);
                    float gjkDist = 0.0f;
                    bool gjkDeep = false;
                    nwbPhysicsRunGjkDistance(otherP, otherOrig, otherRot, otherP, meshOrig, meshRot, true, wV0, wV1, wV2, hullVertexSlot, gjkPtOther, gjkPtTri, gjkDist, gjkDeep);

                    if(!gjkDeep && gjkDist > 1.0e-5f){
                        float sep = gjkDist - totalR;
                        if(sep > maxDist)
                            continue;
                        float3 nOtherToMesh = nwbPhysicsSafeNormal(gjkPtTri - gjkPtOther, -wTriN);
                        const float3 localPtM = nwbPhysicsQuatInvRotate(gjkPtTri - meshOrig, meshRot);
                        const float3 rawMeshToOtherL = nwbPhysicsQuatInvRotate(-nOtherToMesh, meshRot);
                        float3 smoothedL = rawMeshToOtherL;
                        float3 wO = gjkPtOther + nOtherToMesh * otherR;
                        float3 wM = gjkPtTri - nOtherToMesh * meshR;
                        if(nwbPhysicsSmoothMeshNormal(triN, edgeFlags, localPtM, v0, v1, v2, rawMeshToOtherL, smoothedL)){
                            const float3 localPtO = nwbPhysicsQuatInvRotate(gjkPtOther - meshOrig, meshRot);
                            const float planeDist = dot(smoothedL, localPtO - v0);
                            if(planeDist - totalR > maxDist)
                                continue;
                            sep = planeDist - totalR;
                            nOtherToMesh = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(-smoothedL, meshRot), -wTriN);
                            wO = gjkPtOther + nOtherToMesh * otherR;
                            wM = wO + nOtherToMesh * sep;
                        }
                        if(isClosed && dot(nOtherToMesh, wTriN) >= 0.0f)
                            continue;
                        const float3 wNOut = meshIsA ? -nOtherToMesh : nOtherToMesh;
                        if(!foundSingle || sep < bestSep){
                            foundSingle = true;
                            bestSep = sep;
                            chosenWorldN = wNOut;
                            bestNOtherToMesh = nOtherToMesh;
                            bestWorldOther = wO;
                            bestWorldMesh = wM;
                            bestTriV0 = v0;
                            bestTriV1 = v1;
                            bestTriV2 = v2;
                            bestTriRawN = triN;
                            bestLocalTriIdx = localTriIdx;
                        }
                    }
                    else{
                        const float3 deepL = isClosed ? ((capD0 < capD1) ? capL0 : capL1) : (abs(capD1) < abs(capD0) ? capL1 : capL0);
                        const float sep = isClosed ? (min(capD0, capD1) - totalR) : (-min(abs(capD0), abs(capD1)) - totalR);
                        const float3 deepW = meshOrig + nwbPhysicsQuatRotate(deepL, meshRot);
                        const float3 nOtherToMesh = -wTriN;
                        const float3 wNOut = meshIsA ? -nOtherToMesh : nOtherToMesh;
                        if(!foundSingle || sep < bestSep){
                            foundSingle = true;
                            bestSep = sep;
                            chosenWorldN = wNOut;
                            bestNOtherToMesh = nOtherToMesh;
                            bestWorldOther = deepW + nOtherToMesh * otherR;
                            bestWorldMesh = deepW + nOtherToMesh * (otherR + sep);
                            bestTriV0 = v0;
                            bestTriV1 = v1;
                            bestTriV2 = v2;
                            bestTriRawN = triN;
                            bestLocalTriIdx = localTriIdx;
                        }
                    }
                }
            }
        }
        else{
            if(sp + 2u > 64u){
                result.pairInfo.w = 1u;
                return;
            }
            stack[sp++] = asuint(n0.w);
            stack[sp++] = meta & 0x7fffffffu;
        }
    }

    bool usedInterior = false;
    if(!foundSingle || bestSep > -otherR){
        const float4 firstTri1 = g_NwbPhysicsFloat4Buffers[meshTriSlot][triBase * 3u + 1u];
        if(anyClosedMesh || firstTri1.w != 0.0f){
            bool meshOverflow = false;
            if(otherType == NWB_PHYSICS_SHAPE_TYPE_SPHERE){
                const float3 lc = relOrig + nwbPhysicsQuatRotate(otherP.localCenterRadius.xyz, relRot);
                const bool insideSphere = nwbPhysicsIsPointInsideClosedMesh(nodeBase, triBase, meshNodeSlot, meshTriSlot, lc, meshOverflow);
                if(meshOverflow){
                    result.pairInfo.w = 1u;
                    return;
                }
                if(insideSphere){
                    float3 qL = float3(0.0f, 0.0f, 0.0f);
                    float3 triNL = float3(0.0f, 1.0f, 0.0f);
                    uint localTri = 0u;
                    float dist = 0.0f;
                    const bool foundClosest = nwbPhysicsFindClosestPointOnClosedMesh(nodeBase, triBase, meshNodeSlot, meshTriSlot, lc, qL, triNL, localTri, dist, meshOverflow);
                    if(meshOverflow){
                        result.pairInfo.w = 1u;
                        return;
                    }
                    if(foundClosest){
                        const float3 nL = -triNL;
                        const float sep = -dist - totalR;
                        if(sep <= maxDist && (!foundSingle || sep < bestSep)){
                            const float3 wN = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(nL, meshRot), float3(0.0f, -1.0f, 0.0f));
                            const float3 wc = otherOrig + nwbPhysicsQuatRotate(otherP.localCenterRadius.xyz, otherRot);
                            const float3 wQ = meshOrig + nwbPhysicsQuatRotate(qL, meshRot);
                            foundSingle = true;
                            usedInterior = true;
                            bestSep = sep;
                            chosenWorldN = meshIsA ? -wN : wN;
                            bestNOtherToMesh = wN;
                            bestWorldOther = wc + wN * otherR;
                            bestWorldMesh = wQ - wN * meshR;
                            bestLocalTriIdx = localTri;
                        }
                    }
                }
            }
            else{
                const float3 l0 = relOrig + nwbPhysicsQuatRotate(otherP.localCenterRadius.xyz, relRot);
                const float3 l1 = relOrig + nwbPhysicsQuatRotate(otherP.localExtentOrP1.xyz, relRot);
                const float3 lMid = (l0 + l1) * 0.5f;
                const float3 capPts[3] = { lMid, l0, l1 };
                for(uint pi = 0u; pi < 3u; ++pi){
                    const float3 pL = capPts[pi];
                    const bool insidePt = nwbPhysicsIsPointInsideClosedMesh(nodeBase, triBase, meshNodeSlot, meshTriSlot, pL, meshOverflow);
                    if(meshOverflow){
                        result.pairInfo.w = 1u;
                        return;
                    }
                    if(!insidePt)
                        continue;
                    float3 qL = float3(0.0f, 0.0f, 0.0f);
                    float3 triNL = float3(0.0f, 1.0f, 0.0f);
                    uint localTri = 0u;
                    float dist = 0.0f;
                    const bool foundClosest = nwbPhysicsFindClosestPointOnClosedMesh(nodeBase, triBase, meshNodeSlot, meshTriSlot, pL, qL, triNL, localTri, dist, meshOverflow);
                    if(meshOverflow){
                        result.pairInfo.w = 1u;
                        return;
                    }
                    if(foundClosest){
                        const float sep = -dist - totalR;
                        if(sep <= maxDist && (!foundSingle || sep < bestSep)){
                            const float3 nL = -triNL;
                            const float3 wN = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(nL, meshRot), float3(0.0f, -1.0f, 0.0f));
                            const float3 wP = meshOrig + nwbPhysicsQuatRotate(pL, meshRot);
                            const float3 wQ = meshOrig + nwbPhysicsQuatRotate(qL, meshRot);
                            foundSingle = true;
                            usedInterior = true;
                            bestSep = sep;
                            chosenWorldN = meshIsA ? -wN : wN;
                            bestNOtherToMesh = wN;
                            bestWorldOther = wP + wN * otherR;
                            bestWorldMesh = wQ - wN * meshR;
                            bestLocalTriIdx = localTri;
                        }
                    }
                }
            }
        }
    }
    if(foundSingle && !usedInterior && otherType == NWB_PHYSICS_SHAPE_TYPE_CAPSULE){
        const float3 nL = nwbPhysicsSafeNormal(nwbPhysicsQuatInvRotate(-bestNOtherToMesh, meshRot), bestTriRawN);
        if(abs(dot(nL, bestTriRawN)) > 0.99f){
            const float3 l0 = relOrig + nwbPhysicsQuatRotate(otherP.localCenterRadius.xyz, relRot);
            const float3 l1 = relOrig + nwbPhysicsQuatRotate(otherP.localExtentOrP1.xyz, relRot);
            const float3 segDir = l1 - l0;
            const float segLenSq = dot(segDir, segDir);
            if(segLenSq > 2.5e-5f && abs(dot(nL, segDir)) <= 0.1f * sqrt(segLenSq)){
                const float3 te0 = bestTriV1 - bestTriV0;
                const float3 te1 = bestTriV2 - bestTriV1;
                const float3 te2 = bestTriV0 - bestTriV2;
                const float3 sn0 = nwbPhysicsSafeNormal(cross(te0, bestTriRawN), float3(1.0f, 0.0f, 0.0f));
                const float3 sn1 = nwbPhysicsSafeNormal(cross(te1, bestTriRawN), float3(1.0f, 0.0f, 0.0f));
                const float3 sn2 = nwbPhysicsSafeNormal(cross(te2, bestTriRawN), float3(1.0f, 0.0f, 0.0f));
                float tMin = 0.0f;
                float tMax = 1.0f;
                nwbPhysicsClipSegmentTrianglePlane(l0, l1, sn0, dot(sn0, bestTriV0) + 1.0e-3f, tMin, tMax);
                nwbPhysicsClipSegmentTrianglePlane(l0, l1, sn1, dot(sn1, bestTriV1) + 1.0e-3f, tMin, tMax);
                nwbPhysicsClipSegmentTrianglePlane(l0, l1, sn2, dot(sn2, bestTriV2) + 1.0e-3f, tMin, tMax);
                if(tMin < tMax){
                    const float3 clipA = l0 + segDir * tMin;
                    const float3 clipB = l0 + segDir * tMax;
                    const float3 clipDelta = clipB - clipA;
                    if(dot(clipDelta, clipDelta) > 2.5e-5f){
                        const float dA = dot(nL, clipA - bestTriV0);
                        const float dB = dot(nL, clipB - bestTriV0);
                        const float sepA = dA - totalR;
                        const float sepB = dB - totalR;
                        if(sepA <= maxDist && sepB <= maxDist){
                            const float3 triPts[2] = { clipA, clipB };
                            const float triDs[2] = { dA, dB };
                            const float triSeps[2] = { sepA, sepB };
                            const float3 wPlaneN = meshIsA ? -chosenWorldN : chosenWorldN;
                            result.normalPad = float4(chosenWorldN, 0.0f);
                            for(uint k = 0u; k < 2u; ++k){
                                const float3 ptCapL = triPts[k];
                                const float3 ptMeshL = ptCapL - nL * triDs[k];
                                const float3 wCap = meshOrig + nwbPhysicsQuatRotate(ptCapL, meshRot) + wPlaneN * otherR;
                                const float3 wMesh = meshOrig + nwbPhysicsQuatRotate(ptMeshL, meshRot) - wPlaneN * meshR;
                                const float3 wAOut = meshIsA ? wMesh : wCap;
                                const float3 wBOut = meshIsA ? wCap : wMesh;
                                result.anchorAFeature[k] = float4(wAOut - bodyPosA, asfloat(nwbPhysicsMakeFeatureId(3u, k, 3u, bestLocalTriIdx & 0xffu)));
                                result.anchorBSeparation[k] = float4(wBOut - bodyPosB, triSeps[k]);
                            }
                            result.pairInfo.z = 2u;
                            return;
                        }
                    }
                }
            }
        }
    }

    if(foundSingle){
        const float3 wAOut = meshIsA ? bestWorldMesh : bestWorldOther;
        const float3 wBOut = meshIsA ? bestWorldOther : bestWorldMesh;
        result.pairInfo.z = 1u;
        result.normalPad = float4(chosenWorldN, 0.0f);
        const uint singleFeature = (otherType == NWB_PHYSICS_SHAPE_TYPE_SPHERE)
            ? nwbPhysicsMakeFeatureId(2u, 0u, 3u, bestLocalTriIdx & 0xffu)
            : nwbPhysicsMakeFeatureId(3u, 0u, 3u, bestLocalTriIdx & 0xffu);
        result.anchorAFeature[0] = float4(wAOut - bodyPosA, asfloat(singleFeature));
        result.anchorBSeparation[0] = float4(wBOut - bodyPosB, bestSep);
    }
}

void nwbPhysicsCollideMeshPair(
    const NwbPhysicsGpuShapeProxy proxyA,
    const float3 originA,
    const float4 rotA,
    const float3 bodyPosA,
    const NwbPhysicsGpuShapeProxy proxyB,
    const float3 originB,
    const float4 rotB,
    const float3 bodyPosB,
    const uint meshNodeSlot,
    const uint meshTriSlot,
    const uint hullVertexSlot,
    const float maxDist,
    inout NwbPhysicsGpuNarrowphaseManifold result
){
    const uint typeA = proxyA.bodyIndexTypeShape.y;
    const uint typeB = proxyB.bodyIndexTypeShape.y;
    if(typeA == NWB_PHYSICS_SHAPE_TYPE_MESH && typeB == NWB_PHYSICS_SHAPE_TYPE_MESH){
        nwbPhysicsCollideMeshMeshPair(proxyA, originA, rotA, bodyPosA, proxyB, originB, rotB, bodyPosB, meshNodeSlot, meshTriSlot, hullVertexSlot, maxDist, result);
        return;
    }

    const bool meshIsA = (typeA == NWB_PHYSICS_SHAPE_TYPE_MESH);
    const NwbPhysicsGpuShapeProxy meshP = meshIsA ? proxyA : proxyB;
    const NwbPhysicsGpuShapeProxy otherP = meshIsA ? proxyB : proxyA;
    const float3 meshOrig = meshIsA ? originA : originB;
    const float4 meshRot = meshIsA ? rotA : rotB;
    const float3 otherOrig = meshIsA ? originB : originA;
    const float4 otherRot = meshIsA ? rotB : rotA;

    const uint nodeBase = meshP.filterData.w;
    const uint triBase = asuint(meshP.aabbMaxPad.w);
    if(nodeBase == NWB_PHYSICS_INVALID_INDEX || triBase == NWB_PHYSICS_INVALID_INDEX){
        result.pairInfo.w = 1u;
        return;
    }

    const uint otherType = otherP.bodyIndexTypeShape.y;
    if(otherType == NWB_PHYSICS_SHAPE_TYPE_SPHERE || otherType == NWB_PHYSICS_SHAPE_TYPE_CAPSULE){
        nwbPhysicsCollideSphereCapsuleMeshPair(meshP, meshOrig, meshRot, otherP, otherOrig, otherRot, bodyPosA, bodyPosB, meshIsA, nodeBase, triBase, meshNodeSlot, meshTriSlot, hullVertexSlot, maxDist, result);
        return;
    }

    const float meshR = meshP.localCenterRadius.w;
    const float otherR = otherP.localCenterRadius.w;
    const float totalR = otherR + meshR;
    const float queryR = totalR + maxDist;

    const float4 meshInvRot = float4(-meshRot.xyz, meshRot.w);
    const float3 relOrig = nwbPhysicsQuatRotate(otherOrig - meshOrig, meshInvRot);
    const float4 relRot = nwbPhysicsQuatNormalize(nwbPhysicsQuatMul(meshInvRot, otherRot));

    const float3 wOtherCenter = otherOrig + nwbPhysicsQuatRotate(otherP.localCenterRadius.xyz, otherRot);
    const float4 localBoxRot = nwbPhysicsQuatNormalize(nwbPhysicsQuatMul(relRot, otherP.localRotation));
    const float3 lc = relOrig + nwbPhysicsQuatRotate(otherP.localCenterRadius.xyz, relRot);
    float3 axX;
    float3 axY;
    float3 axZ;
    nwbPhysicsQuatToAxes(localBoxRot, axX, axY, axZ);
    axX = abs(axX);
    axY = abs(axY);
    axZ = abs(axZ);
    const float3 h = otherP.localExtentOrP1.xyz;
    const float3 ext = axX * h.x + axY * h.y + axZ * h.z + float3(queryR, queryR, queryR);
    const float3 queryMin = lc - ext;
    const float3 queryMax = lc + ext;

    float bestSep = 1.0e20f;
    float3 chosenWorldN = float3(0.0f, -1.0f, 0.0f);
    bool anyClosedMesh = false;

    float3 candWA[16];
    float3 candWB[16];
    float3 candNorm[16];
    float candSep[16];
    uint candFeat[16];
    uint candCount = 0u;

    uint stack[64];
    uint sp = 0u;
    stack[sp++] = 0u;
    while(sp > 0u){
        const uint nodeRel = stack[--sp];
        const float4 n0 = g_NwbPhysicsFloat4Buffers[meshNodeSlot][(nodeBase + nodeRel) * 2u + 0u];
        const float4 n1 = g_NwbPhysicsFloat4Buffers[meshNodeSlot][(nodeBase + nodeRel) * 2u + 1u];
        if(!nwbPhysicsAabbOverlaps(n0.xyz, n1.xyz, queryMin, queryMax))
            continue;

        const uint meta = asuint(n1.w);
        if((meta & 0x80000000u) != 0u){
            const uint firstTri = asuint(n0.w);
            const uint triCount = meta & 0x7fffffffu;
            for(uint ti = 0u; ti < triCount; ++ti){
                const uint localTriIdx = firstTri + ti;
                const uint triIdx = triBase + localTriIdx;
                const float4 mt0 = g_NwbPhysicsFloat4Buffers[meshTriSlot][triIdx * 3u + 0u];
                const float4 mt1 = g_NwbPhysicsFloat4Buffers[meshTriSlot][triIdx * 3u + 1u];
                const float4 mt2 = g_NwbPhysicsFloat4Buffers[meshTriSlot][triIdx * 3u + 2u];
                const float3 v0 = mt0.xyz;
                const float3 v1 = mt1.xyz;
                const float3 v2 = mt2.xyz;
                const uint edgeFlags = asuint(mt0.w);
                const bool isClosed = (mt1.w != 0.0f);
                if(isClosed)
                    anyClosedMesh = true;
                const float3 triN = nwbPhysicsSafeNormal(cross(v1 - v0, v2 - v0), float3(0.0f, 1.0f, 0.0f));
                const float3 tMin = min(v0, min(v1, v2));
                const float3 tMax = max(v0, max(v1, v2));
                if(!nwbPhysicsAabbOverlaps(tMin, tMax, queryMin, queryMax))
                    continue;

                const float3 wV0 = meshOrig + nwbPhysicsQuatRotate(v0, meshRot);
                const float3 wV1 = meshOrig + nwbPhysicsQuatRotate(v1, meshRot);
                const float3 wV2 = meshOrig + nwbPhysicsQuatRotate(v2, meshRot);
                float3 wTriN = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(triN, meshRot), float3(0.0f, 1.0f, 0.0f));
                float3 triNLocal = triN;
                if(!isClosed && dot(wOtherCenter - wV0, wTriN) < 0.0f){
                    wTriN = -wTriN;
                    triNLocal = -triN;
                }

                float3 gjkPtOther = float3(0.0f, 0.0f, 0.0f);
                float3 gjkPtTri = float3(0.0f, 0.0f, 0.0f);
                float gjkDist = 0.0f;
                bool gjkDeep = false;
                nwbPhysicsRunGjkDistance(otherP, otherOrig, otherRot, otherP, meshOrig, meshRot, true, wV0, wV1, wV2, hullVertexSlot, gjkPtOther, gjkPtTri, gjkDist, gjkDeep);

                if(!gjkDeep && gjkDist > 1.0e-5f){
                    float sep = gjkDist - totalR;
                    if(sep > maxDist)
                        continue;
                    float3 nOtherToMesh = nwbPhysicsSafeNormal(gjkPtTri - gjkPtOther, -wTriN);
                    const float3 localPtM = nwbPhysicsQuatInvRotate(gjkPtTri - meshOrig, meshRot);
                    const float3 rawMeshToOtherL = nwbPhysicsQuatInvRotate(-nOtherToMesh, meshRot);
                    float3 smoothedL = rawMeshToOtherL;
                    float3 wO = gjkPtOther + nOtherToMesh * otherR;
                    float3 wM = gjkPtTri - nOtherToMesh * meshR;
                    if(nwbPhysicsSmoothMeshNormal(triN, edgeFlags, localPtM, v0, v1, v2, rawMeshToOtherL, smoothedL)){
                        const float3 localPtO = nwbPhysicsQuatInvRotate(gjkPtOther - meshOrig, meshRot);
                        const float planeDist = dot(smoothedL, localPtO - v0);
                        if(planeDist - totalR > maxDist)
                            continue;
                        sep = planeDist - totalR;
                        nOtherToMesh = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(-smoothedL, meshRot), -wTriN);
                        wO = gjkPtOther + nOtherToMesh * otherR;
                        wM = wO + nOtherToMesh * sep;
                    }
                    if(isClosed && dot(nOtherToMesh, wTriN) >= 0.0f)
                        continue;
                    const float3 wNOut = meshIsA ? -nOtherToMesh : nOtherToMesh;
                    if(sep < bestSep){
                        bestSep = sep;
                        chosenWorldN = wNOut;
                    }
                    const float3 wAOut = meshIsA ? wM : wO;
                    const float3 wBOut = meshIsA ? wO : wM;
                    nwbPhysicsPushMeshCand16(wAOut, wBOut, wNOut, sep, nwbPhysicsMakeFeatureId(4u, 0u, 3u, localTriIdx & 0xffu), candWA, candWB, candNorm, candSep, candFeat, candCount);
                }
                else{
                    const uint fCountO = nwbPhysicsHullFaceCount(otherP);
                    const uint eCountO = nwbPhysicsHullEdgeCount(otherP);
                    const uint vCountO = nwbPhysicsHullVertexCount(otherP);
                    if(fCountO == 0u || fCountO > NWB_PHYSICS_MAX_HULL_FACES || eCountO > NWB_PHYSICS_MAX_HULL_EDGES || vCountO == 0u || vCountO > NWB_PHYSICS_MAX_HULL_VERTICES){
                        result.pairInfo.w = 1u;
                        return;
                    }

                    uint tmpIdx = 0u;
                    const float3 supInB = nwbPhysicsSupportWorld(otherP, relOrig, relRot, hullVertexSlot, -triNLocal, tmpIdx);
                    const float sepTri = dot(triNLocal, supInB - v0) - totalR;
                    if(sepTri > maxDist)
                        continue;

                    float bestHullSep = -1.0e20f;
                    uint bestHullFace = 0u;
                    float3 bestHullN = float3(0.0f, 1.0f, 0.0f);
                    float bestHullOff = 0.0f;
                    bool separated = false;
                    for(uint fi = 0u; fi < fCountO; ++fi){
                        const float4 pl = nwbPhysicsHullLocalFacePlane(otherP, hullVertexSlot, fi);
                        const float3 fNLocal = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(pl.xyz, relRot), float3(0.0f, 1.0f, 0.0f));
                        const float fOff = pl.w + dot(fNLocal, relOrig);
                        const float d0 = dot(fNLocal, v0);
                        const float d1 = dot(fNLocal, v1);
                        const float d2 = dot(fNLocal, v2);
                        const float sepF = min(d0, min(d1, d2)) - fOff - totalR;
                        if(sepF > maxDist){
                            separated = true;
                            break;
                        }
                        if(sepF > bestHullSep){
                            bestHullSep = sepF;
                            bestHullFace = fi;
                            bestHullN = fNLocal;
                            bestHullOff = fOff;
                        }
                    }
                    if(separated)
                        continue;

                    const float3 te0 = v1 - v0;
                    const float3 te1 = v2 - v1;
                    const float3 te2 = v0 - v2;
                    float bestEdgeSep = -1.0e20f;
                    float3 bestEdgeAxis = float3(0.0f, 1.0f, 0.0f);
                    uint bestEdgeHullIdx = 0u;
                    uint bestEdgeTriIdx = 0u;

                    for(uint eA = 0u; eA < eCountO; ++eA){
                        const uint edgePack = nwbPhysicsHullEdgePacked(otherP, hullVertexSlot, eA);
                        const uint vA0Idx = edgePack & 0xffu;
                        const uint vA1Idx = (edgePack >> 8u) & 0xffu;
                        if(vA0Idx >= vCountO || vA1Idx >= vCountO)
                            continue;
                        const float3 eA0 = relOrig + nwbPhysicsQuatRotate(nwbPhysicsHullLocalVertex(otherP, hullVertexSlot, vA0Idx), relRot);
                        const float3 eA1 = relOrig + nwbPhysicsQuatRotate(nwbPhysicsHullLocalVertex(otherP, hullVertexSlot, vA1Idx), relRot);
                        const float3 dirA = eA1 - eA0;
                        const float dirALenSq = dot(dirA, dirA);

                        for(uint tk = 0u; tk < 3u; ++tk){
                            float3 dirB = te0;
                            if(tk == 1u)
                                dirB = te1;
                            else if(tk == 2u)
                                dirB = te2;
                            const float3 rawCross = cross(dirA, dirB);
                            const float lenSq = dot(rawCross, rawCross);
                            const float normTol = 1.0e-6f * max(dirALenSq * dot(dirB, dirB), 1.0e-12f);
                            if(lenSq <= normTol)
                                continue;

                            const float3 axis = rawCross * rsqrt(lenSq);
                            const float3 supPos = nwbPhysicsSupportWorld(otherP, relOrig, relRot, hullVertexSlot, axis, tmpIdx);
                            const float3 supNeg = nwbPhysicsSupportWorld(otherP, relOrig, relRot, hullVertexSlot, -axis, tmpIdx);
                            const float maxA = dot(axis, supPos);
                            const float minA = dot(axis, supNeg);
                            const float dB0 = dot(axis, v0);
                            const float dB1 = dot(axis, v1);
                            const float dB2 = dot(axis, v2);
                            const float minB = min(dB0, min(dB1, dB2));
                            const float maxB = max(dB0, max(dB1, dB2));

                            const float sepPos = minB - maxA - totalR;
                            const float sepNeg = minA - maxB - totalR;
                            float sepE = sepPos;
                            float3 orientedAxis = axis;
                            if(sepNeg > sepPos){
                                sepE = sepNeg;
                                orientedAxis = -axis;
                            }
                            if(sepE > maxDist){
                                separated = true;
                                break;
                            }
                            if(sepE > bestEdgeSep){
                                bestEdgeSep = sepE;
                                bestEdgeAxis = orientedAxis;
                                bestEdgeHullIdx = eA;
                                bestEdgeTriIdx = tk;
                            }
                        }
                        if(separated)
                            break;
                    }
                    if(separated)
                        continue;

                    uint winKind = 0u;
                    float winSep = sepTri;
                    float3 winNInB = -triNLocal;
                    if(bestHullSep > winSep + 1.0e-4f){
                        winKind = 1u;
                        winSep = bestHullSep;
                        winNInB = bestHullN;
                    }
                    if(bestEdgeSep > winSep + 5.0e-4f){
                        if((edgeFlags & (1u << bestEdgeTriIdx)) != 0u && sepTri >= bestEdgeSep - 0.05f){
                            winKind = 0u;
                            winSep = sepTri;
                            winNInB = -triNLocal;
                        }
                        else{
                            winKind = 2u;
                            winSep = bestEdgeSep;
                            winNInB = bestEdgeAxis;
                        }
                    }

                    if(isClosed && dot(winNInB, triNLocal) >= 0.0f)
                        continue;

                    const float3 worldWinN = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(winNInB, meshRot), -wTriN);
                    const float3 wNOut = meshIsA ? -worldWinN : worldWinN;

                    if(winKind == 2u){
                        const uint edgePack = nwbPhysicsHullEdgePacked(otherP, hullVertexSlot, bestEdgeHullIdx);
                        const uint vA0Idx = edgePack & 0xffu;
                        const uint vA1Idx = (edgePack >> 8u) & 0xffu;
                        const float3 eA0 = relOrig + nwbPhysicsQuatRotate(nwbPhysicsHullLocalVertex(otherP, hullVertexSlot, vA0Idx), relRot);
                        const float3 eA1 = relOrig + nwbPhysicsQuatRotate(nwbPhysicsHullLocalVertex(otherP, hullVertexSlot, vA1Idx), relRot);
                        float3 eB0 = v0;
                        float3 eB1 = v1;
                        if(bestEdgeTriIdx == 1u){
                            eB0 = v1;
                            eB1 = v2;
                        }
                        else if(bestEdgeTriIdx == 2u){
                            eB0 = v2;
                            eB1 = v0;
                        }
                        float sSeg = 0.0f;
                        float tSeg = 0.0f;
                        float3 cA = float3(0.0f, 0.0f, 0.0f);
                        float3 cB = float3(0.0f, 0.0f, 0.0f);
                        nwbPhysicsClosestPointsSegments(eA0, eA1, eB0, eB1, sSeg, tSeg, cA, cB);
                        const float3 wO = meshOrig + nwbPhysicsQuatRotate(cA, meshRot) + worldWinN * otherR;
                        const float3 wM = meshOrig + nwbPhysicsQuatRotate(cB, meshRot) - worldWinN * meshR;
                        if(winSep < bestSep){
                            bestSep = winSep;
                            chosenWorldN = wNOut;
                        }
                        const float3 wAOut = meshIsA ? wM : wO;
                        const float3 wBOut = meshIsA ? wO : wM;
                        nwbPhysicsPushMeshCand16(wAOut, wBOut, wNOut, winSep, nwbPhysicsMakeFeatureId(4u, bestEdgeHullIdx & 0xffu, 3u, (localTriIdx & 0xffu) ^ bestEdgeTriIdx), candWA, candWB, candNorm, candSep, candFeat, candCount);
                    }
                    else if(winKind == 1u){
                        float4 refPoly[16];
                        float3 refNUnused = bestHullN;
                        float refOff = 0.0f;
                        const uint refPolyCount = nwbPhysicsGatherHullWorldFacePoly(otherP, relOrig, relRot, hullVertexSlot, bestHullFace, refPoly, refNUnused, refOff);
                        if(refPolyCount < 3u){
                            result.pairInfo.w = 1u;
                            return;
                        }
                        float4 poly0[16];
                        float4 poly1[16];
                        poly0[0] = float4(v0, 0.0f);
                        poly0[1] = float4(v1, 0.0f);
                        poly0[2] = float4(v2, 0.0f);
                        uint polyCount = 3u;
                        for(uint i = 0u; i < refPolyCount; ++i){
                            const float3 rv0 = refPoly[i].xyz;
                            const float3 rv1 = refPoly[(i + 1u) % refPolyCount].xyz;
                            const float3 sideN = nwbPhysicsSafeNormal(cross(rv1 - rv0, refNUnused), float3(1.0f, 0.0f, 0.0f));
                            const float sideOff = dot(sideN, rv0);
                            polyCount = nwbPhysicsClipPolygonPlane(poly0, polyCount, sideN, sideOff, i, poly1);
                            if(polyCount == 0u)
                                break;
                            for(uint k = 0u; k < polyCount; ++k)
                                poly0[k] = poly1[k];
                        }
                        for(uint k = 0u; k < polyCount; ++k){
                            const float3 ptB = poly0[k].xyz;
                            const float sep = dot(bestHullN, ptB) - bestHullOff - totalR;
                            if(sep > maxDist)
                                continue;
                            const float3 wM = meshOrig + nwbPhysicsQuatRotate(ptB, meshRot) - worldWinN * meshR;
                            const float3 wO = meshOrig + nwbPhysicsQuatRotate(ptB - bestHullN * (sep + totalR - otherR), meshRot);
                            if(sep < bestSep){
                                bestSep = sep;
                                chosenWorldN = wNOut;
                            }
                            const float3 wAOut = meshIsA ? wM : wO;
                            const float3 wBOut = meshIsA ? wO : wM;
                            nwbPhysicsPushMeshCand16(wAOut, wBOut, wNOut, sep, nwbPhysicsMakeFeatureId(4u, bestHullFace & 0xffu, 3u, (localTriIdx & 0xffu) ^ k), candWA, candWB, candNorm, candSep, candFeat, candCount);
                        }
                    }
                    else{
                        uint incFaceIdx = 0u;
                        float minDot = 2.0f;
                        for(uint fi = 0u; fi < fCountO; ++fi){
                            const float4 lp = nwbPhysicsHullLocalFacePlane(otherP, hullVertexSlot, fi);
                            const float3 fNInB = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(lp.xyz, relRot), float3(0.0f, 1.0f, 0.0f));
                            const float d = dot(fNInB, triNLocal);
                            if(d < minDot){
                                minDot = d;
                                incFaceIdx = fi;
                            }
                        }
                        float4 poly0[16];
                        float4 poly1[16];
                        float3 incNUnused = triNLocal;
                        float incOffUnused = 0.0f;
                        uint polyCount = nwbPhysicsGatherHullWorldFacePoly(otherP, relOrig, relRot, hullVertexSlot, incFaceIdx, poly0, incNUnused, incOffUnused);
                        if(polyCount < 3u){
                            result.pairInfo.w = 1u;
                            return;
                        }
                        const float3 sn0 = nwbPhysicsSafeNormal(cross(te0, triN), float3(1.0f, 0.0f, 0.0f));
                        const float3 sn1 = nwbPhysicsSafeNormal(cross(te1, triN), float3(1.0f, 0.0f, 0.0f));
                        const float3 sn2 = nwbPhysicsSafeNormal(cross(te2, triN), float3(1.0f, 0.0f, 0.0f));
                        polyCount = nwbPhysicsClipPolygonPlane(poly0, polyCount, sn0, dot(sn0, v0) + 5.0e-3f, 0u, poly1);
                        if(polyCount > 0u){
                            polyCount = nwbPhysicsClipPolygonPlane(poly1, polyCount, sn1, dot(sn1, v1) + 5.0e-3f, 1u, poly0);
                            if(polyCount > 0u){
                                polyCount = nwbPhysicsClipPolygonPlane(poly0, polyCount, sn2, dot(sn2, v2) + 5.0e-3f, 2u, poly1);
                                for(uint k = 0u; k < polyCount; ++k){
                                    const float3 ptB = poly1[k].xyz;
                                    const float planeDist = dot(triNLocal, ptB - v0);
                                    const float sep = planeDist - totalR;
                                    if(sep > maxDist)
                                        continue;
                                    const float3 wO = meshOrig + nwbPhysicsQuatRotate(ptB, meshRot) + worldWinN * otherR;
                                    const float3 wM = meshOrig + nwbPhysicsQuatRotate(ptB - triNLocal * planeDist, meshRot) - worldWinN * meshR;
                                    if(sep < bestSep){
                                        bestSep = sep;
                                        chosenWorldN = wNOut;
                                    }
                                    const float3 wAOut = meshIsA ? wM : wO;
                                    const float3 wBOut = meshIsA ? wO : wM;
                                    nwbPhysicsPushMeshCand16(wAOut, wBOut, wNOut, sep, nwbPhysicsMakeFeatureId(4u, k & 0xffu, 3u, localTriIdx & 0xffu), candWA, candWB, candNorm, candSep, candFeat, candCount);
                                }
                            }
                        }
                    }
                }
            }
        }
        else{
            if(sp + 2u > 64u){
                result.pairInfo.w = 1u;
                return;
            }
            stack[sp++] = asuint(n0.w);
            stack[sp++] = meta & 0x7fffffffu;
        }
    }

    if(candCount == 0u || bestSep > 0.0f){
        const float4 firstTri1 = g_NwbPhysicsFloat4Buffers[meshTriSlot][triBase * 3u + 1u];
        if(anyClosedMesh || firstTri1.w != 0.0f){
            const float3 hullCenterL = relOrig + nwbPhysicsQuatRotate(otherP.localCenterRadius.xyz, relRot);
            bool meshOverflow = false;
            const bool insideHullCenter = nwbPhysicsIsPointInsideClosedMesh(nodeBase, triBase, meshNodeSlot, meshTriSlot, hullCenterL, meshOverflow);
            if(meshOverflow){
                result.pairInfo.w = 1u;
                return;
            }
            if(insideHullCenter){
                float3 qL = float3(0.0f, 0.0f, 0.0f);
                float3 triNL = float3(0.0f, 1.0f, 0.0f);
                uint localTri = 0u;
                float dist = 0.0f;
                const bool foundClosest = nwbPhysicsFindClosestPointOnClosedMesh(nodeBase, triBase, meshNodeSlot, meshTriSlot, hullCenterL, qL, triNL, localTri, dist, meshOverflow);
                if(meshOverflow){
                    result.pairInfo.w = 1u;
                    return;
                }
                if(foundClosest){
                    const float3 worldOut = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(triNL, meshRot), float3(0.0f, 1.0f, 0.0f));
                    const float3 wNOut = meshIsA ? worldOut : -worldOut;
                    uint tmpSupIdx = 0u;
                    const float3 supW = nwbPhysicsSupportWorld(otherP, otherOrig, otherRot, hullVertexSlot, -worldOut, tmpSupIdx);
                    const float3 closeW = meshOrig + nwbPhysicsQuatRotate(qL, meshRot);
                    const float depth = dot(closeW - supW, worldOut);
                    const float sep = -(max(depth, 0.0f) + totalR);
                    const float3 wO = supW - worldOut * otherR;
                    const float3 wM = closeW + worldOut * meshR;
                    const float3 wAOut = meshIsA ? wM : wO;
                    const float3 wBOut = meshIsA ? wO : wM;
                    if(candCount == 0u || sep < bestSep){
                        bestSep = sep;
                        chosenWorldN = wNOut;
                    }
                    nwbPhysicsPushMeshCand16(wAOut, wBOut, wNOut, sep, nwbPhysicsMakeFeatureId(4u, 0u, 3u, localTri & 0xffu), candWA, candWB, candNorm, candSep, candFeat, candCount);
                }
            }
        }
    }
    if(candCount > 0u)
        nwbPhysicsFinalizeMeshCand16(candWA, candWB, candNorm, candSep, candFeat, candCount, chosenWorldN, bodyPosA, bodyPosB, result);
}
#endif

#if NWB_PHYSICS_NP_PROFILE == 2
void nwbPhysicsEvalBulletSweep(
    const NwbPhysicsGpuBodySim body,
    const float t,
    out float3 outOrigin,
    out float4 outRot
){
    const float3 c0 = body.forceLinearDamping.xyz;
    const float3 c1 = body.positionInvMass.xyz;
    const float4 q0 = body.torqueAngularDamping;
    float4 q1 = body.rotation;
    if(dot(q0, q1) < 0.0f)
        q1 = -q1;
    outRot = nwbPhysicsQuatNormalize(lerp(q0, q1, t));
    const float3 c = lerp(c0, c1, t);
    const float3 localCenter = body.localCenterMinExtent.xyz;
    outOrigin = c - nwbPhysicsQuatRotate(localCenter, outRot);
}

float nwbPhysicsQuatAngle(const float4 q0, const float4 q1){
    const float d = abs(clamp(dot(q0, q1), -1.0f, 1.0f));
    return 2.0f * acos(d);
}

void nwbPhysicsSolveBulletToi(
    const uint pairIdx,
    const uint bodySimSlot,
    const uint shapeSlot,
    const uint pairSlot,
    const uint hullVertexSlot,
    const uint manifoldSlot,
    const uint meshNodeSlot,
    const uint meshTriSlot,
    const float linearSlop
){
    const NwbPhysicsGpuBroadphasePair pair = g_NwbPhysicsBroadphasePairBuffers[pairSlot][pairIdx];
    const NwbPhysicsGpuShapeProxy proxyA = g_NwbPhysicsShapeProxyBuffers[shapeSlot][pair.indices.x];
    const NwbPhysicsGpuShapeProxy proxyB = g_NwbPhysicsShapeProxyBuffers[shapeSlot][pair.indices.y];
    const NwbPhysicsGpuBodySim bodyA = g_NwbPhysicsBodySimBuffers[bodySimSlot][pair.indices.z];
    const NwbPhysicsGpuBodySim bodyB = g_NwbPhysicsBodySimBuffers[bodySimSlot][pair.indices.w];

    NwbPhysicsGpuNarrowphaseManifold result;
    result.pairInfo = uint4(pair.indices.x, pair.indices.y, 0u, 0u);
    result.normalPad = float4(0.0f, 1.0f, 0.0f, 1.0f);
    for(uint k = 0u; k < NWB_PHYSICS_MAX_MANIFOLD_POINTS; ++k){
        result.anchorAFeature[k] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        result.anchorBSeparation[k] = float4(0.0f, 0.0f, 0.0f, 0.0f);
    }

    const float rA = proxyA.localCenterRadius.w;
    const float rB = proxyB.localCenterRadius.w;
    const float totalRadius = rA + rB;
    const float targetDist = totalRadius + 0.25f * linearSlop;
    const float tolerance = 0.5f * linearSlop;
    const float3 dispA = bodyA.positionInvMass.xyz - bodyA.forceLinearDamping.xyz;
    const float3 dispB = bodyB.positionInvMass.xyz - bodyB.forceLinearDamping.xyz;
    const float3 linDisp = dispB - dispA;
    const float angSpeedBound =
        nwbPhysicsQuatAngle(bodyA.torqueAngularDamping, bodyA.rotation) * bodyA.forceLinearDamping.w
        + nwbPhysicsQuatAngle(bodyB.torqueAngularDamping, bodyB.rotation) * bodyB.forceLinearDamping.w;

    if(proxyB.bodyIndexTypeShape.y == NWB_PHYSICS_SHAPE_TYPE_MESH){
        const uint nodeBase = proxyB.filterData.w;
        const uint triBase = asuint(proxyB.aabbMaxPad.w);
        if(nodeBase == NWB_PHYSICS_INVALID_INDEX || triBase == NWB_PHYSICS_INVALID_INDEX){
            result.pairInfo.w = 2u;
            g_NwbPhysicsNarrowphaseManifoldBuffers[manifoldSlot][pairIdx] = result;
            return;
        }

        float3 queryMin = float3(1.0e20f, 1.0e20f, 1.0e20f);
        float3 queryMax = float3(-1.0e20f, -1.0e20f, -1.0e20f);
        const float sampleTs[3] = { 0.0f, 0.5f, 1.0f };
        for(uint si = 0u; si < 3u; ++si){
            const float st = sampleTs[si];
            float3 oAt;
            float4 rAt;
            nwbPhysicsEvalBulletSweep(bodyA, st, oAt, rAt);
            float3 oBt;
            float4 rBt;
            nwbPhysicsEvalBulletSweep(bodyB, st, oBt, rBt);
            const float3 cAt = lerp(bodyA.forceLinearDamping.xyz, bodyA.positionInvMass.xyz, st);
            const float4 bInvRot = float4(-rBt.xyz, rBt.w);
            const float3 cAInB = nwbPhysicsQuatRotate(cAt - oBt, bInvRot);
            queryMin = min(queryMin, cAInB);
            queryMax = max(queryMax, cAInB);
        }
        const float boundR = bodyA.forceLinearDamping.w + bodyB.forceLinearDamping.w + totalRadius + linearSlop + angSpeedBound;
        queryMin -= float3(boundR, boundR, boundR);
        queryMax += float3(boundR, boundR, boundR);

        float curMax = 1.0f;
        bool hasHit = false;
        uint stack[64];
        uint sp = 0u;
        stack[sp++] = 0u;
        while(sp > 0u){
            const uint nodeRel = stack[--sp];
            const float4 n0 = g_NwbPhysicsFloat4Buffers[meshNodeSlot][(nodeBase + nodeRel) * 2u + 0u];
            const float4 n1 = g_NwbPhysicsFloat4Buffers[meshNodeSlot][(nodeBase + nodeRel) * 2u + 1u];
            if(!nwbPhysicsAabbOverlaps(n0.xyz, n1.xyz, queryMin, queryMax))
                continue;
            const uint meta = asuint(n1.w);
            if((meta & 0x80000000u) != 0u){
                const uint firstTri = asuint(n0.w);
                const uint triCount = meta & 0x7fffffffu;
                for(uint ti = 0u; ti < triCount; ++ti){
                    const uint triIdx = triBase + firstTri + ti;
                    const float3 tv0 = g_NwbPhysicsFloat4Buffers[meshTriSlot][triIdx * 3u + 0u].xyz;
                    const float3 tv1 = g_NwbPhysicsFloat4Buffers[meshTriSlot][triIdx * 3u + 1u].xyz;
                    const float3 tv2 = g_NwbPhysicsFloat4Buffers[meshTriSlot][triIdx * 3u + 2u].xyz;
                    if(!nwbPhysicsAabbOverlaps(min(tv0, min(tv1, tv2)), max(tv0, max(tv1, tv2)), queryMin, queryMax))
                        continue;

                    float t1 = 0.0f;
                    bool atMax = false;
                    for(uint iter = 0u; iter <= NWB_PHYSICS_TOI_MAX_ITERATIONS; ++iter){
                        float3 oAt;
                        float4 rAt;
                        nwbPhysicsEvalBulletSweep(bodyA, t1, oAt, rAt);
                        float3 oBt;
                        float4 rBt;
                        nwbPhysicsEvalBulletSweep(bodyB, t1, oBt, rBt);
                        const float3 wV0 = oBt + nwbPhysicsQuatRotate(tv0, rBt);
                        const float3 wV1 = oBt + nwbPhysicsQuatRotate(tv1, rBt);
                        const float3 wV2 = oBt + nwbPhysicsQuatRotate(tv2, rBt);
                        float3 pA;
                        float3 pB;
                        float dist = 0.0f;
                        bool deep = false;
                        nwbPhysicsRunGjkDistance(proxyA, oAt, rAt, proxyB, oBt, rBt, true, wV0, wV1, wV2, hullVertexSlot, pA, pB, dist, deep);
                        if(deep)
                            dist = 0.0f;
                        if(dist <= targetDist + tolerance){
                            if(t1 > 0.0f && (t1 < curMax || (!hasHit && t1 <= curMax))){
                                curMax = t1;
                                hasHit = true;
                                const float3 n = nwbPhysicsSafeNormal(pB - pA, float3(0.0f, 1.0f, 0.0f));
                                const float3 wA = pA + n * rA;
                                const float3 wB = pB - n * rB;
                                result.pairInfo.z = 1u;
                                result.pairInfo.w = 0u;
                                result.normalPad = float4(n, t1);
                                result.anchorAFeature[0] = float4((wA + wB) * 0.5f, 0.0f);
                            }
                            else if(t1 == 0.0f && (dist < totalRadius - tolerance || (totalRadius <= tolerance && dist <= tolerance))){
                                if(!hasHit)
                                    result.pairInfo.w = 1u;
                            }
                            break;
                        }
                        if(atMax)
                            break;
                        const float3 n = nwbPhysicsSafeNormal(pB - pA, float3(0.0f, 1.0f, 0.0f));
                        const float approach = -dot(n, linDisp) + angSpeedBound;
                        if(approach <= 1.0e-6f)
                            break;
                        const float step = (dist - targetDist) / approach;
                        t1 += step <= 1.0e-6f ? (0.01f * curMax) : step;
                        if(t1 >= curMax){
                            if(curMax <= 0.0f)
                                break;
                            t1 = curMax;
                            atMax = true;
                        }
                    }
                }
            }
            else{
                if(sp + 2u > 64u){
                    result.pairInfo.w = 2u;
                    g_NwbPhysicsNarrowphaseManifoldBuffers[manifoldSlot][pairIdx] = result;
                    return;
                }
                stack[sp++] = asuint(n0.w);
                stack[sp++] = meta & 0x7fffffffu;
            }
        }
        g_NwbPhysicsNarrowphaseManifoldBuffers[manifoldSlot][pairIdx] = result;
        return;
    }

    float t1 = 0.0f;
    bool atMax = false;
    for(uint iter = 0u; iter <= NWB_PHYSICS_TOI_MAX_ITERATIONS; ++iter){
        float3 oAt;
        float4 rAt;
        nwbPhysicsEvalBulletSweep(bodyA, t1, oAt, rAt);
        float3 oBt;
        float4 rBt;
        nwbPhysicsEvalBulletSweep(bodyB, t1, oBt, rBt);
        float3 pA;
        float3 pB;
        float dist = 0.0f;
        bool deep = false;
        nwbPhysicsRunGjkDistance(proxyA, oAt, rAt, proxyB, oBt, rBt, false, float3(0,0,0), float3(0,0,0), float3(0,0,0), hullVertexSlot, pA, pB, dist, deep);
        if(deep)
            dist = 0.0f;
        if(dist <= targetDist + tolerance){
            const float3 n = nwbPhysicsSafeNormal(pB - pA, float3(0.0f, 1.0f, 0.0f));
            const float3 wA = pA + n * rA;
            const float3 wB = pB - n * rB;
            result.pairInfo.z = 1u;
            result.pairInfo.w = (t1 == 0.0f && (dist < totalRadius - tolerance || (totalRadius <= tolerance && dist <= tolerance))) ? 1u : 0u;
            result.normalPad = float4(n, t1);
            result.anchorAFeature[0] = float4((wA + wB) * 0.5f, 0.0f);
            g_NwbPhysicsNarrowphaseManifoldBuffers[manifoldSlot][pairIdx] = result;
            return;
        }
        if(atMax){
            g_NwbPhysicsNarrowphaseManifoldBuffers[manifoldSlot][pairIdx] = result;
            return;
        }
        const float3 n = nwbPhysicsSafeNormal(pB - pA, float3(0.0f, 1.0f, 0.0f));
        const float approach = -dot(n, linDisp) + angSpeedBound;
        if(approach <= 1.0e-6f){
            g_NwbPhysicsNarrowphaseManifoldBuffers[manifoldSlot][pairIdx] = result;
            return;
        }
        const float step = (dist - targetDist) / approach;
        t1 += step <= 1.0e-6f ? 0.01f : step;
        if(t1 >= 1.0f){
            t1 = 1.0f;
            atMax = true;
        }
    }
    g_NwbPhysicsNarrowphaseManifoldBuffers[manifoldSlot][pairIdx] = result;
}
#endif
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if NWB_PHYSICS_NP_PROFILE != 2
groupshared uint s_GroupManifoldCount;
groupshared uint s_GroupManifoldBase;
#endif


#if NWB_PHYSICS_NP_PROFILE != 2
[require(spvGroupNonUniform, spvGroupNonUniformBallot)]
#endif
[numthreads(NWB_PHYSICS_WORKGROUP_SIZE, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID, uint3 groupThreadID : SV_GroupThreadID){
    const uint idx = dispatchThreadID.x;
    const uint maxPairs = g_NwbPhysicsPushConstants.itemCount;
    const uint counterSlot = g_NwbPhysicsPushConstants.slots1.w;
    const uint bodySimSlot = g_NwbPhysicsPushConstants.slots0.x;
    const uint meshNodeSlot = g_NwbPhysicsPushConstants.slots0.y;
    const uint shapeSlot = g_NwbPhysicsPushConstants.slots0.z;
    const uint meshTriSlot = g_NwbPhysicsPushConstants.slots0.w;
    const uint pairSlot = g_NwbPhysicsPushConstants.slots1.x;
    const uint hullVertexSlot = g_NwbPhysicsPushConstants.slots1.y;
    const uint manifoldSlot = g_NwbPhysicsPushConstants.slots1.z;

#if NWB_PHYSICS_NP_PROFILE == 2
    if(idx >= maxPairs)
        return;
    nwbPhysicsSolveBulletToi(idx, bodySimSlot, shapeSlot, pairSlot, hullVertexSlot, manifoldSlot, meshNodeSlot, meshTriSlot, g_NwbPhysicsPushConstants.linearSlop);
#else
    uint pairCount = maxPairs;
    if(counterSlot != NWB_PHYSICS_INVALID_INDEX){
        const uint rawPrim = g_NwbPhysicsUintBuffers[counterSlot][0u];
        const uint rawComplex = g_NwbPhysicsUintBuffers[counterSlot][1u];
        const bool overflow = (rawPrim > maxPairs) || (rawComplex > (maxPairs - rawPrim));
#if NWB_PHYSICS_NP_PROFILE == 0
        pairCount = overflow ? 0u : rawPrim;
#else
        pairCount = overflow ? 0u : rawComplex;
#endif
    }

    bool keepManifold = false;
    NwbPhysicsGpuNarrowphaseManifold result;
    result.pairInfo = uint4(0u, 0u, 0u, 0u);
    result.normalPad = float4(0.0f, 1.0f, 0.0f, 0.0f);

    if(idx < pairCount){
#if NWB_PHYSICS_NP_PROFILE == 1
        const uint pairIndex = (counterSlot != NWB_PHYSICS_INVALID_INDEX) ? (maxPairs - 1u - idx) : idx;
#else
        const uint pairIndex = idx;
#endif
        const NwbPhysicsGpuBroadphasePair pair = g_NwbPhysicsBroadphasePairBuffers[pairSlot][pairIndex];
        const NwbPhysicsGpuShapeProxy proxyA = g_NwbPhysicsShapeProxyBuffers[shapeSlot][pair.indices.x];
        const NwbPhysicsGpuShapeProxy proxyB = g_NwbPhysicsShapeProxyBuffers[shapeSlot][pair.indices.y];

        const uint typeA = proxyA.bodyIndexTypeShape.y;
        const uint typeB = proxyB.bodyIndexTypeShape.y;
        const float speculativeDist = g_NwbPhysicsPushConstants.gravitySpeculative.w;
        const NwbPhysicsGpuBodyPose poseA = g_NwbPhysicsBodyPoseBuffers[bodySimSlot][pair.indices.z];
        const NwbPhysicsGpuBodyPose poseB = g_NwbPhysicsBodyPoseBuffers[bodySimSlot][pair.indices.w];

        const bool isSensorPair = ((proxyA.bodyIndexTypeShape.w | proxyB.bodyIndexTypeShape.w) & NWB_PHYSICS_SHAPE_FLAG_SENSOR) != 0u;
        const bool fastOrBulletPair = ((asuint(poseA.sweepDispFlags.w) | asuint(poseB.sweepDispFlags.w)) & NWB_PHYSICS_SHAPE_FLAG_FAST_OR_BULLET) != 0u;
        const float maxDist = isSensorPair ? 0.0f : speculativeDist;

        const float3 originA = poseA.originPad.xyz;
        const float4 rotA = poseA.rotation;
        const float3 originB = poseB.originPad.xyz;
        const float4 rotB = poseB.rotation;

        result.pairInfo = uint4(proxyA.bodyIndexTypeShape.z, proxyB.bodyIndexTypeShape.z, 0u, 0u);

#if NWB_PHYSICS_NP_PROFILE == 1
    if(typeA == NWB_PHYSICS_SHAPE_TYPE_MESH || typeB == NWB_PHYSICS_SHAPE_TYPE_MESH){
        nwbPhysicsCollideMeshPair(proxyA, originA, rotA, originA, proxyB, originB, rotB, originB, meshNodeSlot, meshTriSlot, hullVertexSlot, maxDist, result);
    }
    else if(typeA == NWB_PHYSICS_SHAPE_TYPE_HULL && typeB == NWB_PHYSICS_SHAPE_TYPE_HULL){
        nwbPhysicsSolveHullHullDeep(proxyA, originA, rotA, originA, proxyB, originB, rotB, originB, hullVertexSlot, maxDist, result);
    }
    else{
        const float totalR = proxyA.localCenterRadius.w + proxyB.localCenterRadius.w;
        float3 pA = float3(0.0f, 0.0f, 0.0f);
        float3 pB = float3(0.0f, 0.0f, 0.0f);
        float dist = 0.0f;
        bool hitDeep = false;
        nwbPhysicsRunGjkDistance(proxyA, originA, rotA, proxyB, originB, rotB, false, float3(0,0,0), float3(0,0,0), float3(0,0,0), hullVertexSlot, pA, pB, dist, hitDeep);

        if(typeA == NWB_PHYSICS_SHAPE_TYPE_SPHERE || typeB == NWB_PHYSICS_SHAPE_TYPE_SPHERE){
            if(hitDeep || dist <= 1.0e-5f){
                nwbPhysicsSolveSphereHullDeep(proxyA, originA, rotA, originA, proxyB, originB, rotB, originB, hullVertexSlot, maxDist, result);
            }
            else{
                const float sep = dist - totalR;
                if(sep <= maxDist){
                    const float3 n = (pB - pA) / dist;
                    result.pairInfo.z = 1u;
                    result.normalPad = float4(n, 0.0f);
                    result.anchorAFeature[0] = float4(pA + n * proxyA.localCenterRadius.w - originA, asfloat(20u));
                    result.anchorBSeparation[0] = float4(pB - n * proxyB.localCenterRadius.w - originB, sep);
                }
            }
        }
        else{
            if(hitDeep || dist <= 1.0e-5f){
                nwbPhysicsSolveCapsuleHullDeep(proxyA, originA, rotA, originA, proxyB, originB, rotB, originB, hullVertexSlot, maxDist, result);
            }
            else{
                const float sep = dist - totalR;
                if(sep <= maxDist){
                    const float3 n = (pB - pA) / dist;
                    bool handled = false;
                    if(
                        (typeA == NWB_PHYSICS_SHAPE_TYPE_CAPSULE || typeB == NWB_PHYSICS_SHAPE_TYPE_CAPSULE)
                        && (typeA == NWB_PHYSICS_SHAPE_TYPE_HULL || typeB == NWB_PHYSICS_SHAPE_TYPE_HULL)
                    ){
                        const bool capIsA = (typeA == NWB_PHYSICS_SHAPE_TYPE_CAPSULE);
                        const NwbPhysicsGpuShapeProxy pC = capIsA ? proxyA : proxyB;
                        const NwbPhysicsGpuShapeProxy pH = capIsA ? proxyB : proxyA;
                        const float3 origC = capIsA ? originA : originB;
                        const float4 qC = capIsA ? rotA : rotB;
                        const float3 origH = capIsA ? originB : originA;
                        const float4 qH = capIsA ? rotB : rotA;
                        const uint fCount = nwbPhysicsHullFaceCount(pH);
                        if(fCount == 0u || fCount > NWB_PHYSICS_MAX_HULL_FACES){
                            result.pairInfo.w = 1u;
                            handled = true;
                        }
                        else{
                            const float3 wP0 = origC + nwbPhysicsQuatRotate(pC.localCenterRadius.xyz, qC);
                            const float3 wP1 = origC + nwbPhysicsQuatRotate(pC.localExtentOrP1.xyz, qC);
                            const float3 l0 = nwbPhysicsQuatInvRotate(wP0 - origH, qH);
                            const float3 l1 = nwbPhysicsQuatInvRotate(wP1 - origH, qH);
                            float maxFaceSep = -1.0e20f;
                            uint bestFace = 0u;
                            float3 bestLocalN = float3(0.0f, 1.0f, 0.0f);
                            float bestFaceOff = 0.0f;
                            bool faceSeparated = false;
                            for(uint fi = 0u; fi < fCount; ++fi){
                                const float4 pl = nwbPhysicsHullLocalFacePlane(pH, hullVertexSlot, fi);
                                const float d0 = dot(pl.xyz, l0) - pl.w;
                                const float d1 = dot(pl.xyz, l1) - pl.w;
                                const float faceSep = min(d0, d1);
                                if(faceSep > totalR + maxDist){
                                    faceSeparated = true;
                                    break;
                                }
                                if(faceSep > maxFaceSep){
                                    maxFaceSep = faceSep;
                                    bestFace = fi;
                                    bestLocalN = pl.xyz;
                                    bestFaceOff = pl.w;
                                }
                            }
                            if(faceSeparated){
                                handled = true;
                            }
                            else{
                                const float3 worldOut = capIsA ? -n : n;
                                const float3 bestWorldFaceN = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(bestLocalN, qH), float3(0.0f, 1.0f, 0.0f));
                                if(dot(bestWorldFaceN, worldOut) > 0.995f && maxFaceSep <= totalR + maxDist){
                                    handled = nwbPhysicsTryClipCapsuleAgainstHullFace(
                                        capIsA,
                                        pH,
                                        origH,
                                        qH,
                                        originA,
                                        originB,
                                        l0,
                                        l1,
                                        pC.localCenterRadius.w,
                                        pH.localCenterRadius.w,
                                        totalR,
                                        bestFace,
                                        bestLocalN,
                                        bestFaceOff,
                                        hullVertexSlot,
                                        maxDist,
                                        result
                                    );
                                }
                            }
                        }
                    }
                    if(!handled){
                        result.pairInfo.z = 1u;
                        result.normalPad = float4(n, 0.0f);
                        result.anchorAFeature[0] = float4(pA + n * proxyA.localCenterRadius.w - originA, asfloat(20u));
                        result.anchorBSeparation[0] = float4(pB - n * proxyB.localCenterRadius.w - originB, sep);
                    }
                }
            }
        }
    }
#else
    const bool isBoxA = (typeA == NWB_PHYSICS_SHAPE_TYPE_HULL) && ((proxyA.bodyIndexTypeShape.w & NWB_PHYSICS_SHAPE_FLAG_IS_BOX) != 0u);
    const bool isBoxB = (typeB == NWB_PHYSICS_SHAPE_TYPE_HULL) && ((proxyB.bodyIndexTypeShape.w & NWB_PHYSICS_SHAPE_FLAG_IS_BOX) != 0u);
    if(typeA == NWB_PHYSICS_SHAPE_TYPE_SPHERE && typeB == NWB_PHYSICS_SHAPE_TYPE_SPHERE){
        const float3 cA = originA + nwbPhysicsQuatRotate(proxyA.localCenterRadius.xyz, rotA);
        const float3 cB = originB + nwbPhysicsQuatRotate(proxyB.localCenterRadius.xyz, rotB);
        const float rA = proxyA.localCenterRadius.w;
        const float rB = proxyB.localCenterRadius.w;
        const float3 d = cB - cA;
        const float distSq = dot(d, d);
        const float totalR = rA + rB;
        const float maxD = totalR + maxDist;
        if(distSq <= maxD * maxD){
            const float dist = sqrt(distSq);
            const float3 n = dist > 1.0e-6f ? (d / dist) : float3(0.0f, 1.0f, 0.0f);
            result.pairInfo.z = 1u;
            result.normalPad = float4(n, 0.0f);
            result.anchorAFeature[0] = float4(cA + n * rA - originA, asfloat(1u));
            result.anchorBSeparation[0] = float4(cB - n * rB - originB, dist - totalR);
        }
    }
    else if(typeA == NWB_PHYSICS_SHAPE_TYPE_SPHERE && typeB == NWB_PHYSICS_SHAPE_TYPE_CAPSULE){
        const float3 cA = originA + nwbPhysicsQuatRotate(proxyA.localCenterRadius.xyz, rotA);
        const float3 p0 = originB + nwbPhysicsQuatRotate(proxyB.localCenterRadius.xyz, rotB);
        const float3 p1 = originB + nwbPhysicsQuatRotate(proxyB.localExtentOrP1.xyz, rotB);
        const float3 q = nwbPhysicsClosestPointOnSegment(p0, p1, cA);
        const float rA = proxyA.localCenterRadius.w;
        const float rB = proxyB.localCenterRadius.w;
        const float3 d = q - cA;
        const float distSq = dot(d, d);
        const float totalR = rA + rB;
        const float maxD = totalR + maxDist;
        if(distSq <= maxD * maxD){
            const float dist = sqrt(distSq);
            const float3 n = dist > 1.0e-6f ? (d / dist) : float3(0.0f, 1.0f, 0.0f);
            result.pairInfo.z = 1u;
            result.normalPad = float4(n, 0.0f);
            result.anchorAFeature[0] = float4(cA + n * rA - originA, asfloat(2u));
            result.anchorBSeparation[0] = float4(q - n * rB - originB, dist - totalR);
        }
    }
    else if(typeA == NWB_PHYSICS_SHAPE_TYPE_CAPSULE && typeB == NWB_PHYSICS_SHAPE_TYPE_SPHERE){
        const float3 p0 = originA + nwbPhysicsQuatRotate(proxyA.localCenterRadius.xyz, rotA);
        const float3 p1 = originA + nwbPhysicsQuatRotate(proxyA.localExtentOrP1.xyz, rotA);
        const float3 cB = originB + nwbPhysicsQuatRotate(proxyB.localCenterRadius.xyz, rotB);
        const float3 q = nwbPhysicsClosestPointOnSegment(p0, p1, cB);
        const float rA = proxyA.localCenterRadius.w;
        const float rB = proxyB.localCenterRadius.w;
        const float3 d = cB - q;
        const float distSq = dot(d, d);
        const float totalR = rA + rB;
        const float maxD = totalR + maxDist;
        if(distSq <= maxD * maxD){
            const float dist = sqrt(distSq);
            const float3 n = dist > 1.0e-6f ? (d / dist) : float3(0.0f, 1.0f, 0.0f);
            result.pairInfo.z = 1u;
            result.normalPad = float4(n, 0.0f);
            result.anchorAFeature[0] = float4(q + n * rA - originA, asfloat(3u));
            result.anchorBSeparation[0] = float4(cB - n * rB - originB, dist - totalR);
        }
    }
    else if(typeA == NWB_PHYSICS_SHAPE_TYPE_CAPSULE && typeB == NWB_PHYSICS_SHAPE_TYPE_CAPSULE){
        const float3 a0 = originA + nwbPhysicsQuatRotate(proxyA.localCenterRadius.xyz, rotA);
        const float3 a1 = originA + nwbPhysicsQuatRotate(proxyA.localExtentOrP1.xyz, rotA);
        const float3 b0 = originB + nwbPhysicsQuatRotate(proxyB.localCenterRadius.xyz, rotB);
        const float3 b1 = originB + nwbPhysicsQuatRotate(proxyB.localExtentOrP1.xyz, rotB);
        const float3 dirA = a1 - a0;
        const float3 dirB = b1 - b0;
        const float lenSqA = dot(dirA, dirA);
        const float lenSqB = dot(dirB, dirB);
        float s = 0.0f;
        float t = 0.0f;
        float3 cA;
        float3 cB;
        nwbPhysicsClosestPointsSegments(a0, a1, b0, b1, s, t, cA, cB);
        const float rA = proxyA.localCenterRadius.w;
        const float rB = proxyB.localCenterRadius.w;
        const float3 d = cB - cA;
        const float distSq = dot(d, d);
        const float totalR = rA + rB;
        const float maxD = totalR + maxDist;
        if(distSq <= maxD * maxD){
            const float dist = sqrt(distSq);
            float3 fallback = float3(0.0f, 1.0f, 0.0f);
            if(lenSqA > 1.0e-12f){
                float3 t1;
                float3 t2;
                nwbPhysicsComputeBasis(dirA, t1, t2);
                fallback = t1;
            }
            const float3 n = dist > 1.0e-6f ? (d / dist) : fallback;
            result.normalPad = float4(n, 0.0f);
            if(lenSqA > 1.0e-12f && lenSqB > 1.0e-12f){
                const float lenA = sqrt(lenSqA);
                const float3 uA = dirA / lenA;
                const float3 uB = dirB / sqrt(lenSqB);
                if(abs(dot(uA, uB)) > 0.9995f){
                    const float pB1 = dot(b0 - a0, uA);
                    const float pB2 = dot(b1 - a0, uA);
                    const float overlapMin = max(0.0f, min(pB1, pB2));
                    const float overlapMax = min(lenA, max(pB1, pB2));
                    if(overlapMax - overlapMin > 1.0e-3f){
                        const float3 pA0 = a0 + uA * overlapMin;
                        const float3 pA1 = a0 + uA * overlapMax;
                        const float3 qB0 = pA0 + (cB - cA);
                        const float3 qB1 = pA1 + (cB - cA);
                        const float sep = dist - totalR;
                        result.pairInfo.z = 2u;
                        result.anchorAFeature[0] = float4(pA0 + n * rA - originA, asfloat(4u));
                        result.anchorBSeparation[0] = float4(qB0 - n * rB - originB, sep);
                        result.anchorAFeature[1] = float4(pA1 + n * rA - originA, asfloat(5u));
                        result.anchorBSeparation[1] = float4(qB1 - n * rB - originB, sep);
                    }
                }
            }
            if(result.pairInfo.z == 0u){
                result.pairInfo.z = 1u;
                result.anchorAFeature[0] = float4(cA + n * rA - originA, asfloat(4u));
                result.anchorBSeparation[0] = float4(cB - n * rB - originB, dist - totalR);
            }
        }
    }
    else if((typeA == NWB_PHYSICS_SHAPE_TYPE_SPHERE && isBoxB) || (isBoxA && typeB == NWB_PHYSICS_SHAPE_TYPE_SPHERE)){
        const bool sphereIsA = (typeA == NWB_PHYSICS_SHAPE_TYPE_SPHERE);
        const float3 cS = sphereIsA ? (originA + nwbPhysicsQuatRotate(proxyA.localCenterRadius.xyz, rotA)) : (originB + nwbPhysicsQuatRotate(proxyB.localCenterRadius.xyz, rotB));
        const float rS = sphereIsA ? proxyA.localCenterRadius.w : proxyB.localCenterRadius.w;
        const float3 cBox = sphereIsA ? (originB + nwbPhysicsQuatRotate(proxyB.localCenterRadius.xyz, rotB)) : (originA + nwbPhysicsQuatRotate(proxyA.localCenterRadius.xyz, rotA));
        const float4 qBox = sphereIsA ? nwbPhysicsQuatNormalize(nwbPhysicsQuatMul(rotB, proxyB.localRotation)) : nwbPhysicsQuatNormalize(nwbPhysicsQuatMul(rotA, proxyA.localRotation));
        const float3 hBox = sphereIsA ? proxyB.localExtentOrP1.xyz : proxyA.localExtentOrP1.xyz;
        const float rBox = sphereIsA ? proxyB.localCenterRadius.w : proxyA.localCenterRadius.w;

        const float3 localS = nwbPhysicsQuatInvRotate(cS - cBox, qBox);
        const float3 clamped = clamp(localS, -hBox, hBox);
        const float3 deltaLocal = localS - clamped;
        const float distSq = dot(deltaLocal, deltaLocal);
        const float totalR = rS + rBox;
        const float maxD = totalR + maxDist;
        if(distSq <= maxD * maxD){
            float3 localNormal = float3(0.0f, 1.0f, 0.0f);
            float separation = 0.0f;
            float3 worldBoxPt = cBox + nwbPhysicsQuatRotate(clamped, qBox);
            if(distSq > 1.0e-10f){
                const float dist = sqrt(distSq);
                localNormal = deltaLocal / dist;
                separation = dist - totalR;
            }
            else{
                const float3 dx = hBox - abs(localS);
                if(dx.x <= dx.y && dx.x <= dx.z){
                    localNormal = float3(localS.x >= 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f);
                    separation = -dx.x - totalR;
                    worldBoxPt = cBox + nwbPhysicsQuatRotate(float3(localNormal.x * hBox.x, localS.y, localS.z), qBox);
                }
                else if(dx.y <= dx.z){
                    localNormal = float3(0.0f, localS.y >= 0.0f ? 1.0f : -1.0f, 0.0f);
                    separation = -dx.y - totalR;
                    worldBoxPt = cBox + nwbPhysicsQuatRotate(float3(localS.x, localNormal.y * hBox.y, localS.z), qBox);
                }
                else{
                    localNormal = float3(0.0f, 0.0f, localS.z >= 0.0f ? 1.0f : -1.0f);
                    separation = -dx.z - totalR;
                    worldBoxPt = cBox + nwbPhysicsQuatRotate(float3(localS.x, localS.y, localNormal.z * hBox.z), qBox);
                }
            }
            const float3 outwardBoxNormal = nwbPhysicsQuatRotate(localNormal, qBox);
            const float3 nAtoB = sphereIsA ? -outwardBoxNormal : outwardBoxNormal;
            const float3 worldSpherePt = cS + (sphereIsA ? nAtoB : -nAtoB) * rS;
            const float3 finalBoxPt = worldBoxPt + outwardBoxNormal * rBox;
            result.pairInfo.z = 1u;
            result.normalPad = float4(nAtoB, 0.0f);
            result.anchorAFeature[0] = float4((sphereIsA ? worldSpherePt : finalBoxPt) - originA, asfloat(5u));
            result.anchorBSeparation[0] = float4((sphereIsA ? finalBoxPt : worldSpherePt) - originB, separation);
        }
    }
    else if(isBoxA && isBoxB){
        nwbPhysicsCollideBoxes(proxyA, originA, rotA, originA, proxyB, originB, rotB, originB, maxDist, result);
    }
    else if((typeA == NWB_PHYSICS_SHAPE_TYPE_CAPSULE && isBoxB) || (isBoxA && typeB == NWB_PHYSICS_SHAPE_TYPE_CAPSULE)){
        const bool capIsA = (typeA == NWB_PHYSICS_SHAPE_TYPE_CAPSULE);
        const float3 wP0 = capIsA ? (originA + nwbPhysicsQuatRotate(proxyA.localCenterRadius.xyz, rotA)) : (originB + nwbPhysicsQuatRotate(proxyB.localCenterRadius.xyz, rotB));
        const float3 wP1 = capIsA ? (originA + nwbPhysicsQuatRotate(proxyA.localExtentOrP1.xyz, rotA)) : (originB + nwbPhysicsQuatRotate(proxyB.localExtentOrP1.xyz, rotB));
        const float rCap = capIsA ? proxyA.localCenterRadius.w : proxyB.localCenterRadius.w;
        const float3 cBox = capIsA ? (originB + nwbPhysicsQuatRotate(proxyB.localCenterRadius.xyz, rotB)) : (originA + nwbPhysicsQuatRotate(proxyA.localCenterRadius.xyz, rotA));
        const float4 qBox = capIsA ? nwbPhysicsQuatNormalize(nwbPhysicsQuatMul(rotB, proxyB.localRotation)) : nwbPhysicsQuatNormalize(nwbPhysicsQuatMul(rotA, proxyA.localRotation));
        const float3 hBox = capIsA ? proxyB.localExtentOrP1.xyz : proxyA.localExtentOrP1.xyz;
        const float rBox = capIsA ? proxyB.localCenterRadius.w : proxyA.localCenterRadius.w;
        const float totalR = rCap + rBox;

        const float3 l0 = nwbPhysicsQuatInvRotate(wP0 - cBox, qBox);
        const float3 l1 = nwbPhysicsQuatInvRotate(wP1 - cBox, qBox);
        const float3 dSeg = l1 - l0;

        float ts[8];
        ts[0] = 0.0f;
        ts[1] = 1.0f;
        uint tCount = 2u;

        const float dArr[3] = { dSeg.x, dSeg.y, dSeg.z };
        const float lArr[3] = { l0.x, l0.y, l0.z };
        const float hArr[3] = { hBox.x, hBox.y, hBox.z };
        for(uint c = 0u; c < 3u; ++c){
            if(abs(dArr[c]) > 1.0e-8f){
                const float invD = 1.0f / dArr[c];
                const float tNeg = (-hArr[c] - lArr[c]) * invD;
                if(tNeg > 0.0f && tNeg < 1.0f)
                    ts[tCount++] = tNeg;
                const float tPos = (hArr[c] - lArr[c]) * invD;
                if(tPos > 0.0f && tPos < 1.0f)
                    ts[tCount++] = tPos;
            }
        }

        for(uint i = 1u; i < tCount; ++i){
            const float key = ts[i];
            uint j = i;
            while(j > 0u && ts[j - 1u] > key){
                ts[j] = ts[j - 1u];
                --j;
            }
            ts[j] = key;
        }

        float3 bestSegPt = l0;
        float3 bestBoxPt = clamp(l0, -hBox, hBox);
        float3 diff0 = bestSegPt - bestBoxPt;
        float bestDistSq = dot(diff0, diff0);

        const float3 endBoxPt = clamp(l1, -hBox, hBox);
        const float3 diff1 = l1 - endBoxPt;
        const float endDistSq = dot(diff1, diff1);
        if(endDistSq < bestDistSq){
            bestDistSq = endDistSq;
            bestSegPt = l1;
            bestBoxPt = endBoxPt;
        }

        for(uint i = 0u; i + 1u < tCount; ++i){
            const float ta = ts[i];
            const float tb = ts[i + 1u];
            if(tb - ta <= 1.0e-7f)
                continue;
            const float tMid = 0.5f * (ta + tb);
            const float3 pMid = l0 + dSeg * tMid;
            float aSum = 0.0f;
            float bSum = 0.0f;
            if(pMid.x > hBox.x){
                aSum += dSeg.x * dSeg.x;
                bSum += dSeg.x * (l0.x - hBox.x);
            }
            else if(pMid.x < -hBox.x){
                aSum += dSeg.x * dSeg.x;
                bSum += dSeg.x * (l0.x + hBox.x);
            }
            if(pMid.y > hBox.y){
                aSum += dSeg.y * dSeg.y;
                bSum += dSeg.y * (l0.y - hBox.y);
            }
            else if(pMid.y < -hBox.y){
                aSum += dSeg.y * dSeg.y;
                bSum += dSeg.y * (l0.y + hBox.y);
            }
            if(pMid.z > hBox.z){
                aSum += dSeg.z * dSeg.z;
                bSum += dSeg.z * (l0.z - hBox.z);
            }
            else if(pMid.z < -hBox.z){
                aSum += dSeg.z * dSeg.z;
                bSum += dSeg.z * (l0.z + hBox.z);
            }

            const float tCand = (aSum > 1.0e-12f) ? clamp(-bSum / aSum, ta, tb) : tMid;
            const float3 sp = l0 + dSeg * tCand;
            const float3 bp = clamp(sp, -hBox, hBox);
            const float3 diff = sp - bp;
            const float dSq = dot(diff, diff);
            if(dSq < bestDistSq){
                bestDistSq = dSq;
                bestSegPt = sp;
                bestBoxPt = bp;
            }
        }

        float3 outLocalN = float3(0.0f, 1.0f, 0.0f);
        float singleSep = 0.0f;
        float3 localBoxSurface = bestBoxPt;
        bool wonByEdge = false;
        bool edgeSeparated = false;
        if(bestDistSq > 1.0e-10f){
            const float dist = sqrt(bestDistSq);
            outLocalN = (bestSegPt - bestBoxPt) / dist;
            singleSep = dist - totalR;
        }
        else{
            float maxFaceSep = -1.0e20f;
            float3 bestLocalN = float3(0.0f, 1.0f, 0.0f);
            float bestMinD = 0.0f;
            float3 bestDeepPt = l0;
            for(uint k = 0u; k < 3u; ++k){
                for(uint s = 0u; s < 2u; ++s){
                    const float sgn = (s == 0u) ? 1.0f : -1.0f;
                    float d0 = 0.0f;
                    float d1 = 0.0f;
                    float3 fN = float3(0.0f, 0.0f, 0.0f);
                    if(k == 0u){ d0 = sgn * l0.x - hBox.x; d1 = sgn * l1.x - hBox.x; fN = float3(sgn, 0.0f, 0.0f); }
                    else if(k == 1u){ d0 = sgn * l0.y - hBox.y; d1 = sgn * l1.y - hBox.y; fN = float3(0.0f, sgn, 0.0f); }
                    else{ d0 = sgn * l0.z - hBox.z; d1 = sgn * l1.z - hBox.z; fN = float3(0.0f, 0.0f, sgn); }
                    const float faceSep = min(d0, d1);
                    if(faceSep > maxFaceSep){
                        maxFaceSep = faceSep;
                        bestLocalN = fN;
                        bestMinD = faceSep;
                        bestDeepPt = (d1 < d0) ? l1 : l0;
                    }
                }
            }
            outLocalN = bestLocalN;
            singleSep = bestMinD - totalR;
            localBoxSurface = bestDeepPt - bestLocalN * bestMinD;
            bestSegPt = bestDeepPt;

            const float segLenSqEdge = dot(dSeg, dSeg);
            const float3 segMid = (l0 + l1) * 0.5f;
            float bestEdgeSep = -1.0e20f;
            float3 bestEdgeAxis = float3(0.0f, 1.0f, 0.0f);
            uint bestEdgeK = 0u;
            bool hasEdgeCandidate = false;
            if(segLenSqEdge > 1.0e-10f){
                for(uint k = 0u; k < 3u; ++k){
                    float3 unitAxis = float3(0.0f, 0.0f, 0.0f);
                    if(k == 0u) unitAxis = float3(1.0f, 0.0f, 0.0f);
                    else if(k == 1u) unitAxis = float3(0.0f, 1.0f, 0.0f);
                    else unitAxis = float3(0.0f, 0.0f, 1.0f);
                    const float3 cr = cross(dSeg, unitAxis);
                    const float crLenSq = dot(cr, cr);
                    if(crLenSq <= 1.0e-8f * segLenSqEdge)
                        continue;
                    float3 axis = cr * rsqrt(crLenSq);
                    if(dot(axis, segMid) < 0.0f)
                        axis = -axis;
                    const float boxExtProj = abs(axis.x) * hBox.x + abs(axis.y) * hBox.y + abs(axis.z) * hBox.z;
                    const float edgeSep = dot(axis, l0) - boxExtProj;
                    if(edgeSep - totalR > maxDist){
                        edgeSeparated = true;
                        break;
                    }
                    if(!hasEdgeCandidate || edgeSep > bestEdgeSep){
                        hasEdgeCandidate = true;
                        bestEdgeSep = edgeSep;
                        bestEdgeAxis = axis;
                        bestEdgeK = k;
                    }
                }
            }
            if(!edgeSeparated && hasEdgeCandidate && bestEdgeSep > maxFaceSep + 5.0e-4f){
                wonByEdge = true;
                const float sx = (bestEdgeAxis.x < 0.0f) ? -hBox.x : hBox.x;
                const float sy = (bestEdgeAxis.y < 0.0f) ? -hBox.y : hBox.y;
                const float sz = (bestEdgeAxis.z < 0.0f) ? -hBox.z : hBox.z;
                float3 e0 = float3(sx, sy, sz);
                float3 e1 = float3(sx, sy, sz);
                if(bestEdgeK == 0u){ e0.x = -hBox.x; e1.x = hBox.x; }
                else if(bestEdgeK == 1u){ e0.y = -hBox.y; e1.y = hBox.y; }
                else{ e0.z = -hBox.z; e1.z = hBox.z; }
                float sSeg = 0.0f;
                float tSeg = 0.0f;
                nwbPhysicsClosestPointsSegments(l0, l1, e0, e1, sSeg, tSeg, bestSegPt, localBoxSurface);
                outLocalN = bestEdgeAxis;
                singleSep = bestEdgeSep - totalR;
            }
        }

        if(!edgeSeparated && singleSep <= maxDist){
            const float3 outWorldN = nwbPhysicsQuatRotate(outLocalN, qBox);
            const float3 nAtoB = capIsA ? -outWorldN : outWorldN;
            result.normalPad = float4(nAtoB, 0.0f);

            const float axX = abs(outLocalN.x);
            const float axY = abs(outLocalN.y);
            const float axZ = abs(outLocalN.z);
            uint emitted = 0u;
            if(!wonByEdge && max(axX, max(axY, axZ)) > 0.995f){
                float tMin = 0.0f;
                float tMax = 1.0f;
                if(axX >= axY && axX >= axZ){
                    nwbPhysicsClipIntervalAxis(l0.y, dSeg.y, hBox.y, tMin, tMax);
                    nwbPhysicsClipIntervalAxis(l0.z, dSeg.z, hBox.z, tMin, tMax);
                }
                else if(axY >= axZ){
                    nwbPhysicsClipIntervalAxis(l0.x, dSeg.x, hBox.x, tMin, tMax);
                    nwbPhysicsClipIntervalAxis(l0.z, dSeg.z, hBox.z, tMin, tMax);
                }
                else{
                    nwbPhysicsClipIntervalAxis(l0.x, dSeg.x, hBox.x, tMin, tMax);
                    nwbPhysicsClipIntervalAxis(l0.y, dSeg.y, hBox.y, tMin, tMax);
                }
                if(tMin <= tMax){
                    const float3 segDelta = dSeg * (tMax - tMin);
                    const uint candCount = (dot(segDelta, segDelta) > 2.5e-5f) ? 2u : 1u;
                    const float ts[2] = { tMin, tMax };
                    for(uint ci = 0u; ci < candCount; ++ci){
                        const float3 lSeg = l0 + dSeg * ts[ci];
                        float3 lFace = lSeg;
                        if(axX >= axY && axX >= axZ) lFace.x = outLocalN.x >= 0.0f ? hBox.x : -hBox.x;
                        else if(axY >= axZ) lFace.y = outLocalN.y >= 0.0f ? hBox.y : -hBox.y;
                        else lFace.z = outLocalN.z >= 0.0f ? hBox.z : -hBox.z;
                        const float3 wSeg = cBox + nwbPhysicsQuatRotate(lSeg, qBox);
                        const float3 wFace = cBox + nwbPhysicsQuatRotate(lFace, qBox);
                        const float3 capSurf = wSeg - outWorldN * rCap;
                        const float3 boxSurf = wFace + outWorldN * rBox;
                        const float ptSep = dot(capSurf - boxSurf, outWorldN);
                        if(ptSep <= maxDist){
                            const uint feat = (emitted == 0u) ? 6u : 7u;
                            result.anchorAFeature[emitted] = float4((capIsA ? capSurf : boxSurf) - originA, asfloat(feat));
                            result.anchorBSeparation[emitted] = float4((capIsA ? boxSurf : capSurf) - originB, ptSep);
                            ++emitted;
                        }
                    }
                }
            }
            if(emitted == 0u){
                const float3 wSeg = cBox + nwbPhysicsQuatRotate(bestSegPt, qBox);
                const float3 wBox = cBox + nwbPhysicsQuatRotate(localBoxSurface, qBox);
                const float3 capSurf = wSeg - outWorldN * rCap;
                const float3 boxSurf = wBox + outWorldN * rBox;
                result.anchorAFeature[0] = float4((capIsA ? capSurf : boxSurf) - originA, asfloat(6u));
                result.anchorBSeparation[0] = float4((capIsA ? boxSurf : capSurf) - originB, singleSep);
                emitted = 1u;
            }
            result.pairInfo.z = emitted;
        }
    }
    else{
        result.pairInfo.w = 1u;
    }
#endif

        keepManifold = (result.pairInfo.z > 0u) || (result.pairInfo.w != 0u) || fastOrBulletPair;
        if(keepManifold && result.pairInfo.z == 0u)
            result.normalPad = float4(0.0f, 1.0f, 0.0f, 0.0f);
    }

    if(counterSlot != NWB_PHYSICS_INVALID_INDEX){
        const uint waveCount = WaveActiveCountBits(keepManifold);
        const uint waveOffset = WavePrefixCountBits(keepManifold);
        const uint laneCount = WaveGetLaneCount();
        uint outIdx = 0u;
        if(laneCount >= NWB_PHYSICS_WORKGROUP_SIZE){
            uint waveBase = 0u;
            if(WaveIsFirstLane() && waveCount > 0u)
                InterlockedAdd(g_NwbPhysicsUintBuffers[counterSlot][2u], waveCount, waveBase);
            outIdx = WaveReadLaneFirst(waveBase) + waveOffset;
        }
        else{
            if(groupThreadID.x == 0u)
                s_GroupManifoldCount = 0u;
            GroupMemoryBarrierWithGroupSync();

            uint waveGroupOff = 0u;
            if(WaveIsFirstLane() && waveCount > 0u)
                InterlockedAdd(s_GroupManifoldCount, waveCount, waveGroupOff);
            waveGroupOff = WaveReadLaneFirst(waveGroupOff);
            GroupMemoryBarrierWithGroupSync();

            if(groupThreadID.x == 0u && s_GroupManifoldCount > 0u)
                InterlockedAdd(g_NwbPhysicsUintBuffers[counterSlot][2u], s_GroupManifoldCount, s_GroupManifoldBase);
            GroupMemoryBarrierWithGroupSync();

            outIdx = s_GroupManifoldBase + waveGroupOff + waveOffset;
        }

        if(keepManifold && outIdx < maxPairs){
            g_NwbPhysicsNarrowphaseManifoldBuffers[manifoldSlot][outIdx].pairInfo = result.pairInfo;
            g_NwbPhysicsNarrowphaseManifoldBuffers[manifoldSlot][outIdx].normalPad = result.normalPad;
            const uint emitCount = min(result.pairInfo.z, NWB_PHYSICS_MAX_MANIFOLD_POINTS);
            for(uint k = 0u; k < emitCount; ++k){
                g_NwbPhysicsNarrowphaseManifoldBuffers[manifoldSlot][outIdx].anchorAFeature[k] = result.anchorAFeature[k];
                g_NwbPhysicsNarrowphaseManifoldBuffers[manifoldSlot][outIdx].anchorBSeparation[k] = result.anchorBSeparation[k];
            }
        }
    }
    else if(keepManifold){
        g_NwbPhysicsNarrowphaseManifoldBuffers[manifoldSlot][idx].pairInfo = result.pairInfo;
        g_NwbPhysicsNarrowphaseManifoldBuffers[manifoldSlot][idx].normalPad = result.normalPad;
        const uint emitCount = min(result.pairInfo.z, NWB_PHYSICS_MAX_MANIFOLD_POINTS);
        for(uint k = 0u; k < emitCount; ++k){
            g_NwbPhysicsNarrowphaseManifoldBuffers[manifoldSlot][idx].anchorAFeature[k] = result.anchorAFeature[k];
            g_NwbPhysicsNarrowphaseManifoldBuffers[manifoldSlot][idx].anchorBSeparation[k] = result.anchorBSeparation[k];
        }
    }
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

