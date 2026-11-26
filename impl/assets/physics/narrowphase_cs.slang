// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "physics_common.slangi"

#ifndef NWB_PHYSICS_NP_PROFILE
#define NWB_PHYSICS_NP_PROFILE 0
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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

uint nwbPhysicsMakeFeatureId(const uint o1, const uint i1, const uint o2, const uint i2){
    return ((o1 & 0xffu) << 24u) | ((i1 & 0xffu) << 16u) | ((o2 & 0xffu) << 8u) | (i2 & 0xffu);
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

void nwbPhysicsUpdateFourthManifoldPoint(
    const float a0,
    const float a1,
    const float a2,
    const float baseSign,
    const uint idx,
    inout float bestFallbackArea,
    inout uint fallbackIdx3,
    inout float bestOutsideArea,
    inout uint idx3
){
    const float maxAbs = max(abs(a0), max(abs(a1), abs(a2)));
    if(maxAbs > bestFallbackArea){
        bestFallbackArea = maxAbs;
        fallbackIdx3 = idx;
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
        idx3 = idx;
    }
}

#if NWB_PHYSICS_NP_PROFILE == 0
uint nwbPhysicsClipBox2DPlaneAxis8(
    const float4 inPts[8],
    const uint inCount,
    const uint axisCoord,
    const float signVal,
    const float planeOffset,
    const uint planeId,
    inout float4 outPts[8]
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
            nwbPhysicsUpdateFourthManifoldPoint(a0, a1, a2, baseSign, i, bestFallbackArea, fallbackIdx3, bestOutsideArea, idx3);
        }
        if(bestOutsideArea < 0.0f)
            idx3 = fallbackIdx3;

        outIndices[0] = idx0;
        outIndices[1] = idx1;
        outIndices[2] = idx2;
        outIndices[3] = idx3;
        outCount = NWB_PHYSICS_MAX_MANIFOLD_POINTS;
    }

    const float incR = refIsA ? rB : rA;
    [unroll]
    for(uint k = 0u; k < NWB_PHYSICS_MAX_MANIFOLD_POINTS; ++k){
        if(k < outCount){
            const uint ci = outIndices[k];
            const float4 pt = candPts[ci];
            const float sep = pt.z - totalR;
            const float3 worldPt = refFaceCenter + uAxis * pt.x + vAxis * pt.y + clipRefN * pt.z;
            const float3 wRef = worldPt - clipRefN * (sep + incR);
            const float3 wInc = worldPt - clipRefN * incR;
            const float3 wA = refIsA ? wRef : wInc;
            const float3 wB = refIsA ? wInc : wRef;
            result.anchorAFeature[k] = float4(wA - bodyPosA, pt.w);
            result.anchorBSeparation[k] = float4(wB - bodyPosB, sep);
        }
    }
    result.pairInfo.z = outCount;
}

bool nwbPhysicsTestBoxEdgeAxis(
    const float t1,
    const float t2,
    const float3 axisA1,
    const float3 axisA2,
    const float c1,
    const float c2,
    const float hA1,
    const float ar2j,
    const float hA2,
    const float ar1j,
    const float hB1,
    const float arij2,
    const float hB2,
    const float arij1,
    const float totalR,
    const float maxDist,
    const int axisIdx,
    inout float bestSep,
    inout int bestAxis,
    inout float3 bestNormal
){
    const float lenSq = c1 * c1 + c2 * c2;
    if(lenSq < 1.0e-5f)
        return true;
    const float invLen = rsqrt(lenSq);
    const float signedProj = t2 * c1 - t1 * c2;
    const float rAU = hA1 * ar2j + hA2 * ar1j;
    const float rBU = hB1 * arij2 + hB2 * arij1;
    const float sep = (abs(signedProj) - (rAU + rBU)) * invLen - totalR;
    if(sep > maxDist)
        return false;
    if(sep > bestSep + 5.0e-4f){
        bestSep = sep;
        bestAxis = axisIdx;
        float3 n = (axisA2 * c1 - axisA1 * c2) * invLen;
        if(signedProj < 0.0f)
            n = -n;
        bestNormal = n;
    }
    return true;
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
    const float4 qA = (proxyA.localRotationOrMeta.w >= 0.999999f)
        ? rotA
        : nwbPhysicsQuatNormalize(nwbPhysicsQuatMul(rotA, proxyA.localRotationOrMeta));
    const float3 hA = proxyA.localExtentOrP1.xyz;
    const float rA = proxyA.localCenterRadius.w;

    const float3 cB = originB + nwbPhysicsQuatRotate(proxyB.localCenterRadius.xyz, rotB);
    const float4 qB = (proxyB.localRotationOrMeta.w >= 0.999999f)
        ? rotB
        : nwbPhysicsQuatNormalize(nwbPhysicsQuatMul(rotB, proxyB.localRotationOrMeta));
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

    const float ar00 = abs(r00);
    const float ar01 = abs(r01);
    const float ar02 = abs(r02);
    const float ar10 = abs(r10);
    const float ar11 = abs(r11);
    const float ar12 = abs(r12);
    const float ar20 = abs(r20);
    const float ar21 = abs(r21);
    const float ar22 = abs(r22);

    int bestAxis = -1;
    float bestSep = -1.0e20f;
    float bestSignedProj = 0.0f;
    float3 bestNormal = float3(0.0f, 1.0f, 0.0f);

    {
        const float sep = abs(tVec.x) - (hA.x + hB.x * ar00 + hB.y * ar01 + hB.z * ar02) - totalR;
        if(sep > maxDist) return;
        if(sep > bestSep){ bestSep = sep; bestAxis = 0; bestSignedProj = tVec.x; bestNormal = (tVec.x < 0.0f) ? -axA : axA; }
    }
    {
        const float sep = abs(tVec.y) - (hA.y + hB.x * ar10 + hB.y * ar11 + hB.z * ar12) - totalR;
        if(sep > maxDist) return;
        if(sep > bestSep){ bestSep = sep; bestAxis = 1; bestSignedProj = tVec.y; bestNormal = (tVec.y < 0.0f) ? -ayA : ayA; }
    }
    {
        const float sep = abs(tVec.z) - (hA.z + hB.x * ar20 + hB.y * ar21 + hB.z * ar22) - totalR;
        if(sep > maxDist) return;
        if(sep > bestSep){ bestSep = sep; bestAxis = 2; bestSignedProj = tVec.z; bestNormal = (tVec.z < 0.0f) ? -azA : azA; }
    }

    {
        const float signedProj = tVec.x * r00 + tVec.y * r10 + tVec.z * r20;
        const float sep = abs(signedProj) - (hA.x * ar00 + hA.y * ar10 + hA.z * ar20 + hB.x) - totalR;
        if(sep > maxDist) return;
        if(sep > bestSep + 1.0e-4f){ bestSep = sep; bestAxis = 3; bestSignedProj = signedProj; bestNormal = (signedProj < 0.0f) ? -axB : axB; }
    }
    {
        const float signedProj = tVec.x * r01 + tVec.y * r11 + tVec.z * r21;
        const float sep = abs(signedProj) - (hA.x * ar01 + hA.y * ar11 + hA.z * ar21 + hB.y) - totalR;
        if(sep > maxDist) return;
        if(sep > bestSep + 1.0e-4f){ bestSep = sep; bestAxis = 4; bestSignedProj = signedProj; bestNormal = (signedProj < 0.0f) ? -ayB : ayB; }
    }
    {
        const float signedProj = tVec.x * r02 + tVec.y * r12 + tVec.z * r22;
        const float sep = abs(signedProj) - (hA.x * ar02 + hA.y * ar12 + hA.z * ar22 + hB.z) - totalR;
        if(sep > maxDist) return;
        if(sep > bestSep + 1.0e-4f){ bestSep = sep; bestAxis = 5; bestSignedProj = signedProj; bestNormal = (signedProj < 0.0f) ? -azB : azB; }
    }

    if(!nwbPhysicsTestBoxEdgeAxis(tVec.y, tVec.z, ayA, azA, r10, r20, hA.y, ar20, hA.z, ar10, hB.y, ar02, hB.z, ar01, totalR, maxDist, 6, bestSep, bestAxis, bestNormal)) return;
    if(!nwbPhysicsTestBoxEdgeAxis(tVec.y, tVec.z, ayA, azA, r11, r21, hA.y, ar21, hA.z, ar11, hB.z, ar00, hB.x, ar02, totalR, maxDist, 7, bestSep, bestAxis, bestNormal)) return;
    if(!nwbPhysicsTestBoxEdgeAxis(tVec.y, tVec.z, ayA, azA, r12, r22, hA.y, ar22, hA.z, ar12, hB.x, ar01, hB.y, ar00, totalR, maxDist, 8, bestSep, bestAxis, bestNormal)) return;
    if(!nwbPhysicsTestBoxEdgeAxis(tVec.z, tVec.x, azA, axA, r20, r00, hA.z, ar00, hA.x, ar20, hB.y, ar12, hB.z, ar11, totalR, maxDist, 9, bestSep, bestAxis, bestNormal)) return;
    if(!nwbPhysicsTestBoxEdgeAxis(tVec.z, tVec.x, azA, axA, r21, r01, hA.z, ar01, hA.x, ar21, hB.z, ar10, hB.x, ar12, totalR, maxDist, 10, bestSep, bestAxis, bestNormal)) return;
    if(!nwbPhysicsTestBoxEdgeAxis(tVec.z, tVec.x, azA, axA, r22, r02, hA.z, ar02, hA.x, ar22, hB.x, ar11, hB.y, ar10, totalR, maxDist, 11, bestSep, bestAxis, bestNormal)) return;
    if(!nwbPhysicsTestBoxEdgeAxis(tVec.x, tVec.y, axA, ayA, r00, r10, hA.x, ar10, hA.y, ar00, hB.y, ar22, hB.z, ar21, totalR, maxDist, 12, bestSep, bestAxis, bestNormal)) return;
    if(!nwbPhysicsTestBoxEdgeAxis(tVec.x, tVec.y, axA, ayA, r01, r11, hA.x, ar11, hA.y, ar01, hB.z, ar20, hB.x, ar22, totalR, maxDist, 13, bestSep, bestAxis, bestNormal)) return;
    if(!nwbPhysicsTestBoxEdgeAxis(tVec.x, tVec.y, axA, ayA, r02, r12, hA.x, ar12, hA.y, ar02, hB.x, ar21, hB.y, ar20, totalR, maxDist, 14, bestSep, bestAxis, bestNormal)) return;

    if(bestAxis >= 6){
        const int edgeI = (bestAxis - 6) / 3;
        const int edgeJ = (bestAxis - 6) % 3;
        float3 pA = cA;
        float3 pB = cB;
        if(edgeI != 0) pA += axA * ((dot(axA, bestNormal) >= 0.0f ? 1.0f : -1.0f) * hA.x);
        if(edgeI != 1) pA += ayA * ((dot(ayA, bestNormal) >= 0.0f ? 1.0f : -1.0f) * hA.y);
        if(edgeI != 2) pA += azA * ((dot(azA, bestNormal) >= 0.0f ? 1.0f : -1.0f) * hA.z);
        if(edgeJ != 0) pB += axB * ((dot(axB, bestNormal) <= 0.0f ? 1.0f : -1.0f) * hB.x);
        if(edgeJ != 1) pB += ayB * ((dot(ayB, bestNormal) <= 0.0f ? 1.0f : -1.0f) * hB.y);
        if(edgeJ != 2) pB += azB * ((dot(azB, bestNormal) <= 0.0f ? 1.0f : -1.0f) * hB.z);
        const float3 eA = (edgeI == 0) ? axA : ((edgeI == 1) ? ayA : azA);
        const float3 eB = (edgeJ == 0) ? axB : ((edgeJ == 1) ? ayB : azB);
        const float hEA = (edgeI == 0) ? hA.x : ((edgeI == 1) ? hA.y : hA.z);
        const float hEB = (edgeJ == 0) ? hB.x : ((edgeJ == 1) ? hB.y : hB.z);
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
    const float3 refAx0 = refIsA ? axA : axB;
    const float3 refAx1 = refIsA ? ayA : ayB;
    const float3 refAx2 = refIsA ? azA : azB;
    const float3 incAx0 = refIsA ? axB : axA;
    const float3 incAx1 = refIsA ? ayB : ayA;
    const float3 incAx2 = refIsA ? azB : azA;
    const float3 refH = refIsA ? hA : hB;
    const float3 incH = refIsA ? hB : hA;

    const int refFace = refIsA ? (bestAxis * 2 + (bestSignedProj < 0.0f ? 1 : 0)) : ((bestAxis - 3) * 2 + (bestSignedProj < 0.0f ? 0 : 1));

    int incFace = 0;
    float bestIncDot = 2.0f;
    {
        const float d0 = dot(incAx0, refNormal);
        if(d0 < bestIncDot){ bestIncDot = d0; incFace = 0; }
        if(-d0 < bestIncDot){ bestIncDot = -d0; incFace = 1; }
        const float d1 = dot(incAx1, refNormal);
        if(d1 < bestIncDot){ bestIncDot = d1; incFace = 2; }
        if(-d1 < bestIncDot){ bestIncDot = -d1; incFace = 3; }
        const float d2 = dot(incAx2, refNormal);
        if(d2 < bestIncDot){ bestIncDot = d2; incFace = 4; }
        if(-d2 < bestIncDot){ bestIncDot = -d2; incFace = 5; }
    }

    const int refAxisIdx = refFace / 2;
    const float refSign = ((refFace & 1) == 0) ? 1.0f : -1.0f;
    float3 uAxis = refAx1;
    float3 vAxis = refAx2;
    float3 clipRefN = refAx0 * refSign;
    float refDepth = refH.x;
    float hu = refH.y;
    float hv = refH.z;
    if(refAxisIdx == 1){
        uAxis = refAx2;
        vAxis = refAx0;
        clipRefN = refAx1 * refSign;
        refDepth = refH.y;
        hu = refH.z;
        hv = refH.x;
    }
    else if(refAxisIdx == 2){
        uAxis = refAx0;
        vAxis = refAx1;
        clipRefN = refAx2 * refSign;
        refDepth = refH.z;
        hu = refH.x;
        hv = refH.y;
    }
    const float3 refFaceCenter = refCenter + clipRefN * refDepth;

    float4 poly0[8];
    float4 poly1[8];
    float3 incVerts[4];
    nwbPhysicsGetBoxFace(incCenter, incAx0, incAx1, incAx2, incH, incFace, incVerts);
    [unroll]
    for(uint p = 0u; p < 4u; ++p){
        const float3 rel = incVerts[p] - refFaceCenter;
        poly0[p] = float4(dot(uAxis, rel), dot(vAxis, rel), dot(clipRefN, rel), asfloat(p));
    }
    uint polyCount = 4u;

    for(uint axis = 0u; axis < 2u; ++axis){
        const float limit = (axis == 0u) ? hu : hv;
        const uint planeBase = axis * 2u + 1u;
        polyCount = nwbPhysicsClipBox2DPlaneAxis8(poly0, polyCount, axis, 1.0f, limit, planeBase, poly1);
        if(polyCount == 0u)
            return;
        polyCount = nwbPhysicsClipBox2DPlaneAxis8(poly1, polyCount, axis, -1.0f, limit, planeBase + 1u, poly0);
        if(polyCount == 0u)
            return;
    }

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
    const float4 inPts[NWB_PHYSICS_GPU_MAX_CLIP_POINTS],
    const uint inCount,
    const float3 planeNormal,
    const float planeOffset,
    const uint planeId,
    out float4 outPts[NWB_PHYSICS_GPU_MAX_CLIP_POINTS],
    inout bool outOverflow
){
    uint outCount = 0u;
    if(inCount == 0u){
        outPts[0] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return 0u;
    }

    float4 prevPt = inPts[inCount - 1u];
    uint prevFeat = asuint(prevPt.w);
    float prevDist = dot(planeNormal, prevPt.xyz) - planeOffset;

    [loop]
    for(uint i = 0u; i < inCount; ++i){
        const float4 curPt = inPts[i];
        const uint curFeat = asuint(curPt.w);
        const float curDist = dot(planeNormal, curPt.xyz) - planeOffset;

        if(curDist <= 0.0f){
            if(prevDist > 0.0f){
                const float denom = prevDist - curDist;
                const float t = abs(denom) > 1.0e-12f ? (prevDist / denom) : 0.0f;
                if(outCount < NWB_PHYSICS_GPU_MAX_CLIP_POINTS){
                    const uint feat = 0x4000u | ((planeId & 0x3fu) << 8u) | (curFeat & 0xffu);
                    outPts[outCount] = float4(lerp(prevPt.xyz, curPt.xyz, clamp(t, 0.0f, 1.0f)), asfloat(feat));
                    ++outCount;
                }
                else{
                    outOverflow = true;
                }
            }
            if(outCount < NWB_PHYSICS_GPU_MAX_CLIP_POINTS){
                outPts[outCount] = curPt;
                ++outCount;
            }
            else{
                outOverflow = true;
            }
        }
        else if(prevDist <= 0.0f){
            const float denom = prevDist - curDist;
            const float t = abs(denom) > 1.0e-12f ? (prevDist / denom) : 0.0f;
            if(outCount < NWB_PHYSICS_GPU_MAX_CLIP_POINTS){
                const uint feat = 0x8000u | ((planeId & 0x3fu) << 8u) | (prevFeat & 0xffu);
                outPts[outCount] = float4(lerp(prevPt.xyz, curPt.xyz, clamp(t, 0.0f, 1.0f)), asfloat(feat));
                ++outCount;
            }
            else{
                outOverflow = true;
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

void nwbPhysicsSolveSimplex(
    inout float3 sW[4],
    inout float3 sWA[4],
    inout float3 sWB[4],
    inout uint sIdxA[4],
    inout uint sIdxB[4],
    inout float sU[4],
    inout uint count
){
    uint slots[4];
    float weights[4];
    uint nextCount = count;
    if(count == 2u)
        nwbPhysicsSolveSimplex2Slots(sW[0], sW[1], slots, weights, nextCount);
    else if(count == 3u)
        nwbPhysicsSolveSimplex3Slots(sW[0], sW[1], sW[2], slots, weights, nextCount);
    else if(count == 4u)
        nwbPhysicsSolveSimplex4Slots(sW, slots, weights, nextCount);
    else
        return;

    const float3 pW[4] = sW;
    const float3 pWA[4] = sWA;
    const float3 pWB[4] = sWB;
    const uint pIdxA[4] = sIdxA;
    const uint pIdxB[4] = sIdxB;
    count = nextCount;
    for(uint k = 0u; k < nextCount; ++k){
        const uint src = slots[k];
        sW[k] = pW[src];
        sWA[k] = pWA[src];
        sWB[k] = pWB[src];
        sIdxA[k] = pIdxA[src];
        sIdxB[k] = pIdxB[src];
        sU[k] = weights[k];
    }
}

struct NwbPhysicsGpuSupportCache{
    uint shapeType;
    uint vCount;
    uint vOffset;
    bool isBox;
    float3 p0OrCenter;
    float3 p1OrExtent;
    float3 axX;
    float3 axY;
    float3 axZ;
    float2 extraParams;
};

NwbPhysicsGpuSupportCache nwbPhysicsBuildSupportCache(
    const NwbPhysicsGpuShapeProxy proxy,
    const float3 origin,
    const float4 rot
){
    NwbPhysicsGpuSupportCache cache;
    const uint shapeType = proxy.bodyIndexTypeShape.y;
    const bool isBox = (shapeType == NWB_PHYSICS_SHAPE_TYPE_HULL) && ((proxy.bodyIndexTypeShape.w & NWB_PHYSICS_SHAPE_FLAG_IS_BOX) != 0u);
    cache.shapeType = shapeType;
    cache.vCount = 0u;
    cache.vOffset = 0u;
    cache.isBox = isBox;
    cache.p0OrCenter = float3(0.0f, 0.0f, 0.0f);
    cache.p1OrExtent = float3(0.0f, 0.0f, 0.0f);
    cache.axX = float3(1.0f, 0.0f, 0.0f);
    cache.axY = float3(0.0f, 1.0f, 0.0f);
    cache.axZ = float3(0.0f, 0.0f, 1.0f);
    cache.extraParams = float2(0.0f, 0.0f);

    if(shapeType == NWB_PHYSICS_SHAPE_TYPE_SPHERE){
        cache.p0OrCenter = origin + nwbPhysicsQuatRotate(proxy.localCenterRadius.xyz, rot);
    }
    else if(shapeType == NWB_PHYSICS_SHAPE_TYPE_CAPSULE || shapeType == NWB_PHYSICS_SHAPE_TYPE_SEGMENT){
        cache.p0OrCenter = origin + nwbPhysicsQuatRotate(proxy.localCenterRadius.xyz, rot);
        cache.p1OrExtent = origin + nwbPhysicsQuatRotate(proxy.localExtentOrP1.xyz, rot);
    }
    else if(shapeType == NWB_PHYSICS_SHAPE_TYPE_TAPERED_CAPSULE){
        cache.p0OrCenter = origin + nwbPhysicsQuatRotate(proxy.localCenterRadius.xyz, rot);
        cache.p1OrExtent = origin + nwbPhysicsQuatRotate(proxy.localExtentOrP1.xyz, rot);
        cache.extraParams = float2(proxy.localRotationOrMeta.x, proxy.localRotationOrMeta.y);
    }
    else if(shapeType == NWB_PHYSICS_SHAPE_TYPE_CYLINDER || shapeType == NWB_PHYSICS_SHAPE_TYPE_CONE){
        const float4 qShape = (proxy.localRotationOrMeta.w >= 0.999999f)
            ? rot
            : nwbPhysicsQuatNormalize(nwbPhysicsQuatMul(rot, proxy.localRotationOrMeta));
        cache.p0OrCenter = origin + nwbPhysicsQuatRotate(proxy.localCenterRadius.xyz, rot);
        nwbPhysicsQuatToAxes(qShape, cache.axX, cache.axY, cache.axZ);
        cache.extraParams = float2(proxy.localExtentOrP1.x, proxy.localExtentOrP1.y);
    }
    else if(isBox){
        const float4 qBox = (proxy.localRotationOrMeta.w >= 0.999999f)
            ? rot
            : nwbPhysicsQuatNormalize(nwbPhysicsQuatMul(rot, proxy.localRotationOrMeta));
        cache.p0OrCenter = origin + nwbPhysicsQuatRotate(proxy.localCenterRadius.xyz, rot);
        cache.p1OrExtent = proxy.localExtentOrP1.xyz;
        nwbPhysicsQuatToAxes(qBox, cache.axX, cache.axY, cache.axZ);
    }
    else{
        cache.vOffset = proxy.filterData.w;
        cache.vCount = min(asuint(proxy.localRotationOrMeta.x) & 0xffu, NWB_PHYSICS_MAX_HULL_VERTICES);
        cache.p0OrCenter = origin;
        nwbPhysicsQuatToAxes(rot, cache.axX, cache.axY, cache.axZ);
    }
    return cache;
}

float3 nwbPhysicsSupportCached(
    const NwbPhysicsGpuSupportCache cache,
    const uint hullVertexSlot,
    const float3 worldDir,
    out uint outIdx
){
    if(cache.shapeType == NWB_PHYSICS_SHAPE_TYPE_SPHERE){
        outIdx = 0u;
        return cache.p0OrCenter;
    }
    if(cache.shapeType == NWB_PHYSICS_SHAPE_TYPE_CAPSULE || cache.shapeType == NWB_PHYSICS_SHAPE_TYPE_SEGMENT){
        if(dot(cache.p0OrCenter, worldDir) >= dot(cache.p1OrExtent, worldDir)){
            outIdx = 0u;
            return cache.p0OrCenter;
        }
        outIdx = 1u;
        return cache.p1OrExtent;
    }
    if(cache.shapeType == NWB_PHYSICS_SHAPE_TYPE_TAPERED_CAPSULE){
        const float dirLen = length(worldDir);
        const float3 uDir = (dirLen > 1.0e-6f) ? (worldDir / dirLen) : float3(0.0f, 1.0f, 0.0f);
        const float3 s0 = cache.p0OrCenter + uDir * cache.extraParams.x;
        const float3 s1 = cache.p1OrExtent + uDir * cache.extraParams.y;
        outIdx = 0x8000u;
        if(dot(s1, worldDir) > dot(s0, worldDir))
            return s1;
        return s0;
    }
    if(cache.shapeType == NWB_PHYSICS_SHAPE_TYPE_CYLINDER){
        const float3 ld = float3(dot(cache.axX, worldDir), dot(cache.axY, worldDir), dot(cache.axZ, worldDir));
        const float rCyl = cache.extraParams.x;
        const float hhCyl = cache.extraParams.y;
        const float sy = (ld.y < 0.0f) ? -hhCyl : hhCyl;
        const float radSq = ld.x * ld.x + ld.z * ld.z;
        float3 sLoc = float3(0.0f, sy, 0.0f);
        if(radSq > 1.0e-12f){
            const float scale = rCyl * rsqrt(radSq);
            sLoc = float3(ld.x * scale, sy, ld.z * scale);
        }
        outIdx = 0x8000u;
        return cache.p0OrCenter + cache.axX * sLoc.x + cache.axY * sLoc.y + cache.axZ * sLoc.z;
    }
    if(cache.shapeType == NWB_PHYSICS_SHAPE_TYPE_CONE){
        const float3 ld = float3(dot(cache.axX, worldDir), dot(cache.axY, worldDir), dot(cache.axZ, worldDir));
        const float rCone = cache.extraParams.x;
        const float hhCone = cache.extraParams.y;
        const float radSq = ld.x * ld.x + ld.z * ld.z;
        const float radLen = sqrt(radSq);
        const float apexDot = ld.y * hhCone;
        const float baseDot = -ld.y * hhCone + rCone * radLen;
        float3 sLoc = float3(0.0f, hhCone, 0.0f);
        if(apexDot < baseDot){
            if(radSq > 1.0e-12f){
                const float scale = rCone / radLen;
                sLoc = float3(ld.x * scale, -hhCone, ld.z * scale);
            }
            else{
                sLoc = float3(0.0f, -hhCone, 0.0f);
            }
        }
        outIdx = 0x8000u;
        return cache.p0OrCenter + cache.axX * sLoc.x + cache.axY * sLoc.y + cache.axZ * sLoc.z;
    }
    if(cache.isBox){
        const float3 ld = float3(dot(cache.axX, worldDir), dot(cache.axY, worldDir), dot(cache.axZ, worldDir));
        const float3 h = cache.p1OrExtent;
        const float3 s = float3(ld.x >= 0.0f ? h.x : -h.x, ld.y >= 0.0f ? h.y : -h.y, ld.z >= 0.0f ? h.z : -h.z);
        outIdx = (ld.x >= 0.0f ? 1u : 0u) | (ld.y >= 0.0f ? 2u : 0u) | (ld.z >= 0.0f ? 4u : 0u);
        return cache.p0OrCenter + cache.axX * s.x + cache.axY * s.y + cache.axZ * s.z;
    }

    if(cache.vCount == 0u){
        outIdx = 0u;
        return cache.p0OrCenter;
    }
    const float3 localDir = float3(dot(cache.axX, worldDir), dot(cache.axY, worldDir), dot(cache.axZ, worldDir));
    uint bestIdx = 0u;
    float3 bestLocal = g_NwbPhysicsFloat4Buffers[hullVertexSlot][cache.vOffset].xyz;
    float bestDot = dot(bestLocal, localDir);
    for(uint i = 1u; i < cache.vCount; ++i){
        const float3 v = g_NwbPhysicsFloat4Buffers[hullVertexSlot][cache.vOffset + i].xyz;
        const float d = dot(v, localDir);
        if(d > bestDot){
            bestDot = d;
            bestIdx = i;
            bestLocal = v;
        }
    }
    outIdx = bestIdx;
    return cache.p0OrCenter + cache.axX * bestLocal.x + cache.axY * bestLocal.y + cache.axZ * bestLocal.z;
}

#if NWB_PHYSICS_NP_PROFILE == 1
void nwbPhysicsSupportMinMaxCached(
    const NwbPhysicsGpuSupportCache cache,
    const uint hullVertexSlot,
    const float3 axis,
    out float outMin,
    out float outMax
){
    if(cache.shapeType == NWB_PHYSICS_SHAPE_TYPE_SPHERE){
        const float p = dot(cache.p0OrCenter, axis);
        outMin = p;
        outMax = p;
        return;
    }
    if(cache.shapeType == NWB_PHYSICS_SHAPE_TYPE_CAPSULE || cache.shapeType == NWB_PHYSICS_SHAPE_TYPE_SEGMENT){
        const float d0 = dot(cache.p0OrCenter, axis);
        const float d1 = dot(cache.p1OrExtent, axis);
        outMin = min(d0, d1);
        outMax = max(d0, d1);
        return;
    }
    if(cache.shapeType == NWB_PHYSICS_SHAPE_TYPE_TAPERED_CAPSULE){
        const float axisLen = length(axis);
        const float d0 = dot(cache.p0OrCenter, axis);
        const float d1 = dot(cache.p1OrExtent, axis);
        const float r0 = cache.extraParams.x * axisLen;
        const float r1 = cache.extraParams.y * axisLen;
        outMin = min(d0 - r0, d1 - r1);
        outMax = max(d0 + r0, d1 + r1);
        return;
    }
    if(cache.shapeType == NWB_PHYSICS_SHAPE_TYPE_CYLINDER){
        const float3 ld = float3(dot(cache.axX, axis), dot(cache.axY, axis), dot(cache.axZ, axis));
        const float c = dot(cache.p0OrCenter, axis);
        const float ext = abs(ld.y) * cache.extraParams.y + cache.extraParams.x * sqrt(ld.x * ld.x + ld.z * ld.z);
        outMin = c - ext;
        outMax = c + ext;
        return;
    }
    if(cache.shapeType == NWB_PHYSICS_SHAPE_TYPE_CONE){
        const float3 ld = float3(dot(cache.axX, axis), dot(cache.axY, axis), dot(cache.axZ, axis));
        const float c = dot(cache.p0OrCenter, axis);
        const float apexProj = ld.y * cache.extraParams.y;
        const float radExt = cache.extraParams.x * sqrt(ld.x * ld.x + ld.z * ld.z);
        outMin = c + min(apexProj, -apexProj - radExt);
        outMax = c + max(apexProj, -apexProj + radExt);
        return;
    }
    if(cache.isBox){
        const float3 ld = abs(float3(dot(cache.axX, axis), dot(cache.axY, axis), dot(cache.axZ, axis)));
        const float c = dot(cache.p0OrCenter, axis);
        const float r = dot(ld, cache.p1OrExtent);
        outMin = c - r;
        outMax = c + r;
        return;
    }

    const float baseOff = dot(cache.p0OrCenter, axis);
    if(cache.vCount == 0u){
        outMin = baseOff;
        outMax = baseOff;
        return;
    }
    const float3 localDir = float3(dot(cache.axX, axis), dot(cache.axY, axis), dot(cache.axZ, axis));
    const float dFirst = dot(g_NwbPhysicsFloat4Buffers[hullVertexSlot][cache.vOffset].xyz, localDir);
    float dMin = dFirst;
    float dMax = dFirst;
    for(uint i = 1u; i < cache.vCount; ++i){
        const float d = dot(g_NwbPhysicsFloat4Buffers[hullVertexSlot][cache.vOffset + i].xyz, localDir);
        dMin = min(dMin, d);
        dMax = max(dMax, d);
    }
    outMin = baseOff + dMin;
    outMax = baseOff + dMax;
}

void nwbPhysicsReduceManifoldTo4(
    const float3 worldOriginA,
    const float4 worldRotA,
    const float3 clipRefNLocal,
    const float3 clipRefNWorld,
    const float3 candPos[NWB_PHYSICS_GPU_MAX_MESH_CANDIDATES],
    const float candSep[NWB_PHYSICS_GPU_MAX_MESH_CANDIDATES],
    const uint candFeat[NWB_PHYSICS_GPU_MAX_MESH_CANDIDATES],
    const uint candCount,
    const bool refIsA,
    const float refR,
    const float incR,
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
            const float area = abs(dot(cross(e01, candPos[i] - p0), clipRefNLocal));
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
        const float baseSign = dot(cross(e01, p2 - p0), clipRefNLocal);
        for(uint i = 0u; i < candCount; ++i){
            if(i == idx0 || i == idx1 || i == idx2)
                continue;
            const float3 pi = candPos[i];
            const float a0 = dot(cross(p1 - p0, pi - p0), clipRefNLocal);
            const float a1 = dot(cross(p2 - p1, pi - p1), clipRefNLocal);
            const float a2 = dot(cross(p0 - p2, pi - p2), clipRefNLocal);
            nwbPhysicsUpdateFourthManifoldPoint(a0, a1, a2, baseSign, i, bestFallbackArea, fallbackIdx3, bestOutsideArea, idx3);
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
        const float3 ptW = worldOriginA + nwbPhysicsQuatRotate(candPos[ci], worldRotA);
        const float sep = candSep[ci];
        const float3 wRef = ptW - clipRefNWorld * (sep + totalR - refR);
        const float3 wInc = ptW - clipRefNWorld * incR;
        const float3 wA = refIsA ? wRef : wInc;
        const float3 wB = refIsA ? wInc : wRef;
        result.anchorAFeature[k] = float4(wA - bodyPosA, asfloat(candFeat[ci]));
        result.anchorBSeparation[k] = float4(wB - bodyPosB, sep);
    }
    result.pairInfo.z = outCount;
}

#endif

void nwbPhysicsSupportCachedOrTri(
    const NwbPhysicsGpuSupportCache cache,
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
    outPt = nwbPhysicsSupportCached(cache, hullVertexSlot, worldDir, outIdx);
}

bool nwbPhysicsRunGjkDistanceCached(
    const NwbPhysicsGpuSupportCache cacheA,
    const bool triA,
    const float3 av0,
    const float3 av1,
    const float3 av2,
    const NwbPhysicsGpuSupportCache cacheB,
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
    nwbPhysicsSupportCachedOrTri(cacheA, hullVertexSlot, triA, av0, av1, av2, float3(1.0f, 0.0f, 0.0f), sIdxA[0], sWA[0]);
    nwbPhysicsSupportCachedOrTri(cacheB, hullVertexSlot, triB, bv0, bv1, bv2, float3(-1.0f, 0.0f, 0.0f), sIdxB[0], sWB[0]);
    sW[0] = sWB[0] - sWA[0];
    sU[0] = 1.0f;

    outHitDeep = false;
    for(uint iter = 0u; iter < 25u; ++iter){
        if(sCount >= 2u)
            nwbPhysicsSolveSimplex(sW, sWA, sWB, sIdxA, sIdxB, sU, sCount);

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
        nwbPhysicsSupportCachedOrTri(cacheA, hullVertexSlot, triA, av0, av1, av2, -d, idxA, wA);
        nwbPhysicsSupportCachedOrTri(cacheB, hullVertexSlot, triB, bv0, bv1, bv2, d, idxB, wB);

        bool dup = false;
        if(idxA < 0x8000u && idxB < 0x8000u){
            for(uint k = 0u; k < sCount; ++k){
                if(sIdxA[k] == idxA && sIdxB[k] == idxB){
                    dup = true;
                    break;
                }
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
    const NwbPhysicsGpuSupportCache cacheA = nwbPhysicsBuildSupportCache(proxyA, originA, rotA);
    NwbPhysicsGpuSupportCache cacheB;
    cacheB.shapeType = 0u;
    cacheB.vCount = 0u;
    cacheB.vOffset = 0u;
    cacheB.isBox = false;
    cacheB.p0OrCenter = float3(0.0f, 0.0f, 0.0f);
    cacheB.p1OrExtent = float3(0.0f, 0.0f, 0.0f);
    cacheB.axX = float3(1.0f, 0.0f, 0.0f);
    cacheB.axY = float3(0.0f, 1.0f, 0.0f);
    cacheB.axZ = float3(0.0f, 0.0f, 1.0f);
    cacheB.extraParams = float2(0.0f, 0.0f);
    if(!triB)
        cacheB = nwbPhysicsBuildSupportCache(proxyB, originB, rotB);
    return nwbPhysicsRunGjkDistanceCached(
        cacheA,
        false,
        float3(0.0f, 0.0f, 0.0f),
        float3(0.0f, 0.0f, 0.0f),
        float3(0.0f, 0.0f, 0.0f),
        cacheB,
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
struct NwbPhysicsGpuEpaFace{
    uint packedIdxAlive;
    float3 normal;
    float dist;
};

uint nwbPhysicsEpaPackFaceIdxAlive(const uint i0, const uint i1, const uint i2, const uint alive){
    return (i0 & 0xffu) | ((i1 & 0xffu) << 8u) | ((i2 & 0xffu) << 16u) | ((alive & 0xffu) << 24u);
}

uint nwbPhysicsEpaFaceI0(const NwbPhysicsGpuEpaFace f){
    return f.packedIdxAlive & 0xffu;
}

uint nwbPhysicsEpaFaceI1(const NwbPhysicsGpuEpaFace f){
    return (f.packedIdxAlive >> 8u) & 0xffu;
}

uint nwbPhysicsEpaFaceI2(const NwbPhysicsGpuEpaFace f){
    return (f.packedIdxAlive >> 16u) & 0xffu;
}

bool nwbPhysicsEpaFaceAlive(const NwbPhysicsGpuEpaFace f){
    return (f.packedIdxAlive >> 24u) != 0u;
}

float3 nwbPhysicsEpaSupportPair(
    const NwbPhysicsGpuSupportCache cacheA,
    const bool triA,
    const float3 av0,
    const float3 av1,
    const float3 av2,
    const NwbPhysicsGpuSupportCache cacheB,
    const bool triB,
    const float3 bv0,
    const float3 bv1,
    const float3 bv2,
    const uint hullVertexSlot,
    const float3 dir,
    out float3 outWA
){
    uint idxA = 0u;
    uint idxB = 0u;
    float3 ptA = float3(0.0f, 0.0f, 0.0f);
    float3 ptB = float3(0.0f, 0.0f, 0.0f);
    nwbPhysicsSupportCachedOrTri(cacheA, hullVertexSlot, triA, av0, av1, av2, dir, idxA, ptA);
    nwbPhysicsSupportCachedOrTri(cacheB, hullVertexSlot, triB, bv0, bv1, bv2, -dir, idxB, ptB);
    outWA = ptA;
    return ptA - ptB;
}

bool nwbPhysicsEpaMakeFace(
    const float3 verts[20],
    const uint i0In,
    const uint i1In,
    const uint i2In,
    const float3 interior,
    out NwbPhysicsGpuEpaFace outFace
){
    const uint f0 = i0In;
    uint f1 = i1In;
    uint f2 = i2In;
    outFace.packedIdxAlive = nwbPhysicsEpaPackFaceIdxAlive(f0, f1, f2, 0u);
    outFace.normal = float3(0.0f, 1.0f, 0.0f);
    outFace.dist = 0.0f;

    const float3 a = verts[i0In];
    const float3 b = verts[i1In];
    const float3 c = verts[i2In];
    float3 n = cross(b - a, c - a);
    const float lenSq = dot(n, n);
    if(lenSq <= 1.0e-12f)
        return false;
    n *= rsqrt(lenSq);
    if(dot(n, interior - a) > 0.0f){
        n = -n;
        f1 = i2In;
        f2 = i1In;
    }
    outFace.packedIdxAlive = nwbPhysicsEpaPackFaceIdxAlive(f0, f1, f2, 1u);
    outFace.normal = n;
    outFace.dist = dot(n, a);
    return true;
}

float3 nwbPhysicsClosestPointBaryTriangle(const float3 p, const float3 a, const float3 b, const float3 c){
    const float3 ab = b - a;
    const float3 ac = c - a;
    const float3 ap = p - a;
    const float d1 = dot(ab, ap);
    const float d2 = dot(ac, ap);
    if(d1 <= 0.0f && d2 <= 0.0f)
        return float3(1.0f, 0.0f, 0.0f);

    const float3 bp = p - b;
    const float d3 = dot(ab, bp);
    const float d4 = dot(ac, bp);
    if(d3 >= 0.0f && d4 <= d3)
        return float3(0.0f, 1.0f, 0.0f);

    const float vc = d1 * d4 - d3 * d2;
    if(vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f){
        const float v = d1 / (d1 - d3);
        return float3(1.0f - v, v, 0.0f);
    }

    const float3 cp = p - c;
    const float d5 = dot(ab, cp);
    const float d6 = dot(ac, cp);
    if(d6 >= 0.0f && d5 <= d6)
        return float3(0.0f, 0.0f, 1.0f);

    const float vb = d5 * d2 - d1 * d6;
    if(vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f){
        const float w = d2 / (d2 - d6);
        return float3(1.0f - w, 0.0f, w);
    }

    const float va = d3 * d6 - d5 * d4;
    if(va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f){
        const float denomBc = (d4 - d3) + (d5 - d6);
        const float w = (denomBc > 1.0e-12f) ? ((d4 - d3) / denomBc) : 0.0f;
        return float3(0.0f, 1.0f - w, w);
    }

    const float denom = va + vb + vc;
    if(denom <= 1.0e-12f)
        return float3(1.0f, 0.0f, 0.0f);
    const float invDenom = 1.0f / denom;
    const float v = vb * invDenom;
    const float w = vc * invDenom;
    return float3(1.0f - v - w, v, w);
}

bool nwbPhysicsRunEpaCached(
    const NwbPhysicsGpuSupportCache cacheA,
    const bool triA,
    const float3 av0,
    const float3 av1,
    const float3 av2,
    const NwbPhysicsGpuSupportCache cacheB,
    const bool triB,
    const float3 bv0,
    const float3 bv1,
    const float3 bv2,
    const uint hullVertexSlot,
    const float3 hintNormalAtoB,
    out float3 outNormalAtoB,
    out float3 outPtA,
    out float3 outPtB,
    out float outDepth
){
    outNormalAtoB = float3(0.0f, 1.0f, 0.0f);
    outPtA = float3(0.0f, 0.0f, 0.0f);
    outPtB = float3(0.0f, 0.0f, 0.0f);
    outDepth = 0.0f;

    float3 verts[20];
    float3 wAs[20];

    const float3 d0 = nwbPhysicsSafeNormal(hintNormalAtoB, float3(0.0f, 1.0f, 0.0f));
    verts[0] = nwbPhysicsEpaSupportPair(cacheA, triA, av0, av1, av2, cacheB, triB, bv0, bv1, bv2, hullVertexSlot, d0, wAs[0]);

    float3 d1 = (dot(verts[0], verts[0]) > 1.0e-10f) ? nwbPhysicsSafeNormal(-verts[0], -d0) : -d0;
    verts[1] = nwbPhysicsEpaSupportPair(cacheA, triA, av0, av1, av2, cacheB, triB, bv0, bv1, bv2, hullVertexSlot, d1, wAs[1]);
    float3 e01 = verts[1] - verts[0];
    if(dot(e01, e01) <= 1.0e-10f){
        float3 bU1;
        float3 bU2;
        nwbPhysicsComputeTangentBasis(d0, bU1, bU2);
        const float3 altDirs1[3] = { -d0, bU1, bU2 };
        [loop]
        for(uint ai = 0u; ai < 3u; ++ai){
            verts[1] = nwbPhysicsEpaSupportPair(cacheA, triA, av0, av1, av2, cacheB, triB, bv0, bv1, bv2, hullVertexSlot, altDirs1[ai], wAs[1]);
            e01 = verts[1] - verts[0];
            if(dot(e01, e01) > 1.0e-10f)
                break;
        }
        if(dot(e01, e01) <= 1.0e-10f)
            return false;
    }

    const float3 e01Dir = nwbPhysicsSafeNormal(e01, d0);
    float3 perpU1;
    float3 perpU2;
    nwbPhysicsComputeTangentBasis(e01Dir, perpU1, perpU2);
    float3 d2 = cross(e01, -verts[0]);
    d2 = (dot(d2, d2) > 1.0e-10f) ? nwbPhysicsSafeNormal(d2, perpU1) : perpU1;
    const float3 candDirs2[4] = { d2, -d2, perpU2, -perpU2 };
    bool foundP2 = false;
    [loop]
    for(uint ci = 0u; ci < 4u; ++ci){
        verts[2] = nwbPhysicsEpaSupportPair(cacheA, triA, av0, av1, av2, cacheB, triB, bv0, bv1, bv2, hullVertexSlot, candDirs2[ci], wAs[2]);
        const float3 triArea = cross(e01, verts[2] - verts[0]);
        if(dot(triArea, triArea) > 1.0e-10f){
            foundP2 = true;
            break;
        }
    }
    if(!foundP2)
        return false;

    const float3 e02 = verts[2] - verts[0];
    const float3 rawN012 = cross(e01, e02);
    const float3 n012 = nwbPhysicsSafeNormal(rawN012, d0);
    const float3 d3 = (dot(n012, -verts[0]) >= 0.0f) ? n012 : -n012;
    verts[3] = nwbPhysicsEpaSupportPair(cacheA, triA, av0, av1, av2, cacheB, triB, bv0, bv1, bv2, hullVertexSlot, d3, wAs[3]);
    if(abs(dot(n012, verts[3] - verts[0])) <= 1.0e-7f){
        verts[3] = nwbPhysicsEpaSupportPair(cacheA, triA, av0, av1, av2, cacheB, triB, bv0, bv1, bv2, hullVertexSlot, -d3, wAs[3]);
        if(abs(dot(n012, verts[3] - verts[0])) <= 1.0e-7f)
            return false;
    }

    NwbPhysicsGpuEpaFace faces[36];

    float3 interior = (verts[0] + verts[1] + verts[2] + verts[3]) * 0.25f;
    if(
        !nwbPhysicsEpaMakeFace(verts, 0u, 1u, 2u, interior, faces[0])
        || !nwbPhysicsEpaMakeFace(verts, 0u, 2u, 3u, interior, faces[1])
        || !nwbPhysicsEpaMakeFace(verts, 0u, 3u, 1u, interior, faces[2])
        || !nwbPhysicsEpaMakeFace(verts, 1u, 3u, 2u, interior, faces[3])
    )
        return false;

    [loop]
    for(uint enc = 0u; enc < 4u; ++enc){
        uint worstIdx = 0u;
        float worstDist = faces[0].dist;
        [loop]
        for(uint fi = 1u; fi < 4u; ++fi){
            if(faces[fi].dist < worstDist){
                worstDist = faces[fi].dist;
                worstIdx = fi;
            }
        }
        if(worstDist >= -1.0e-4f)
            break;

        const NwbPhysicsGpuEpaFace worstFace = faces[worstIdx];
        const uint oppIdx = 6u - (nwbPhysicsEpaFaceI0(worstFace) + nwbPhysicsEpaFaceI1(worstFace) + nwbPhysicsEpaFaceI2(worstFace));
        const float3 oldV = verts[oppIdx];
        const float3 oldWA = wAs[oppIdx];
        float3 wANew = float3(0.0f, 0.0f, 0.0f);
        const float3 pNew = nwbPhysicsEpaSupportPair(cacheA, triA, av0, av1, av2, cacheB, triB, bv0, bv1, bv2, hullVertexSlot, worstFace.normal, wANew);
        if(dot(worstFace.normal, pNew - verts[nwbPhysicsEpaFaceI0(worstFace)]) <= 1.0e-6f)
            break;

        verts[oppIdx] = pNew;
        wAs[oppIdx] = wANew;
        const float3 candInterior = (verts[0] + verts[1] + verts[2] + verts[3]) * 0.25f;
        if(
            !nwbPhysicsEpaMakeFace(verts, 0u, 1u, 2u, candInterior, faces[4])
            || !nwbPhysicsEpaMakeFace(verts, 0u, 2u, 3u, candInterior, faces[5])
            || !nwbPhysicsEpaMakeFace(verts, 0u, 3u, 1u, candInterior, faces[6])
            || !nwbPhysicsEpaMakeFace(verts, 1u, 3u, 2u, candInterior, faces[7])
        ){
            verts[oppIdx] = oldV;
            wAs[oppIdx] = oldWA;
            break;
        }
        interior = candInterior;
        faces[0] = faces[4];
        faces[1] = faces[5];
        faces[2] = faces[6];
        faces[3] = faces[7];
    }

    [loop]
    for(uint fi = 0u; fi < 4u; ++fi){
        if(faces[fi].dist < -5.0e-4f)
            return false;
    }

    uint vertCount = 4u;
    uint faceCount = 4u;
    uint openEdges[36];
    openEdges[0] = 0u;

    [loop]
    for(uint iter = 0u; iter < 16u; ++iter){
        uint bestIdx = 36u;
        float bestDist = 1.0e20f;
        [loop]
        for(uint fi = 0u; fi < faceCount; ++fi){
            if(nwbPhysicsEpaFaceAlive(faces[fi]) && faces[fi].dist < bestDist){
                bestDist = faces[fi].dist;
                bestIdx = fi;
            }
        }
        if(bestIdx >= faceCount || vertCount >= 20u)
            break;

        float3 wANew = float3(0.0f, 0.0f, 0.0f);
        const float3 pNew = nwbPhysicsEpaSupportPair(cacheA, triA, av0, av1, av2, cacheB, triB, bv0, bv1, bv2, hullVertexSlot, faces[bestIdx].normal, wANew);
        const float pProj = dot(faces[bestIdx].normal, pNew);
        if(pProj - bestDist <= max(1.0e-4f, 1.0e-4f * max(bestDist, 0.0f)))
            break;

        bool dupVert = false;
        [loop]
        for(uint vk = 0u; vk < vertCount; ++vk){
            const float3 vd = pNew - verts[vk];
            if(dot(vd, vd) <= 1.0e-10f){
                dupVert = true;
                break;
            }
        }
        if(dupVert)
            break;

        uint edgeCount = 0u;
        bool edgeOverflow = false;
        uint deadSlotsAfterVis = 0u;
        uint visMaskLo = 0u;
        uint visMaskHi = 0u;
        [loop]
        for(uint fi = 0u; fi < faceCount; ++fi){
            if(!nwbPhysicsEpaFaceAlive(faces[fi])){
                ++deadSlotsAfterVis;
                continue;
            }
            const uint fi0 = nwbPhysicsEpaFaceI0(faces[fi]);
            if(dot(faces[fi].normal, pNew - verts[fi0]) > 1.0e-6f){
                if(fi < 32u)
                    visMaskLo |= (1u << fi);
                else
                    visMaskHi |= (1u << (fi - 32u));
                ++deadSlotsAfterVis;
                const uint fi1 = nwbPhysicsEpaFaceI1(faces[fi]);
                const uint fi2 = nwbPhysicsEpaFaceI2(faces[fi]);
                const uint triEdges[3] = {
                    fi0 | (fi1 << 8u),
                    fi1 | (fi2 << 8u),
                    fi2 | (fi0 << 8u)
                };
                [loop]
                for(uint ek = 0u; ek < 3u; ++ek){
                    const uint ePack = triEdges[ek];
                    const uint revPack = ((ePack >> 8u) & 0xffu) | ((ePack & 0xffu) << 8u);
                    bool cancelled = false;
                    [loop]
                    for(uint oi = 0u; oi < edgeCount; ++oi){
                        if(openEdges[oi] == revPack){
                            openEdges[oi] = openEdges[edgeCount - 1u];
                            --edgeCount;
                            cancelled = true;
                            break;
                        }
                    }
                    if(!cancelled){
                        if(edgeCount >= 36u){
                            edgeOverflow = true;
                            break;
                        }
                        openEdges[edgeCount] = ePack;
                        ++edgeCount;
                    }
                }
                if(edgeOverflow)
                    break;
            }
        }

        const uint freeTotal = deadSlotsAfterVis + (36u - faceCount);
        if(edgeOverflow || edgeCount == 0u || freeTotal < edgeCount)
            break;

        uint killLo = visMaskLo;
        while(killLo != 0u){
            const uint fi = firstbitlow(killLo);
            killLo &= (killLo - 1u);
            faces[fi].packedIdxAlive &= 0x00ffffffu;
        }
        uint killHi = visMaskHi;
        while(killHi != 0u){
            const uint fi = 32u + firstbitlow(killHi);
            killHi &= (killHi - 1u);
            faces[fi].packedIdxAlive &= 0x00ffffffu;
        }

        const uint newV = vertCount;
        ++vertCount;
        verts[newV] = pNew;
        wAs[newV] = wANew;

        uint slotSearch = 0u;
        [loop]
        for(uint oi = 0u; oi < edgeCount; ++oi){
            NwbPhysicsGpuEpaFace nf;
            const uint ePacked = openEdges[oi];
            if(!nwbPhysicsEpaMakeFace(verts, ePacked & 0xffu, (ePacked >> 8u) & 0xffu, newV, interior, nf))
                continue;
            while(slotSearch < faceCount && nwbPhysicsEpaFaceAlive(faces[slotSearch]))
                ++slotSearch;
            if(slotSearch < faceCount){
                faces[slotSearch] = nf;
                ++slotSearch;
            }
            else if(faceCount < 36u){
                faces[faceCount] = nf;
                ++faceCount;
                slotSearch = faceCount;
            }
        }
    }

    uint winIdx = 36u;
    float winDist = 1.0e20f;
    [loop]
    for(uint fi = 0u; fi < faceCount; ++fi){
        if(nwbPhysicsEpaFaceAlive(faces[fi]) && faces[fi].dist < winDist){
            winDist = faces[fi].dist;
            winIdx = fi;
        }
    }
    if(winIdx >= faceCount)
        return false;

    const NwbPhysicsGpuEpaFace wf = faces[winIdx];
    const uint wfI0 = nwbPhysicsEpaFaceI0(wf);
    const uint wfI1 = nwbPhysicsEpaFaceI1(wf);
    const uint wfI2 = nwbPhysicsEpaFaceI2(wf);
    const float3 bary = nwbPhysicsClosestPointBaryTriangle(float3(0.0f, 0.0f, 0.0f), verts[wfI0], verts[wfI1], verts[wfI2]);
    const float3 pWin = verts[wfI0] * bary.x + verts[wfI1] * bary.y + verts[wfI2] * bary.z;
    outNormalAtoB = wf.normal;
    outDepth = max(wf.dist, 0.0f);
    outPtA = wAs[wfI0] * bary.x + wAs[wfI1] * bary.y + wAs[wfI2] * bary.z;
    outPtB = outPtA - pWin;
    return true;
}
#endif

#if NWB_PHYSICS_NP_PROFILE == 1
uint nwbPhysicsHullVertexCount(const NwbPhysicsGpuShapeProxy proxy){
    if((proxy.bodyIndexTypeShape.w & NWB_PHYSICS_SHAPE_FLAG_IS_BOX) != 0u)
        return 8u;
    return asuint(proxy.localRotationOrMeta.x) & 0xffu;
}

uint nwbPhysicsHullFaceCount(const NwbPhysicsGpuShapeProxy proxy){
    if((proxy.bodyIndexTypeShape.w & NWB_PHYSICS_SHAPE_FLAG_IS_BOX) != 0u)
        return 6u;
    return (asuint(proxy.localRotationOrMeta.x) >> 8u) & 0xffu;
}

uint nwbPhysicsHullEdgeCount(const NwbPhysicsGpuShapeProxy proxy){
    if((proxy.bodyIndexTypeShape.w & NWB_PHYSICS_SHAPE_FLAG_IS_BOX) != 0u)
        return 12u;
    return asuint(proxy.localRotationOrMeta.x) >> 16u;
}

float3 nwbPhysicsHullLocalVertex(const NwbPhysicsGpuShapeProxy proxy, const uint hullVertexSlot, const uint vIdx){
    if((proxy.bodyIndexTypeShape.w & NWB_PHYSICS_SHAPE_FLAG_IS_BOX) != 0u){
        const float3 h = proxy.localExtentOrP1.xyz;
        const float3 corner = float3(
            (vIdx & 1u) != 0u ? h.x : -h.x,
            (vIdx & 2u) != 0u ? h.y : -h.y,
            (vIdx & 4u) != 0u ? h.z : -h.z
        );
        return proxy.localCenterRadius.xyz + nwbPhysicsQuatRotate(corner, proxy.localRotationOrMeta);
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
        const float3 rotN = nwbPhysicsQuatRotate(localN, proxy.localRotationOrMeta);
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
        const float4 qBox = (proxy.localRotationOrMeta.w >= 0.999999f)
            ? rot
            : nwbPhysicsQuatNormalize(nwbPhysicsQuatMul(rot, proxy.localRotationOrMeta));
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
    nwbPhysicsComputeTangentBasis(lN, u1, u2);
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
            const uint f0Idx = (packE >> 16u) & 0xffu;
            const uint f1Idx = (packE >> 24u) & 0xffu;
            if(f0Idx < fCount && f1Idx < fCount){
                const float3 n0 = nwbPhysicsHullLocalFacePlane(pH, hullVertexSlot, f0Idx).xyz;
                const float3 n1 = nwbPhysicsHullLocalFacePlane(pH, hullVertexSlot, f1Idx).xyz;
                if(dot(n0, dSegHull) * dot(n1, dSegHull) > 0.0f)
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
                const float3 outAxis = (dot(axis, n0 + n1) < 0.0f) ? -axis : axis;
                const float sepE = dot(outAxis, l0 - e0);
                if(sepE - totalR > maxDist)
                    return;
                if(!hasEdgeCandidate || sepE > bestEdgeSep){
                    hasEdgeCandidate = true;
                    bestEdgeSep = sepE;
                    bestEdgeOut = outAxis;
                    bestEdgeV0 = e0;
                    bestEdgeV1 = e1;
                    bestHullEdge = ei;
                }
            }
            else{
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

    const float4 invRotA = float4(-rotA.xyz, rotA.w);
    const float3 originBInA = nwbPhysicsQuatRotate(originB - originA, invRotA);
    const float4 rotBToA = nwbPhysicsQuatNormalize(nwbPhysicsQuatMul(invRotA, rotB));
    float3 bAxX;
    float3 bAxY;
    float3 bAxZ;
    nwbPhysicsQuatToAxes(rotBToA, bAxX, bAxY, bAxZ);

    const NwbPhysicsGpuSupportCache cacheA = nwbPhysicsBuildSupportCache(proxyA, float3(0.0f, 0.0f, 0.0f), float4(0.0f, 0.0f, 0.0f, 1.0f));
    const NwbPhysicsGpuSupportCache cacheBInA = nwbPhysicsBuildSupportCache(proxyB, originBInA, rotBToA);

    float bestSepA = -1.0e20f;
    uint bestFaceA = 0u;
    float3 bestLocalNA = float3(0.0f, 1.0f, 0.0f);
    for(uint i = 0u; i < fCountA; ++i){
        const float4 lp = nwbPhysicsHullLocalFacePlane(proxyA, hullVertexSlot, i);
        uint tmpIdx = 0u;
        const float3 supBInA = nwbPhysicsSupportCached(cacheBInA, hullVertexSlot, -lp.xyz, tmpIdx);
        const float sep = dot(lp.xyz, supBInA) - lp.w - totalR;
        if(sep > maxDist)
            return;
        if(sep > bestSepA){
            bestSepA = sep;
            bestFaceA = i;
            bestLocalNA = lp.xyz;
        }
    }

    float bestSepB = -1.0e20f;
    uint bestFaceB = 0u;
    float3 bestLocalNB = float3(0.0f, 1.0f, 0.0f);
    for(uint i = 0u; i < fCountB; ++i){
        const float4 lp = nwbPhysicsHullLocalFacePlane(proxyB, hullVertexSlot, i);
        const float3 nBInA = bAxX * lp.x + bAxY * lp.y + bAxZ * lp.z;
        const float offBInA = lp.w + dot(nBInA, originBInA);
        uint tmpIdx = 0u;
        const float3 supAInA = nwbPhysicsSupportCached(cacheA, hullVertexSlot, -nBInA, tmpIdx);
        const float sep = dot(nBInA, supAInA) - offBInA - totalR;
        if(sep > maxDist)
            return;
        if(sep > bestSepB){
            bestSepB = sep;
            bestFaceB = i;
            bestLocalNB = nBInA;
        }
    }

    const float3 cAInA = proxyA.localCenterRadius.xyz;
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
        const float3 pA0 = nwbPhysicsHullLocalVertex(proxyA, hullVertexSlot, vA0Idx);
        const float3 pA1 = nwbPhysicsHullLocalVertex(proxyA, hullVertexSlot, vA1Idx);
        const float3 dirA = pA1 - pA0;
        const float dirALenSq = dot(dirA, dirA);
        const float3 nA0 = nwbPhysicsHullLocalFacePlane(proxyA, hullVertexSlot, fA0Idx).xyz;
        const float3 nA1 = nwbPhysicsHullLocalFacePlane(proxyA, hullVertexSlot, fA1Idx).xyz;
        const float3 edgeBA = cross(nA0, nA1);
        const float3 nA0InB = float3(dot(bAxX, nA0), dot(bAxY, nA0), dot(bAxZ, nA0));
        const float3 nA1InB = float3(dot(bAxX, nA1), dot(bAxY, nA1), dot(bAxZ, nA1));
        const float3 edgeBAInB = float3(dot(bAxX, edgeBA), dot(bAxY, edgeBA), dot(bAxZ, edgeBA));

        for(uint eB = 0u; eB < eCountB; ++eB){
            const uint packB = nwbPhysicsHullEdgePacked(proxyB, hullVertexSlot, eB);
            const uint vB0Idx = packB & 0xffu;
            const uint vB1Idx = (packB >> 8u) & 0xffu;
            const uint fB0Idx = (packB >> 16u) & 0xffu;
            const uint fB1Idx = (packB >> 24u) & 0xffu;
            if(vB0Idx >= vCountB || vB1Idx >= vCountB || fB0Idx >= fCountB || fB1Idx >= fCountB)
                continue;
            const float3 lnB0 = nwbPhysicsHullLocalFacePlane(proxyB, hullVertexSlot, fB0Idx).xyz;
            const float3 lnB1 = nwbPhysicsHullLocalFacePlane(proxyB, hullVertexSlot, fB1Idx).xyz;
            const float3 edgeDC = cross(lnB0, lnB1);

            if(!nwbPhysicsIsMinkowskiFace(nA0InB, nA1InB, edgeBAInB, -lnB0, -lnB1, edgeDC))
                continue;

            const float3 lvB0 = nwbPhysicsHullLocalVertex(proxyB, hullVertexSlot, vB0Idx);
            const float3 lvB1 = nwbPhysicsHullLocalVertex(proxyB, hullVertexSlot, vB1Idx);
            const float3 pB0 = originBInA + bAxX * lvB0.x + bAxY * lvB0.y + bAxZ * lvB0.z;
            const float3 pB1 = originBInA + bAxX * lvB1.x + bAxY * lvB1.y + bAxZ * lvB1.z;
            const float3 dirB = pB1 - pB0;

            const float3 rawCross = cross(dirA, dirB);
            const float lenSq = dot(rawCross, rawCross);
            const float normTol = 1.0e-6f * max(dirALenSq * dot(dirB, dirB), 1.0e-12f);
            if(lenSq <= normTol)
                continue;

            float3 axis = rawCross * rsqrt(lenSq);
            if(dot(axis, pA0 - cAInA) < 0.0f)
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
        float3 segPtALoc = float3(0.0f, 0.0f, 0.0f);
        float3 segPtBLoc = float3(0.0f, 0.0f, 0.0f);
        nwbPhysicsClosestPointsSegments(bestEA0, bestEA1, bestEB0, bestEB1, sSeg, tSeg, segPtALoc, segPtBLoc);
        const float3 worldAxis = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(bestEdgeAxis, rotA), float3(0.0f, 1.0f, 0.0f));
        const float3 segPtA = originA + nwbPhysicsQuatRotate(segPtALoc, rotA);
        const float3 segPtB = originA + nwbPhysicsQuatRotate(segPtBLoc, rotA);
        result.pairInfo.z = 1u;
        result.normalPad = float4(worldAxis, 0.0f);
        result.anchorAFeature[0] = float4(segPtA + worldAxis * rA - bodyPosA, asfloat(50u + (bestEdgeIdxA & 0xffu) + ((bestEdgeIdxB & 0xffu) << 8u)));
        result.anchorBSeparation[0] = float4(segPtB - worldAxis * rB - bodyPosB, bestEdgeSep);
        return;
    }

    const bool refIsA = (bestSepB <= bestSepA + 1.0e-4f);
    const NwbPhysicsGpuShapeProxy refProxy = refIsA ? proxyA : proxyB;
    const NwbPhysicsGpuShapeProxy incProxy = refIsA ? proxyB : proxyA;
    const float3 refOrigLoc = refIsA ? float3(0.0f, 0.0f, 0.0f) : originBInA;
    const float4 refRotLoc = refIsA ? float4(0.0f, 0.0f, 0.0f, 1.0f) : rotBToA;
    const float3 incOrigLoc = refIsA ? originBInA : float3(0.0f, 0.0f, 0.0f);
    const float4 incRotLoc = refIsA ? rotBToA : float4(0.0f, 0.0f, 0.0f, 1.0f);
    const uint refFace = refIsA ? bestFaceA : bestFaceB;
    const uint incFaceCount = refIsA ? fCountB : fCountA;
    const float3 refLocalN = refIsA ? bestLocalNA : bestLocalNB;
    const float3 refNInInc = refIsA ? float3(dot(bAxX, bestLocalNA), dot(bAxY, bestLocalNA), dot(bAxZ, bestLocalNA)) : bestLocalNB;
    const float refR = refIsA ? rA : rB;
    const float incR = refIsA ? rB : rA;

    uint incFace = 0u;
    float minIncDot = 2.0f;
    for(uint i = 0u; i < incFaceCount; ++i){
        const float4 lp = nwbPhysicsHullLocalFacePlane(incProxy, hullVertexSlot, i);
        const float d = dot(lp.xyz, refNInInc);
        if(d < minIncDot){
            minIncDot = d;
            incFace = i;
        }
    }

    float4 refPoly[16];
    float3 refNUnused = refLocalN;
    float refOff = 0.0f;
    const uint refPolyCount = nwbPhysicsGatherHullWorldFacePoly(refProxy, refOrigLoc, refRotLoc, hullVertexSlot, refFace, refPoly, refNUnused, refOff);

    float4 poly0[16];
    float4 poly1[16];
    float3 incNUnused = refLocalN;
    float incOffUnused = 0.0f;
    uint polyCount = nwbPhysicsGatherHullWorldFacePoly(incProxy, incOrigLoc, incRotLoc, hullVertexSlot, incFace, poly0, incNUnused, incOffUnused);

    if(refPolyCount < 3u || polyCount < 3u){
        result.pairInfo.w = 1u;
        return;
    }

    bool clipOverflow = false;
    const float3 v0First = refPoly[0].xyz;
    const float3 v1First = refPoly[1].xyz;
    const float3 sideNFirst = nwbPhysicsSafeNormal(cross(v1First - v0First, refNUnused), float3(1.0f, 0.0f, 0.0f));
    polyCount = nwbPhysicsClipPolygonPlane(poly0, polyCount, sideNFirst, dot(sideNFirst, v0First), 0u, poly1, clipOverflow);
    bool polyIn0 = false;
    for(uint i = 1u; i < refPolyCount && !clipOverflow && polyCount > 0u; ++i){
        const float3 v0 = refPoly[i].xyz;
        const float3 v1 = refPoly[(i + 1u < refPolyCount) ? (i + 1u) : 0u].xyz;
        const float3 sideN = nwbPhysicsSafeNormal(cross(v1 - v0, refNUnused), float3(1.0f, 0.0f, 0.0f));
        const float sideOff = dot(sideN, v0);
        if(polyIn0)
            polyCount = nwbPhysicsClipPolygonPlane(poly0, polyCount, sideN, sideOff, i, poly1, clipOverflow);
        else
            polyCount = nwbPhysicsClipPolygonPlane(poly1, polyCount, sideN, sideOff, i, poly0, clipOverflow);
        polyIn0 = !polyIn0;
    }
    if(clipOverflow || polyCount == 0u){
        result.pairInfo.w = 1u;
        return;
    }

    const uint hullFacePack = ((refIsA ? 1u : 0u) << 28u) | ((refFace & 0x3fu) << 22u) | ((incFace & 0x3fu) << 16u);
    float3 candPos[NWB_PHYSICS_GPU_MAX_MESH_CANDIDATES];
    float candSep[NWB_PHYSICS_GPU_MAX_MESH_CANDIDATES];
    uint candFeat[NWB_PHYSICS_GPU_MAX_MESH_CANDIDATES];
    uint candCount = 0u;
    for(uint k = 0u; k < polyCount; ++k){
        const float4 clipped = polyIn0 ? poly0[k] : poly1[k];
        const float3 pt = clipped.xyz;
        const float sep = dot(refNUnused, pt) - refOff - totalR;
        if(sep <= maxDist){
            if(candCount >= NWB_PHYSICS_GPU_MAX_MESH_CANDIDATES){
                clipOverflow = true;
            }
            else{
                candPos[candCount] = pt;
                candSep[candCount] = sep;
                candFeat[candCount] = hullFacePack | (asuint(clipped.w) & 0xffffu);
                ++candCount;
            }
        }
    }
    if(clipOverflow || candCount == 0u){
        result.pairInfo.w = 1u;
        return;
    }

    const float3 worldRefN = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(refNUnused, rotA), float3(0.0f, 1.0f, 0.0f));
    result.normalPad = float4(refIsA ? worldRefN : -worldRefN, 0.0f);
    nwbPhysicsReduceManifoldTo4(originA, rotA, refNUnused, worldRefN, candPos, candSep, candFeat, candCount, refIsA, refR, incR, totalR, bodyPosA, bodyPosB, result);
}

void nwbPhysicsPushMeshCand(
    const float3 wA,
    const float3 wB,
    const float3 wNormalAtoB,
    const float sep,
    const uint feat,
    inout float3 candWA[NWB_PHYSICS_MAX_MANIFOLD_POINTS],
    inout float3 candWB[NWB_PHYSICS_MAX_MANIFOLD_POINTS],
    inout float3 candNorm[NWB_PHYSICS_MAX_MANIFOLD_POINTS],
    inout float candSep[NWB_PHYSICS_MAX_MANIFOLD_POINTS],
    inout uint candFeat[NWB_PHYSICS_MAX_MANIFOLD_POINTS],
    inout uint candCount
){
    if(candCount < NWB_PHYSICS_MAX_MANIFOLD_POINTS){
        const uint slot = candCount;
        ++candCount;
        candWA[slot] = wA;
        candWB[slot] = wB;
        candNorm[slot] = wNormalAtoB;
        candSep[slot] = sep;
        candFeat[slot] = feat;
        return;
    }

    const float3 pts[5] = {
        (candWA[0] + candWB[0]) * 0.5f,
        (candWA[1] + candWB[1]) * 0.5f,
        (candWA[2] + candWB[2]) * 0.5f,
        (candWA[3] + candWB[3]) * 0.5f,
        (wA + wB) * 0.5f
    };
    const float seps[5] = { candSep[0], candSep[1], candSep[2], candSep[3], sep };
    uint deepIdx = 0u;
    for(uint i = 1u; i < 5u; ++i){
        if(seps[i] < seps[deepIdx])
            deepIdx = i;
    }
    const float3 deepN = (deepIdx == 4u) ? wNormalAtoB : candNorm[deepIdx];
    const uint alignedMask =
        (dot(candNorm[0], deepN) > 0.7f ? 1u : 0u) |
        (dot(candNorm[1], deepN) > 0.7f ? 2u : 0u) |
        (dot(candNorm[2], deepN) > 0.7f ? 4u : 0u) |
        (dot(candNorm[3], deepN) > 0.7f ? 8u : 0u) |
        (dot(wNormalAtoB, deepN) > 0.7f ? 16u : 0u);

    uint bestDrop = 4u;
    uint bestDropAligned = 2u;
    float bestAreaSq = -1.0f;
    for(uint skip = 0u; skip < 5u; ++skip){
        if(skip == deepIdx)
            continue;
        const uint dropAligned = (alignedMask >> skip) & 1u;
        const float3 q0 = pts[(skip <= 0u) ? 1u : 0u];
        const float3 q1 = pts[(skip <= 1u) ? 2u : 1u];
        const float3 q2 = pts[(skip <= 2u) ? 3u : 2u];
        const float3 q3 = pts[(skip <= 3u) ? 4u : 3u];
        const float3 c0 = cross(q1 - q0, q3 - q2);
        const float3 c1 = cross(q2 - q0, q1 - q3);
        const float3 c2 = cross(q3 - q0, q1 - q2);
        const float areaSq = max(dot(c0, c0), max(dot(c1, c1), dot(c2, c2)));
        if(
            dropAligned < bestDropAligned ||
            (dropAligned == bestDropAligned && (areaSq > bestAreaSq || (areaSq == bestAreaSq && seps[skip] > seps[bestDrop])))
        ){
            bestDropAligned = dropAligned;
            bestAreaSq = areaSq;
            bestDrop = skip;
        }
    }

    if(bestDrop < 4u){
        candWA[bestDrop] = wA;
        candWB[bestDrop] = wB;
        candNorm[bestDrop] = wNormalAtoB;
        candSep[bestDrop] = sep;
        candFeat[bestDrop] = feat;
    }
}

void nwbPhysicsFinalizeMeshCand(
    const float3 candWA[NWB_PHYSICS_MAX_MANIFOLD_POINTS],
    const float3 candWB[NWB_PHYSICS_MAX_MANIFOLD_POINTS],
    const float3 candNorm[NWB_PHYSICS_MAX_MANIFOLD_POINTS],
    const float candSep[NWB_PHYSICS_MAX_MANIFOLD_POINTS],
    const uint candFeat[NWB_PHYSICS_MAX_MANIFOLD_POINTS],
    const uint candCount,
    const float3 chosenWorldN,
    const float3 bodyPosA,
    const float3 bodyPosB,
    inout NwbPhysicsGpuNarrowphaseManifold result
){
    if(candCount == 0u)
        return;

    float3 useNormal = chosenWorldN;
    uint outCount = 0u;
    for(uint i = 0u; i < candCount; ++i){
        if(dot(candNorm[i], useNormal) > 0.7f){
            result.anchorAFeature[outCount] = float4(candWA[i] - bodyPosA, asfloat(candFeat[i]));
            result.anchorBSeparation[outCount] = float4(candWB[i] - bodyPosB, candSep[i]);
            ++outCount;
        }
    }

    if(outCount == 0u){
        uint bestIdx = 0u;
        for(uint i = 1u; i < candCount; ++i){
            if(candSep[i] < candSep[bestIdx])
                bestIdx = i;
        }
        useNormal = candNorm[bestIdx];
        result.anchorAFeature[0] = float4(candWA[bestIdx] - bodyPosA, asfloat(candFeat[bestIdx]));
        result.anchorBSeparation[0] = float4(candWB[bestIdx] - bodyPosB, candSep[bestIdx]);
        outCount = 1u;
    }

    result.normalPad = float4(useNormal, 0.0f);
    result.pairInfo.z = outCount;
}

static const float2 kNwbPhysicsOctagonCosSin[8] = {
    float2(1.0f, 0.0f),
    float2(0.7071067811865475f, 0.7071067811865475f),
    float2(0.0f, 1.0f),
    float2(-0.7071067811865475f, 0.7071067811865475f),
    float2(-1.0f, 0.0f),
    float2(-0.7071067811865475f, -0.7071067811865475f),
    float2(0.0f, -1.0f),
    float2(0.7071067811865475f, -0.7071067811865475f)
};

void nwbPhysicsBuildOctagonCapPatch(
    const float3 center,
    const float3 normal,
    const float radius,
    out float4 outVerts[NWB_PHYSICS_GPU_MAX_CLIP_POINTS]
){
    float3 b1;
    float3 b2;
    nwbPhysicsComputeTangentBasis(normal, b1, b2);
    [loop] for(uint k = 0u; k < 8u; ++k){
        const float2 cs = kNwbPhysicsOctagonCosSin[k];
        const float3 pt = center + (b1 * cs.x + b2 * cs.y) * radius;
        outVerts[k] = float4(pt, asfloat(k));
    }
}

uint nwbPhysicsBuildProxyPlanarPatchWorld(
    const NwbPhysicsGpuShapeProxy proxy,
    const float3 origin,
    const float4 rot,
    const uint hullVertexSlot,
    const float3 worldDir,
    out float4 outVerts[NWB_PHYSICS_GPU_MAX_CLIP_POINTS],
    out float3 outPatchNormal
){
    outVerts[0] = float4(0.0f, 0.0f, 0.0f, 0.0f);
    outPatchNormal = nwbPhysicsSafeNormal(worldDir, float3(0.0f, 1.0f, 0.0f));

    const float3 dir = outPatchNormal;
    const uint shapeType = proxy.bodyIndexTypeShape.y;
    const bool isBox = (shapeType == NWB_PHYSICS_SHAPE_TYPE_HULL) && ((proxy.bodyIndexTypeShape.w & NWB_PHYSICS_SHAPE_FLAG_IS_BOX) != 0u);

    if(shapeType == NWB_PHYSICS_SHAPE_TYPE_CAPSULE || shapeType == NWB_PHYSICS_SHAPE_TYPE_SEGMENT){
        const float3 p0 = origin + nwbPhysicsQuatRotate(proxy.localCenterRadius.xyz, rot);
        const float3 p1 = origin + nwbPhysicsQuatRotate(proxy.localExtentOrP1.xyz, rot);
        const float3 seg = p1 - p0;
        const float lenSq = dot(seg, seg);
        if(lenSq > 1.0e-12f){
            const float3 u = seg * rsqrt(lenSq);
            if(abs(dot(u, dir)) <= 0.12f){
                outVerts[0] = float4(p0, asfloat(0u));
                outVerts[1] = float4(p1, asfloat(1u));
                return 2u;
            }
        }
        return 0u;
    }

    if(shapeType == NWB_PHYSICS_SHAPE_TYPE_TAPERED_CAPSULE){
        const float3 p0 = origin + nwbPhysicsQuatRotate(proxy.localCenterRadius.xyz, rot);
        const float3 p1 = origin + nwbPhysicsQuatRotate(proxy.localExtentOrP1.xyz, rot);
        const float3 seg = p1 - p0;
        const float lenSq = dot(seg, seg);
        if(lenSq > 1.0e-12f){
            const float L = sqrt(lenSq);
            const float dR0 = proxy.localRotationOrMeta.x;
            const float dR1 = proxy.localRotationOrMeta.y;
            if(abs(dR0 - dR1) < L - 1.0e-5f){
                const float3 u = seg / L;
                const float targetCos = (dR0 - dR1) / L;
                if(abs(dot(u, dir) - targetCos) <= 0.08f){
                    outVerts[0] = float4(p0 + dir * dR0, asfloat(0u));
                    outVerts[1] = float4(p1 + dir * dR1, asfloat(1u));
                    return 2u;
                }
            }
        }
        return 0u;
    }

    if(shapeType == NWB_PHYSICS_SHAPE_TYPE_CYLINDER){
        const float4 qCyl = (proxy.localRotationOrMeta.w >= 0.999999f)
            ? rot
            : nwbPhysicsQuatNormalize(nwbPhysicsQuatMul(rot, proxy.localRotationOrMeta));
        const float3 c = origin + nwbPhysicsQuatRotate(proxy.localCenterRadius.xyz, rot);
        float3 axX;
        float3 axY;
        float3 axZ;
        nwbPhysicsQuatToAxes(qCyl, axX, axY, axZ);
        const float rCyl = proxy.localExtentOrP1.x;
        const float hhCyl = proxy.localExtentOrP1.y;
        const float dAxis = dot(axY, dir);
        if(abs(dAxis) >= 0.96f){
            const bool isTop = (dAxis >= 0.0f);
            const float3 capN = isTop ? axY : -axY;
            const float3 capCenter = c + capN * hhCyl;
            nwbPhysicsBuildOctagonCapPatch(capCenter, capN, rCyl, outVerts);
            outPatchNormal = capN;
            return 8u;
        }
        if(abs(dAxis) <= 0.08f){
            const float3 radDir = nwbPhysicsSafeNormal(dir - axY * dAxis, axX);
            const float3 edgeCenter = c + radDir * rCyl;
            outPatchNormal = radDir;
            outVerts[0] = float4(edgeCenter - axY * hhCyl, asfloat(0u));
            outVerts[1] = float4(edgeCenter + axY * hhCyl, asfloat(1u));
            return 2u;
        }
        return 0u;
    }

    if(shapeType == NWB_PHYSICS_SHAPE_TYPE_CONE){
        const float4 qCone = (proxy.localRotationOrMeta.w >= 0.999999f)
            ? rot
            : nwbPhysicsQuatNormalize(nwbPhysicsQuatMul(rot, proxy.localRotationOrMeta));
        const float3 c = origin + nwbPhysicsQuatRotate(proxy.localCenterRadius.xyz, rot);
        float3 axX;
        float3 axY;
        float3 axZ;
        nwbPhysicsQuatToAxes(qCone, axX, axY, axZ);
        const float rCone = max(proxy.localExtentOrP1.x, 0.0f);
        const float hhCone = max(proxy.localExtentOrP1.y, 0.0f);
        const float dAxis = dot(axY, dir);
        if(dAxis <= -0.96f){
            const float3 baseN = -axY;
            const float3 baseCenter = c - axY * hhCone;
            nwbPhysicsBuildOctagonCapPatch(baseCenter, baseN, rCone, outVerts);
            outPatchNormal = baseN;
            return 8u;
        }
        const float slantDenom = sqrt(rCone * rCone + 4.0f * hhCone * hhCone);
        if(slantDenom > 1.0e-6f){
            const float targetCos = rCone / slantDenom;
            if(abs(dAxis - targetCos) <= 0.08f){
                const float3 radDir = nwbPhysicsSafeNormal(dir - axY * dAxis, axX);
                outPatchNormal = nwbPhysicsSafeNormal(radDir * (2.0f * hhCone) + axY * rCone, dir);
                outVerts[0] = float4(c + axY * hhCone, asfloat(0u));
                outVerts[1] = float4(c - axY * hhCone + radDir * rCone, asfloat(1u));
                return 2u;
            }
        }
        return 0u;
    }

    bool gatherHullFace = false;
    uint bestHullFace = 0u;
    if(isBox){
        const float4 qBox = (proxy.localRotationOrMeta.w >= 0.999999f)
            ? rot
            : nwbPhysicsQuatNormalize(nwbPhysicsQuatMul(rot, proxy.localRotationOrMeta));
        float3 ax;
        float3 ay;
        float3 az;
        nwbPhysicsQuatToAxes(qBox, ax, ay, az);
        const float dx = dot(ax, dir);
        const float dy = dot(ay, dir);
        const float dz = dot(az, dir);
        bestHullFace = 0u;
        float bestDot = dx;
        if(-dx > bestDot){ bestDot = -dx; bestHullFace = 1u; }
        if(dy > bestDot){ bestDot = dy; bestHullFace = 2u; }
        if(-dy > bestDot){ bestDot = -dy; bestHullFace = 3u; }
        if(dz > bestDot){ bestDot = dz; bestHullFace = 4u; }
        if(-dz > bestDot){ bestDot = -dz; bestHullFace = 5u; }
        gatherHullFace = (bestDot >= 0.96f);
    }
    else if(shapeType == NWB_PHYSICS_SHAPE_TYPE_HULL){
        const uint vOffset = proxy.filterData.w;
        const uint fCount = nwbPhysicsHullFaceCount(proxy);
        if(hullVertexSlot != NWB_PHYSICS_INVALID_INDEX && vOffset != NWB_PHYSICS_INVALID_INDEX && fCount > 0u && fCount <= NWB_PHYSICS_MAX_HULL_FACES){
            float bestDot = -2.0f;
            [loop] for(uint fi = 0u; fi < fCount; ++fi){
                const float4 lp = nwbPhysicsHullLocalFacePlane(proxy, hullVertexSlot, fi);
                const float3 wn = nwbPhysicsQuatRotate(lp.xyz, rot);
                const float d = dot(wn, dir);
                if(d > bestDot){
                    bestDot = d;
                    bestHullFace = fi;
                }
            }
            gatherHullFace = (bestDot >= 0.96f);
        }
    }
    if(gatherHullFace){
        float unusedOff = 0.0f;
        return nwbPhysicsGatherHullWorldFacePoly(proxy, origin, rot, hullVertexSlot, bestHullFace, outVerts, outPatchNormal, unusedOff);
    }

    return 0u;
}

bool nwbPhysicsClipSegmentToPolygon(
    const float3 seg0,
    const float3 seg1,
    const float4 poly[NWB_PHYSICS_GPU_MAX_CLIP_POINTS],
    const uint polyCount,
    const float3 polyN,
    const float tol,
    out float3 outP0,
    out float3 outP1,
    out uint outCount
){
    outP0 = seg0;
    outP1 = seg1;
    outCount = 0u;
    if(polyCount < 3u)
        return false;

    float tMin = 0.0f;
    float tMax = 1.0f;
    const float3 segDir = seg1 - seg0;
    [loop] for(uint i = 0u; i < polyCount; ++i){
        const float3 e0 = poly[i].xyz;
        const float3 e1 = poly[(i + 1u < polyCount) ? (i + 1u) : 0u].xyz;
        float3 sideN = nwbPhysicsSafeNormal(cross(e1 - e0, polyN), float3(1.0f, 0.0f, 0.0f));
        const float numer = -(dot(sideN, seg0 - e0) - tol);
        const float denom = dot(sideN, segDir);
        if(abs(denom) <= 1.0e-12f){
            if(numer < 0.0f)
                return false;
        }
        else if(denom < 0.0f){
            tMin = max(tMin, numer / denom);
        }
        else{
            tMax = min(tMax, numer / denom);
        }
        if(tMin > tMax)
            return false;
    }

    outP0 = lerp(seg0, seg1, tMin);
    if(tMax - tMin > 1.0e-4f){
        outP1 = lerp(seg0, seg1, tMax);
        outCount = 2u;
    }
    else{
        outP1 = outP0;
        outCount = 1u;
    }
    return true;
}

void nwbPhysicsClipPolyAndPushPlanarManifold(
    const float4 refPoly[NWB_PHYSICS_GPU_MAX_CLIP_POINTS],
    const uint refCount,
    const float3 refN,
    inout float4 incPoly[NWB_PHYSICS_GPU_MAX_CLIP_POINTS],
    const uint incCount,
    const float incR,
    const float totalR,
    const float maxDist,
    const float3 norm,
    const bool refIsA,
    inout float3 candWA[NWB_PHYSICS_MAX_MANIFOLD_POINTS],
    inout float3 candWB[NWB_PHYSICS_MAX_MANIFOLD_POINTS],
    inout float3 candNorm[NWB_PHYSICS_MAX_MANIFOLD_POINTS],
    inout float candSep[NWB_PHYSICS_MAX_MANIFOLD_POINTS],
    inout uint candFeat[NWB_PHYSICS_MAX_MANIFOLD_POINTS],
    inout uint candCount
){
    float4 clipTmp[NWB_PHYSICS_GPU_MAX_CLIP_POINTS];
    const float3 refOriginPt = refPoly[0].xyz;
    const float3 v0First = refOriginPt;
    const float3 v1First = refPoly[1].xyz;
    const float3 sideNFirst = nwbPhysicsSafeNormal(cross(v1First - v0First, refN), float3(1.0f, 0.0f, 0.0f));
    bool clipOverflow = false;
    uint polyCount = nwbPhysicsClipPolygonPlane(incPoly, incCount, sideNFirst, dot(sideNFirst, v0First), 0u, clipTmp, clipOverflow);
    bool inTmp = true;
    [loop] for(uint i = 1u; i < refCount && !clipOverflow && polyCount > 0u; ++i){
        const float3 v0 = refPoly[i].xyz;
        const float3 v1 = refPoly[(i + 1u < refCount) ? (i + 1u) : 0u].xyz;
        const float3 sideN = nwbPhysicsSafeNormal(cross(v1 - v0, refN), float3(1.0f, 0.0f, 0.0f));
        const float sideOff = dot(sideN, v0);
        if(!inTmp)
            polyCount = nwbPhysicsClipPolygonPlane(incPoly, polyCount, sideN, sideOff, i, clipTmp, clipOverflow);
        else
            polyCount = nwbPhysicsClipPolygonPlane(clipTmp, polyCount, sideN, sideOff, i, incPoly, clipOverflow);
        inTmp = !inTmp;
    }
    if(!clipOverflow && polyCount > 0u){
        [loop] for(uint k = 0u; k < polyCount; ++k){
            const float4 cv = inTmp ? clipTmp[k] : incPoly[k];
            const float planeDist = dot(refN, cv.xyz - refOriginPt);
            const float sep = planeDist - totalR;
            if(sep <= maxDist){
                const float3 wRef = cv.xyz - refN * (sep + incR);
                const float3 wInc = cv.xyz - refN * incR;
                nwbPhysicsPushMeshCand(refIsA ? wRef : wInc, refIsA ? wInc : wRef, norm, sep, asuint(cv.w), candWA, candWB, candNorm, candSep, candFeat, candCount);
            }
        }
    }
}

void nwbPhysicsClipSegAndPushPlanarManifold(
    const float4 refPoly[NWB_PHYSICS_GPU_MAX_CLIP_POINTS],
    const uint refCount,
    const float3 refN,
    const float3 s0,
    const float3 s1,
    const float incR,
    const float totalR,
    const float maxDist,
    const float3 norm,
    const bool refIsA,
    inout float3 candWA[NWB_PHYSICS_MAX_MANIFOLD_POINTS],
    inout float3 candWB[NWB_PHYSICS_MAX_MANIFOLD_POINTS],
    inout float3 candNorm[NWB_PHYSICS_MAX_MANIFOLD_POINTS],
    inout float candSep[NWB_PHYSICS_MAX_MANIFOLD_POINTS],
    inout uint candFeat[NWB_PHYSICS_MAX_MANIFOLD_POINTS],
    inout uint candCount
){
    float3 c0 = float3(0.0f, 0.0f, 0.0f);
    float3 c1 = float3(0.0f, 0.0f, 0.0f);
    uint cCnt = 0u;
    if(nwbPhysicsClipSegmentToPolygon(s0, s1, refPoly, refCount, refN, 1.0e-3f, c0, c1, cCnt)){
        const float3 refOriginPt = refPoly[0].xyz;
        const float3 clipPts[2] = { c0, c1 };
        [loop] for(uint k = 0u; k < cCnt; ++k){
            const float planeDist = dot(refN, clipPts[k] - refOriginPt);
            const float sep = planeDist - totalR;
            if(sep <= maxDist){
                const float3 wRef = clipPts[k] - refN * (sep + incR);
                const float3 wInc = clipPts[k] - refN * incR;
                nwbPhysicsPushMeshCand(refIsA ? wRef : wInc, refIsA ? wInc : wRef, norm, sep, 30u + k, candWA, candWB, candNorm, candSep, candFeat, candCount);
            }
        }
    }
}

void nwbPhysicsTriEdgeClipPlane(
    const float3 ea,
    const float3 eb,
    const float3 eOpp,
    const float3 triN,
    out float3 outSn,
    out float outOff
){
    outSn = nwbPhysicsSafeNormal(cross(eb - ea, triN), float3(1.0f, 0.0f, 0.0f));
    if(dot(outSn, eOpp - ea) > 0.0f)
        outSn = -outSn;
    outOff = dot(outSn, ea) + 5.0e-3f;
}

bool nwbPhysicsBuildPlanarManifold(
    const NwbPhysicsGpuShapeProxy proxyA,
    const float3 originA,
    const float4 rotA,
    const float3 bodyPosA,
    const NwbPhysicsGpuShapeProxy proxyB,
    const float3 originB,
    const float4 rotB,
    const float3 bodyPosB,
    const uint hullVertexSlot,
    const float3 normalAtoB,
    const float maxDist,
    inout NwbPhysicsGpuNarrowphaseManifold result
){
    const float3 norm = nwbPhysicsSafeNormal(normalAtoB, float3(0.0f, 1.0f, 0.0f));
    float4 polyA[NWB_PHYSICS_GPU_MAX_CLIP_POINTS];
    float4 polyB[NWB_PHYSICS_GPU_MAX_CLIP_POINTS];
    float3 patchNA = norm;
    float3 patchNB = -norm;
    const uint cntA = nwbPhysicsBuildProxyPlanarPatchWorld(proxyA, originA, rotA, hullVertexSlot, norm, polyA, patchNA);
    const uint cntB = nwbPhysicsBuildProxyPlanarPatchWorld(proxyB, originB, rotB, hullVertexSlot, -norm, polyB, patchNB);
    if(cntA < 2u || cntB < 2u)
        return false;

    const float rA = proxyA.localCenterRadius.w;
    const float rB = proxyB.localCenterRadius.w;
    const float totalR = rA + rB;

    float3 candWA[NWB_PHYSICS_MAX_MANIFOLD_POINTS];
    float3 candWB[NWB_PHYSICS_MAX_MANIFOLD_POINTS];
    float3 candNorm[NWB_PHYSICS_MAX_MANIFOLD_POINTS];
    float candSep[NWB_PHYSICS_MAX_MANIFOLD_POINTS];
    uint candFeat[NWB_PHYSICS_MAX_MANIFOLD_POINTS];
    [loop] for(uint kInit = 0u; kInit < NWB_PHYSICS_MAX_MANIFOLD_POINTS; ++kInit){
        candWA[kInit] = float3(0.0f, 0.0f, 0.0f);
        candWB[kInit] = float3(0.0f, 0.0f, 0.0f);
        candNorm[kInit] = norm;
        candSep[kInit] = 0.0f;
        candFeat[kInit] = 0u;
    }
    uint candCount = 0u;

    if(cntA >= 3u && cntB >= 3u){
        const bool refIsA = abs(dot(patchNA, norm)) >= abs(dot(patchNB, -norm));
        if(refIsA)
            nwbPhysicsClipPolyAndPushPlanarManifold(polyA, cntA, patchNA, polyB, cntB, rB, totalR, maxDist, norm, true, candWA, candWB, candNorm, candSep, candFeat, candCount);
        else
            nwbPhysicsClipPolyAndPushPlanarManifold(polyB, cntB, patchNB, polyA, cntA, rA, totalR, maxDist, norm, false, candWA, candWB, candNorm, candSep, candFeat, candCount);
    }
    else if(cntA >= 3u){
        nwbPhysicsClipSegAndPushPlanarManifold(polyA, cntA, patchNA, polyB[0].xyz, polyB[1].xyz, rB, totalR, maxDist, norm, true, candWA, candWB, candNorm, candSep, candFeat, candCount);
    }
    else if(cntB >= 3u){
        nwbPhysicsClipSegAndPushPlanarManifold(polyB, cntB, patchNB, polyA[0].xyz, polyA[1].xyz, rA, totalR, maxDist, norm, false, candWA, candWB, candNorm, candSep, candFeat, candCount);
    }
    else{
        const float3 a0 = polyA[0].xyz;
        const float3 a1 = polyA[1].xyz;
        const float3 b0 = polyB[0].xyz;
        const float3 b1 = polyB[1].xyz;
        const float3 segA = a1 - a0;
        const float segALenSq = dot(segA, segA);
        const float3 segB = b1 - b0;
        const float segBLenSq = dot(segB, segB);
        if(segALenSq > 1.0e-12f && segBLenSq > 1.0e-12f){
            const float3 uA = segA * rsqrt(segALenSq);
            const float3 uB = segB * rsqrt(segBLenSq);
            if(abs(dot(uA, uB)) >= 0.96f){
                const float invLenA = 1.0f / segALenSq;
                const float tb0 = dot(b0 - a0, segA) * invLenA;
                const float tb1 = dot(b1 - a0, segA) * invLenA;
                const float tLo = clamp(min(tb0, tb1), 0.0f, 1.0f);
                const float tHi = clamp(max(tb0, tb1), 0.0f, 1.0f);
                if(tHi >= tLo){
                    const float ts[2] = { tLo, tHi };
                    const uint tCount = (tHi - tLo > 1.0e-4f) ? 2u : 1u;
                    const float spanB = tb1 - tb0;
                    [loop] for(uint k = 0u; k < tCount; ++k){
                        const float3 pOnA = a0 + segA * ts[k];
                        const float uBParam = (abs(spanB) > 1.0e-6f) ? clamp((ts[k] - tb0) / spanB, 0.0f, 1.0f) : 0.5f;
                        const float3 pOnB = lerp(b0, b1, uBParam);
                        const float sep = dot(norm, pOnB - pOnA) - totalR;
                        if(sep <= maxDist){
                            const float3 wA = pOnA + norm * rA;
                            const float3 wB = pOnB - norm * rB;
                            nwbPhysicsPushMeshCand(wA, wB, norm, sep, 40u + k, candWA, candWB, candNorm, candSep, candFeat, candCount);
                        }
                    }
                }
            }
        }
    }

    if(candCount == 0u)
        return false;

    nwbPhysicsFinalizeMeshCand(candWA, candWB, candNorm, candSep, candFeat, candCount, norm, bodyPosA, bodyPosB, result);
    result.normalPad.w = asfloat(NWB_PHYSICS_INVALID_INDEX);
    return result.pairInfo.z > 0u;
}

bool nwbPhysicsTryPushTrianglePlanarPatch(
    const NwbPhysicsGpuShapeProxy otherP,
    const float3 relOrig,
    const float4 relRot,
    const float3 surfOrig,
    const float4 surfRot,
    const bool surfIsA,
    const float otherR,
    const float surfR,
    const uint hullVertexSlot,
    const float3 v0,
    const float3 v1,
    const float3 v2,
    const float3 triN,
    const float3 nOtherToSurfL,
    const float3 worldWinN,
    const float3 wNOut,
    const uint featureTriTag,
    const uint matTriTag,
    const float maxDist,
    inout float bestSep,
    inout float3 chosenWorldN,
    inout uint bestMatTri,
    inout float3 candWA[NWB_PHYSICS_MAX_MANIFOLD_POINTS],
    inout float3 candWB[NWB_PHYSICS_MAX_MANIFOLD_POINTS],
    inout float3 candNorm[NWB_PHYSICS_MAX_MANIFOLD_POINTS],
    inout float candSep[NWB_PHYSICS_MAX_MANIFOLD_POINTS],
    inout uint candFeat[NWB_PHYSICS_MAX_MANIFOLD_POINTS],
    inout uint candCount
){
    if(abs(dot(nOtherToSurfL, triN)) < 0.96f)
        return false;

    float4 buf0[NWB_PHYSICS_GPU_MAX_CLIP_POINTS];
    float3 patchN = nOtherToSurfL;
    const uint patchCnt = nwbPhysicsBuildProxyPlanarPatchWorld(otherP, relOrig, relRot, hullVertexSlot, nOtherToSurfL, buf0, patchN);
    if(patchCnt < 2u)
        return false;

    const float totalR = otherR + surfR;
    const float3 planeN = -nOtherToSurfL;
    const float3 triVerts[3] = { v0, v1, v2 };
    bool pushedAny = false;

    float4 buf1[NWB_PHYSICS_GPU_MAX_CLIP_POINTS];
    uint emitCnt = 0u;
    bool inBuf1 = false;
    if(patchCnt >= 3u){
        bool clipOvf = false;
        float3 sn0;
        float off0 = 0.0f;
        nwbPhysicsTriEdgeClipPlane(v0, v1, v2, triN, sn0, off0);
        uint clipCnt = nwbPhysicsClipPolygonPlane(buf0, patchCnt, sn0, off0, 0u, buf1, clipOvf);
        inBuf1 = true;
        [loop] for(uint ek = 1u; ek < 3u && !clipOvf && clipCnt > 0u; ++ek){
            const float3 ea = triVerts[ek];
            const float3 eb = triVerts[(ek + 1u < 3u) ? (ek + 1u) : 0u];
            const float3 eOpp = triVerts[(ek + 2u) % 3u];
            float3 sn;
            float off = 0.0f;
            nwbPhysicsTriEdgeClipPlane(ea, eb, eOpp, triN, sn, off);
            if(!inBuf1)
                clipCnt = nwbPhysicsClipPolygonPlane(buf0, clipCnt, sn, off, ek, buf1, clipOvf);
            else
                clipCnt = nwbPhysicsClipPolygonPlane(buf1, clipCnt, sn, off, ek, buf0, clipOvf);
            inBuf1 = !inBuf1;
        }
        emitCnt = (!clipOvf) ? clipCnt : 0u;
    }
    else{
        buf1[0] = float4(v0, 0.0f);
        buf1[1] = float4(v1, 0.0f);
        buf1[2] = float4(v2, 0.0f);
        float3 c0 = float3(0.0f, 0.0f, 0.0f);
        float3 c1 = float3(0.0f, 0.0f, 0.0f);
        uint cCnt = 0u;
        if(nwbPhysicsClipSegmentToPolygon(buf0[0].xyz, buf0[1].xyz, buf1, 3u, triN, 5.0e-3f, c0, c1, cCnt)){
            buf0[0] = float4(c0, 0.0f);
            buf0[1] = float4(c1, 0.0f);
            emitCnt = cCnt;
        }
    }

    [loop] for(uint k = 0u; k < emitCnt; ++k){
        const float3 clippedPt = (inBuf1 ? buf1[k] : buf0[k]).xyz;
        const float ptSep = dot(planeN, clippedPt - v0) - totalR;
        if(ptSep <= maxDist){
            const float3 wO = surfOrig + nwbPhysicsQuatRotate(clippedPt, surfRot) + worldWinN * otherR;
            const float3 wM = wO + worldWinN * ptSep;
            const float3 wAOut = surfIsA ? wM : wO;
            const float3 wBOut = surfIsA ? wO : wM;
            if(ptSep < bestSep){
                bestSep = ptSep;
                chosenWorldN = wNOut;
                bestMatTri = matTriTag;
            }
            nwbPhysicsPushMeshCand(wAOut, wBOut, wNOut, ptSep, nwbPhysicsMakeFeatureId(4u, k & 0xffu, 3u, featureTriTag & 0xffu), candWA, candWB, candNorm, candSep, candFeat, candCount);
            pushedAny = true;
        }
    }
    return pushedAny;
}

uint nwbPhysicsCollideConvexWithSurfaceTriangle(
    const NwbPhysicsGpuShapeProxy otherP,
    const NwbPhysicsGpuSupportCache otherCache,
    const float3 relOrig,
    const float4 relRot,
    const float3 surfOrig,
    const float4 surfRot,
    const bool surfIsA,
    const float otherR,
    const float surfR,
    const uint hullVertexSlot,
    const uint meshEdgeFlags,
    const bool useHfEdgeFlags,
    const uint hfBase,
    const uint cols,
    const uint rows,
    const float3 hfLocalOrigin,
    const float sx,
    const float sz,
    const float sy,
    const uint hfFlags,
    const uint cx,
    const uint cz,
    const uint hfTriIdx,
    const float3 hfV00,
    const float3 hfV10,
    const float3 hfV01,
    const float3 hfV11,
    const float3 v0,
    const float3 v1,
    const float3 v2,
    const float3 triN,
    const float3 triNLocal,
    const bool cullBackfaces,
    const bool handleDeepTriangle,
    const bool recoverSingleSided,
    const bool tryPlanarPatch,
    const float maxDist,
    const uint featShapeTag,
    const uint featSubTag,
    const uint featSurfTag,
    const uint triOrCellIdx,
    inout float bestSep,
    inout float3 chosenWorldN,
    inout uint bestTriOrCell,
    inout float3 candWA[NWB_PHYSICS_MAX_MANIFOLD_POINTS],
    inout float3 candWB[NWB_PHYSICS_MAX_MANIFOLD_POINTS],
    inout float3 candNorm[NWB_PHYSICS_MAX_MANIFOLD_POINTS],
    inout float candSep[NWB_PHYSICS_MAX_MANIFOLD_POINTS],
    inout uint candFeat[NWB_PHYSICS_MAX_MANIFOLD_POINTS],
    inout uint candCount
){
    const float totalR = otherR + surfR;
    const float3 zeroVec = float3(0.0f, 0.0f, 0.0f);
    float3 gjkPtO = zeroVec;
    float3 gjkPtT = zeroVec;
    float gjkDist = 0.0f;
    bool gjkDeep = false;
    nwbPhysicsRunGjkDistanceCached(otherCache, false, zeroVec, zeroVec, zeroVec, otherCache, true, v0, v1, v2, hullVertexSlot, gjkPtO, gjkPtT, gjkDist, gjkDeep);

    const bool separated = (!gjkDeep && gjkDist > 1.0e-5f);
    if(separated && gjkDist - totalR > maxDist)
        return 0u;
    if(!separated && !handleDeepTriangle)
        return 2u;

    const uint edgeFlags = useHfEdgeFlags
        ? nwbPhysicsHfComputeTriangleEdgeFlags(hullVertexSlot, hfBase, cols, rows, hfLocalOrigin, sx, sz, sy, hfFlags, cx, cz, hfTriIdx, hfV00, hfV10, hfV01, hfV11, triN)
        : meshEdgeFlags;

    float sep = 0.0f;
    float3 nOtherToSurfL = -triNLocal;
    float3 useO = gjkPtO;
    float3 useT = gjkPtT;
    bool smoothed = false;
    bool forceFallback = false;
    if(separated){
        sep = gjkDist - totalR;
        nOtherToSurfL = nwbPhysicsSafeNormal(gjkPtT - gjkPtO, -triNLocal);
    }
    else{
        float3 epaN = -triNLocal;
        float3 epaPtO = zeroVec;
        float3 epaPtT = zeroVec;
        float epaDepth = 0.0f;
        const bool hasEpa = nwbPhysicsRunEpaCached(otherCache, false, zeroVec, zeroVec, zeroVec, otherCache, true, v0, v1, v2, hullVertexSlot, -triNLocal, epaN, epaPtO, epaPtT, epaDepth);
        if(hasEpa){
            sep = -epaDepth - totalR;
            nOtherToSurfL = nwbPhysicsSafeNormal(epaN, -triNLocal);
            useO = epaPtO;
            useT = epaPtT;
        }
        else{
            if(!recoverSingleSided)
                return 1u;
            forceFallback = true;
        }
    }

    if(!forceFallback){
        float3 smoothedL = -nOtherToSurfL;
        smoothed = nwbPhysicsSmoothMeshNormal(triN, edgeFlags, useT, v0, v1, v2, -nOtherToSurfL, smoothedL);
        if(smoothed){
            if(!separated){
                uint supDummy = 0u;
                useO = nwbPhysicsSupportCached(otherCache, hullVertexSlot, -smoothedL, supDummy);
            }
            sep = dot(smoothedL, useO - v0) - totalR;
            nOtherToSurfL = -smoothedL;
        }
        if(!separated && recoverSingleSided && dot(nOtherToSurfL, triN) >= 0.0f)
            forceFallback = true;
    }

    if(forceFallback){
        uint supDummy = 0u;
        useO = nwbPhysicsSupportCached(otherCache, hullVertexSlot, -triN, supDummy);
        sep = dot(triN, useO - v0) - totalR;
        nOtherToSurfL = -triN;
        smoothed = true;
    }

    if(sep > maxDist)
        return 0u;
    if(cullBackfaces && dot(nOtherToSurfL, triN) >= 0.0f)
        return 0u;

    const float3 nOtherToSurf = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(nOtherToSurfL, surfRot), float3(0.0f, -1.0f, 0.0f));
    const float3 wNOut = surfIsA ? -nOtherToSurf : nOtherToSurf;
    bool pushedPatch = false;
    if(tryPlanarPatch){
        pushedPatch = nwbPhysicsTryPushTrianglePlanarPatch(
            otherP,
            relOrig,
            relRot,
            surfOrig,
            surfRot,
            surfIsA,
            otherR,
            surfR,
            hullVertexSlot,
            v0,
            v1,
            v2,
            triN,
            nOtherToSurfL,
            nOtherToSurf,
            wNOut,
            triOrCellIdx,
            triOrCellIdx,
            maxDist,
            bestSep,
            chosenWorldN,
            bestTriOrCell,
            candWA,
            candWB,
            candNorm,
            candSep,
            candFeat,
            candCount
        );
    }
    if(!pushedPatch){
        const float3 wO = surfOrig + nwbPhysicsQuatRotate(useO, surfRot) + nOtherToSurf * otherR;
        const float3 wM = smoothed
            ? (wO + nOtherToSurf * sep)
            : (surfOrig + nwbPhysicsQuatRotate(useT, surfRot) - nOtherToSurf * surfR);
        const float3 wAOut = surfIsA ? wM : wO;
        const float3 wBOut = surfIsA ? wO : wM;
        if(sep < bestSep){
            bestSep = sep;
            chosenWorldN = wNOut;
            bestTriOrCell = triOrCellIdx;
        }
        nwbPhysicsPushMeshCand(wAOut, wBOut, wNOut, sep, nwbPhysicsMakeFeatureId(featShapeTag, featSubTag, featSurfTag, triOrCellIdx & 0xffu), candWA, candWB, candNorm, candSep, candFeat, candCount);
    }
    return 0u;
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
    float stackDistSq[64];
    uint sp = 0u;
    stack[0] = 0u;
    stackDistSq[0] = 0.0f;
    sp = 1u;
    while(sp > 0u){
        --sp;
        if(stackDistSq[sp] >= bestDistSq)
            continue;
        const uint nodeRel = stack[sp];
        const float4 n0 = g_NwbPhysicsFloat4Buffers[meshNodeSlot][(nodeBase + nodeRel) * 2u + 0u];
        const float4 n1 = g_NwbPhysicsFloat4Buffers[meshNodeSlot][(nodeBase + nodeRel) * 2u + 1u];

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
                    stack[sp] = rightRel;
                    stackDistSq[sp] = dRightSq;
                    ++sp;
                }
                if(dLeftSq < bestDistSq){
                    if(sp >= 64u){
                        outOverflow = true;
                        return false;
                    }
                    stack[sp] = leftRel;
                    stackDistSq[sp] = dLeftSq;
                    ++sp;
                }
            }
            else{
                if(dLeftSq < bestDistSq){
                    if(sp >= 64u){
                        outOverflow = true;
                        return false;
                    }
                    stack[sp] = leftRel;
                    stackDistSq[sp] = dLeftSq;
                    ++sp;
                }
                if(dRightSq < bestDistSq){
                    if(sp >= 64u){
                        outOverflow = true;
                        return false;
                    }
                    stack[sp] = rightRel;
                    stackDistSq[sp] = dRightSq;
                    ++sp;
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
    const uint triBaseA = asuint(proxyA.localRotationOrMeta.x);
    const uint nodeBaseB = proxyB.filterData.w;
    const uint triBaseB = asuint(proxyB.localRotationOrMeta.x);
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
    float3 sBAxX;
    float3 sBAxY;
    float3 sBAxZ;
    nwbPhysicsQuatToAxes(rotBToA, sBAxX, sBAxY, sBAxZ);
    const float3 bAxX = abs(sBAxX);
    const float3 bAxY = abs(sBAxY);
    const float3 bAxZ = abs(sBAxZ);

    NwbPhysicsGpuSupportCache dummyCache;
    dummyCache.shapeType = 0u;
    dummyCache.vCount = 0u;
    dummyCache.vOffset = 0u;
    dummyCache.isBox = false;
    dummyCache.p0OrCenter = float3(0.0f, 0.0f, 0.0f);
    dummyCache.p1OrExtent = float3(0.0f, 0.0f, 0.0f);
    dummyCache.axX = float3(1.0f, 0.0f, 0.0f);
    dummyCache.axY = float3(0.0f, 1.0f, 0.0f);
    dummyCache.axZ = float3(0.0f, 0.0f, 1.0f);
    dummyCache.extraParams = float2(0.0f, 0.0f);

    float3 candWA[NWB_PHYSICS_MAX_MANIFOLD_POINTS];
    float3 candWB[NWB_PHYSICS_MAX_MANIFOLD_POINTS];
    float3 candNorm[NWB_PHYSICS_MAX_MANIFOLD_POINTS];
    float candSep[NWB_PHYSICS_MAX_MANIFOLD_POINTS];
    uint candFeat[NWB_PHYSICS_MAX_MANIFOLD_POINTS];
    [loop] for(uint kInit = 0u; kInit < NWB_PHYSICS_MAX_MANIFOLD_POINTS; ++kInit){
        candWA[kInit] = float3(0.0f, 0.0f, 0.0f);
        candWB[kInit] = float3(0.0f, 0.0f, 0.0f);
        candNorm[kInit] = float3(0.0f, 1.0f, 0.0f);
        candSep[kInit] = 0.0f;
        candFeat[kInit] = 0u;
    }
    uint candCount = 0u;
    bool overflow = false;
    float bestSep = 1.0e20f;
    float3 chosenWorldN = float3(0.0f, -1.0f, 0.0f);
    uint bestLocalTriA = NWB_PHYSICS_INVALID_INDEX;
    uint bestLocalTriB = NWB_PHYSICS_INVALID_INDEX;

    uint2 stack[64];
    uint sp = 0u;
    stack[sp++] = uint2(0u, 0u);
    while(sp > 0u && !overflow){
        const uint2 pairNode = stack[--sp];
        const float4 nA0 = g_NwbPhysicsFloat4Buffers[meshNodeSlot][(nodeBaseA + pairNode.x) * 2u + 0u];
        const float4 nA1 = g_NwbPhysicsFloat4Buffers[meshNodeSlot][(nodeBaseA + pairNode.x) * 2u + 1u];
        const float4 nB0 = g_NwbPhysicsFloat4Buffers[meshNodeSlot][(nodeBaseB + pairNode.y) * 2u + 0u];
        const float4 nB1 = g_NwbPhysicsFloat4Buffers[meshNodeSlot][(nodeBaseB + pairNode.y) * 2u + 1u];

        const float3 bMid = (nB0.xyz + nB1.xyz) * 0.5f;
        const float3 bHalf = (nB1.xyz - nB0.xyz) * 0.5f;
        const float3 bCenterA = posBInA + sBAxX * bMid.x + sBAxY * bMid.y + sBAxZ * bMid.z;
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
                const float3 tMinA = min(av0, min(av1, av2));
                const float3 tMaxA = max(av0, max(av1, av2));
                if(!nwbPhysicsAabbOverlaps(tMinA, tMaxA, nodeBMinA, nodeBMaxA))
                    continue;

                const uint meshFlagsA = asuint(aT1.w);
                const bool closedA = (meshFlagsA & NWB_PHYSICS_MESH_FLAG_CLOSED) != 0u;
                const bool cullBackA = closedA || ((meshFlagsA & NWB_PHYSICS_MESH_FLAG_CULL_BACKFACES) != 0u);
                const uint edgeFlagsA = asuint(aT0.w);
                const float3 rawTriNA = nwbPhysicsSafeNormal(cross(av1 - av0, av2 - av0), float3(0.0f, 1.0f, 0.0f));
                const float3 edgesA[3] = { av1 - av0, av2 - av1, av0 - av2 };
                const float3 vertsA[3] = { av0, av1, av2 };

                for(uint tiB = 0u; tiB < triCountB; ++tiB){
                    const uint localTriB = firstTriB + tiB;
                    const uint triIdxB = triBaseB + localTriB;
                    const float4 bT0 = g_NwbPhysicsFloat4Buffers[meshTriSlot][triIdxB * 3u + 0u];
                    const float4 bT1 = g_NwbPhysicsFloat4Buffers[meshTriSlot][triIdxB * 3u + 1u];
                    const float4 bT2 = g_NwbPhysicsFloat4Buffers[meshTriSlot][triIdxB * 3u + 2u];
                    const float3 bv0 = bT0.xyz;
                    const float3 bv1 = bT1.xyz;
                    const float3 bv2 = bT2.xyz;

                    const float3 b0InA = posBInA + sBAxX * bv0.x + sBAxY * bv0.y + sBAxZ * bv0.z;
                    const float3 b1InA = posBInA + sBAxX * bv1.x + sBAxY * bv1.y + sBAxZ * bv1.z;
                    const float3 b2InA = posBInA + sBAxX * bv2.x + sBAxY * bv2.y + sBAxZ * bv2.z;
                    const float3 tMinBInA = min(b0InA, min(b1InA, b2InA)) - marginVec;
                    const float3 tMaxBInA = max(b0InA, max(b1InA, b2InA)) + marginVec;
                    if(!nwbPhysicsAabbOverlaps(tMinA, tMaxA, tMinBInA, tMaxBInA))
                        continue;

                    const uint meshFlagsB = asuint(bT1.w);
                    const bool closedB = (meshFlagsB & NWB_PHYSICS_MESH_FLAG_CLOSED) != 0u;
                    const bool cullBackB = closedB || ((meshFlagsB & NWB_PHYSICS_MESH_FLAG_CULL_BACKFACES) != 0u);
                    const uint edgeFlagsB = asuint(bT0.w);
                    const float3 rawTriNBInA = nwbPhysicsSafeNormal(cross(b1InA - b0InA, b2InA - b0InA), float3(0.0f, 1.0f, 0.0f));

                    float3 gjkPtA = float3(0.0f, 0.0f, 0.0f);
                    float3 gjkPtB = float3(0.0f, 0.0f, 0.0f);
                    float gjkDist = 0.0f;
                    bool gjkDeep = false;
                    nwbPhysicsRunGjkDistanceCached(dummyCache, true, av0, av1, av2, dummyCache, true, b0InA, b1InA, b2InA, hullVertexSlot, gjkPtA, gjkPtB, gjkDist, gjkDeep);

                    if(!gjkDeep && gjkDist > 1.0e-5f){
                        const float sep = gjkDist - totalR;
                        if(sep > maxDist)
                            continue;
                        const float3 nAtoBInA = nwbPhysicsSafeNormal(gjkPtB - gjkPtA, rawTriNA);
                        if(cullBackA && dot(nAtoBInA, rawTriNA) <= 0.0f)
                            continue;
                        if(cullBackB && dot(nAtoBInA, rawTriNBInA) >= 0.0f)
                            continue;
                        const float3 nAtoB = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(nAtoBInA, rotA), float3(0.0f, -1.0f, 0.0f));
                        const float3 wAOut = originA + nwbPhysicsQuatRotate(gjkPtA, rotA) + nAtoB * rA;
                        const float3 wBOut = originA + nwbPhysicsQuatRotate(gjkPtB, rotA) - nAtoB * rB;
                        if(sep < bestSep){
                            bestSep = sep;
                            chosenWorldN = nAtoB;
                        }
                        nwbPhysicsPushMeshCand(wAOut, wBOut, nAtoB, sep, nwbPhysicsMakeFeatureId(3u, localTriA & 0xffu, 3u, localTriB & 0xffu), candWA, candWB, candNorm, candSep, candFeat, candCount);
                    }
                    else{
                        float3 triNA = rawTriNA;
                        const float dA = dot(rawTriNA, av0);
                        const float b0A = dot(rawTriNA, b0InA);
                        const float b1A = dot(rawTriNA, b1InA);
                        const float b2A = dot(rawTriNA, b2InA);
                        const float minBOnA = min(b0A, min(b1A, b2A));
                        float sepA = minBOnA - dA - totalR;
                        if(!cullBackA){
                            const float maxBOnA = max(b0A, max(b1A, b2A));
                            const float sepANeg = dA - maxBOnA - totalR;
                            if(sepANeg > sepA){
                                sepA = sepANeg;
                                triNA = -rawTriNA;
                            }
                        }
                        if(sepA > maxDist)
                            continue;

                        float3 nBtoA = -rawTriNBInA;
                        const float dB = dot(nBtoA, b0InA);
                        const float a0B = dot(nBtoA, av0);
                        const float a1B = dot(nBtoA, av1);
                        const float a2B = dot(nBtoA, av2);
                        const float maxAOnB = max(a0B, max(a1B, a2B));
                        float sepB = dB - maxAOnB - totalR;
                        if(!cullBackB){
                            const float minAOnB = min(a0B, min(a1B, a2B));
                            const float sepBNeg = minAOnB - dB - totalR;
                            if(sepBNeg > sepB){
                                sepB = sepBNeg;
                                nBtoA = rawTriNBInA;
                            }
                        }
                        if(sepB > maxDist)
                            continue;

                        const float3 edgesB[3] = { b1InA - b0InA, b2InA - b1InA, b0InA - b2InA };
                        const float3 vertsB[3] = { b0InA, b1InA, b2InA };

                        float bestEdgeSep = -1.0e20f;
                        float3 bestEdgeAxis = triNA;
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
                                const float a0 = dot(axis, av0);
                                const float a1 = dot(axis, av1);
                                const float a2 = dot(axis, av2);
                                const float minA = min(a0, min(a1, a2));
                                const float maxA = max(a0, max(a1, a2));
                                const float b0 = dot(axis, b0InA);
                                const float b1 = dot(axis, b1InA);
                                const float b2 = dot(axis, b2InA);
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
                        float3 winNInA = triNA;
                        if(sepB > winSep + 1.0e-4f){
                            winKind = 1u;
                            winSep = sepB;
                            winNInA = nBtoA;
                        }
                        if(bestEdgeSep > winSep + 5.0e-4f){
                            const bool internalA = (edgeFlagsA & (1u << bestEA)) != 0u;
                            const bool internalB = (edgeFlagsB & (1u << bestEB)) != 0u;
                            if(internalA && sepA >= bestEdgeSep - 0.05f){
                                winKind = 0u;
                                winSep = sepA;
                                winNInA = triNA;
                            }
                            else if(internalB && sepB >= bestEdgeSep - 0.05f){
                                winKind = 1u;
                                winSep = sepB;
                                winNInA = nBtoA;
                            }
                            else{
                                winKind = 2u;
                                winSep = bestEdgeSep;
                                winNInA = bestEdgeAxis;
                            }
                        }

                        if(cullBackA && dot(winNInA, rawTriNA) <= 0.0f)
                            continue;
                        if(cullBackB && dot(winNInA, rawTriNBInA) >= 0.0f)
                            continue;

                        const float3 winN = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(winNInA, rotA), float3(0.0f, -1.0f, 0.0f));
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
                            const float3 wAOut = originA + nwbPhysicsQuatRotate(cA, rotA) + winN * rA;
                            const float3 wBOut = originA + nwbPhysicsQuatRotate(cB, rotA) - winN * rB;
                            if(winSep < bestSep){
                                bestSep = winSep;
                                chosenWorldN = winN;
                                bestLocalTriA = localTriA;
                                bestLocalTriB = localTriB;
                            }
                            nwbPhysicsPushMeshCand(wAOut, wBOut, winN, winSep, nwbPhysicsMakeFeatureId(3u, (localTriA & 0xffu) ^ bestEA, 3u, (localTriB & 0xffu) ^ bestEB), candWA, candWB, candNorm, candSep, candFeat, candCount);
                        }
                        else if(winKind == 0u){
                            float4 poly0[NWB_PHYSICS_GPU_MAX_CLIP_POINTS];
                            float4 poly1[NWB_PHYSICS_GPU_MAX_CLIP_POINTS];
                            poly0[0] = float4(b0InA, 0.0f);
                            poly0[1] = float4(b1InA, 0.0f);
                            poly0[2] = float4(b2InA, 0.0f);
                            const float3 sN0 = nwbPhysicsSafeNormal(cross(edgesA[0], rawTriNA), float3(1.0f, 0.0f, 0.0f));
                            uint polyCount = nwbPhysicsClipPolygonPlane(poly0, 3u, sN0, dot(sN0, vertsA[0]) + 5.0e-3f, 0u, poly1, overflow);
                            if(!overflow && polyCount > 0u){
                                const float3 sN1 = nwbPhysicsSafeNormal(cross(edgesA[1], rawTriNA), float3(1.0f, 0.0f, 0.0f));
                                polyCount = nwbPhysicsClipPolygonPlane(poly1, polyCount, sN1, dot(sN1, vertsA[1]) + 5.0e-3f, 1u, poly0, overflow);
                            }
                            if(!overflow && polyCount > 0u){
                                const float3 sN2 = nwbPhysicsSafeNormal(cross(edgesA[2], rawTriNA), float3(1.0f, 0.0f, 0.0f));
                                polyCount = nwbPhysicsClipPolygonPlane(poly0, polyCount, sN2, dot(sN2, vertsA[2]) + 5.0e-3f, 2u, poly1, overflow);
                            }
                            if(!overflow){
                                for(uint k = 0u; k < polyCount && !overflow; ++k){
                                    const float3 ptB = poly1[k].xyz;
                                    const float sep = dot(winNInA, ptB - av0) - totalR;
                                    if(sep > maxDist)
                                        continue;
                                    const float3 wPtB = originA + nwbPhysicsQuatRotate(ptB, rotA);
                                    const float3 wAOut = wPtB - winN * (sep + rB);
                                    const float3 wBOut = wPtB - winN * rB;
                                    if(sep < bestSep){
                                        bestSep = sep;
                                        chosenWorldN = winN;
                                        bestLocalTriA = localTriA;
                                        bestLocalTriB = localTriB;
                                    }
                                    nwbPhysicsPushMeshCand(wAOut, wBOut, winN, sep, nwbPhysicsMakeFeatureId(3u, localTriA & 0xffu, 3u, (localTriB & 0xffu) ^ k), candWA, candWB, candNorm, candSep, candFeat, candCount);
                                }
                            }
                        }
                        else{
                            float4 poly0[NWB_PHYSICS_GPU_MAX_CLIP_POINTS];
                            float4 poly1[NWB_PHYSICS_GPU_MAX_CLIP_POINTS];
                            poly0[0] = float4(av0, 0.0f);
                            poly0[1] = float4(av1, 0.0f);
                            poly0[2] = float4(av2, 0.0f);
                            const float3 sN0 = nwbPhysicsSafeNormal(cross(edgesB[0], rawTriNBInA), float3(1.0f, 0.0f, 0.0f));
                            uint polyCount = nwbPhysicsClipPolygonPlane(poly0, 3u, sN0, dot(sN0, vertsB[0]) + 5.0e-3f, 0u, poly1, overflow);
                            if(!overflow && polyCount > 0u){
                                const float3 sN1 = nwbPhysicsSafeNormal(cross(edgesB[1], rawTriNBInA), float3(1.0f, 0.0f, 0.0f));
                                polyCount = nwbPhysicsClipPolygonPlane(poly1, polyCount, sN1, dot(sN1, vertsB[1]) + 5.0e-3f, 1u, poly0, overflow);
                            }
                            if(!overflow && polyCount > 0u){
                                const float3 sN2 = nwbPhysicsSafeNormal(cross(edgesB[2], rawTriNBInA), float3(1.0f, 0.0f, 0.0f));
                                polyCount = nwbPhysicsClipPolygonPlane(poly0, polyCount, sN2, dot(sN2, vertsB[2]) + 5.0e-3f, 2u, poly1, overflow);
                            }
                            if(!overflow){
                                for(uint k = 0u; k < polyCount && !overflow; ++k){
                                    const float3 ptA = poly1[k].xyz;
                                    const float sep = dot(winNInA, b0InA - ptA) - totalR;
                                    if(sep > maxDist)
                                        continue;
                                    const float3 wPtA = originA + nwbPhysicsQuatRotate(ptA, rotA);
                                    const float3 wAOut = wPtA + winN * rA;
                                    const float3 wBOut = wPtA + winN * (sep + rA);
                                    if(sep < bestSep){
                                        bestSep = sep;
                                        chosenWorldN = winN;
                                        bestLocalTriA = localTriA;
                                        bestLocalTriB = localTriB;
                                    }
                                    nwbPhysicsPushMeshCand(wAOut, wBOut, winN, sep, nwbPhysicsMakeFeatureId(3u, (localTriA & 0xffu) ^ k, 3u, localTriB & 0xffu), candWA, candWB, candNorm, candSep, candFeat, candCount);
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    if(!overflow && candCount == 0u){
        const bool closedB = (asuint(g_NwbPhysicsFloat4Buffers[meshTriSlot][triBaseB * 3u + 1u].w) & NWB_PHYSICS_MESH_FLAG_CLOSED) != 0u;
        if(closedB){
            const float4 invRotB = float4(-rotB.xyz, rotB.w);
            const float3 posAInB = nwbPhysicsQuatRotate(originA - originB, invRotB);
            const float4 rotAToB = nwbPhysicsQuatNormalize(nwbPhysicsQuatMul(invRotB, rotA));
            const float3 rootAN0 = g_NwbPhysicsFloat4Buffers[meshNodeSlot][nodeBaseA * 2u + 0u].xyz;
            const float3 rootAN1 = g_NwbPhysicsFloat4Buffers[meshNodeSlot][nodeBaseA * 2u + 1u].xyz;
            const float3 cAInB = posAInB + nwbPhysicsQuatRotate((rootAN0 + rootAN1) * 0.5f, rotAToB);
            const float3 vAInB = posAInB + nwbPhysicsQuatRotate(g_NwbPhysicsFloat4Buffers[meshTriSlot][triBaseA * 3u + 0u].xyz, rotAToB);
            float3 probeB = float3(0.0f, 0.0f, 0.0f);
            bool insideB = nwbPhysicsIsPointInsideClosedMesh(nodeBaseB, triBaseB, meshNodeSlot, meshTriSlot, cAInB, overflow);
            if(!overflow && insideB){
                probeB = cAInB;
            }
            else if(!overflow){
                insideB = nwbPhysicsIsPointInsideClosedMesh(nodeBaseB, triBaseB, meshNodeSlot, meshTriSlot, vAInB, overflow);
                if(!overflow && insideB)
                    probeB = vAInB;
            }
            if(!overflow && insideB){
                float3 closePt = float3(0.0f, 0.0f, 0.0f);
                float3 closeN = float3(0.0f, 1.0f, 0.0f);
                uint closeTri = 0u;
                float dist = 0.0f;
                const bool foundClosest = nwbPhysicsFindClosestPointOnClosedMesh(nodeBaseB, triBaseB, meshNodeSlot, meshTriSlot, probeB, closePt, closeN, closeTri, dist, overflow);
                if(!overflow && foundClosest){
                    const float3 worldOut = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(closeN, rotB), float3(0.0f, 1.0f, 0.0f));
                    const float3 nAtoB = -worldOut;
                    const float3 wProbe = originB + nwbPhysicsQuatRotate(probeB, rotB);
                    const float3 wSurf = originB + nwbPhysicsQuatRotate(closePt, rotB);
                    const float sep = -(dist + totalR);
                    const float3 wAOut = wProbe + nAtoB * rA;
                    const float3 wBOut = wSurf - nAtoB * rB;
                    if(sep <= maxDist){
                        chosenWorldN = nAtoB;
                        bestLocalTriA = NWB_PHYSICS_INVALID_INDEX;
                        bestLocalTriB = closeTri;
                        nwbPhysicsPushMeshCand(wAOut, wBOut, nAtoB, sep, nwbPhysicsMakeFeatureId(3u, 0u, 3u, closeTri & 0xffu), candWA, candWB, candNorm, candSep, candFeat, candCount);
                    }
                }
            }
        }
        const bool closedA = (asuint(g_NwbPhysicsFloat4Buffers[meshTriSlot][triBaseA * 3u + 1u].w) & NWB_PHYSICS_MESH_FLAG_CLOSED) != 0u;
        if(!overflow && candCount == 0u && closedA){
            const float3 rootBN0 = g_NwbPhysicsFloat4Buffers[meshNodeSlot][nodeBaseB * 2u + 0u].xyz;
            const float3 rootBN1 = g_NwbPhysicsFloat4Buffers[meshNodeSlot][nodeBaseB * 2u + 1u].xyz;
            const float3 cBInA = posBInA + nwbPhysicsQuatRotate((rootBN0 + rootBN1) * 0.5f, rotBToA);
            const float3 vBInA = posBInA + nwbPhysicsQuatRotate(g_NwbPhysicsFloat4Buffers[meshTriSlot][triBaseB * 3u + 0u].xyz, rotBToA);
            float3 probeA = float3(0.0f, 0.0f, 0.0f);
            bool insideA = nwbPhysicsIsPointInsideClosedMesh(nodeBaseA, triBaseA, meshNodeSlot, meshTriSlot, cBInA, overflow);
            if(!overflow && insideA){
                probeA = cBInA;
            }
            else if(!overflow){
                insideA = nwbPhysicsIsPointInsideClosedMesh(nodeBaseA, triBaseA, meshNodeSlot, meshTriSlot, vBInA, overflow);
                if(!overflow && insideA)
                    probeA = vBInA;
            }
            if(!overflow && insideA){
                float3 closePt = float3(0.0f, 0.0f, 0.0f);
                float3 closeN = float3(0.0f, 1.0f, 0.0f);
                uint closeTri = 0u;
                float dist = 0.0f;
                const bool foundClosest = nwbPhysicsFindClosestPointOnClosedMesh(nodeBaseA, triBaseA, meshNodeSlot, meshTriSlot, probeA, closePt, closeN, closeTri, dist, overflow);
                if(!overflow && foundClosest){
                    const float3 nAtoB = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(closeN, rotA), float3(0.0f, 1.0f, 0.0f));
                    const float3 wProbe = originA + nwbPhysicsQuatRotate(probeA, rotA);
                    const float3 wSurf = originA + nwbPhysicsQuatRotate(closePt, rotA);
                    const float sep = -(dist + totalR);
                    const float3 wAOut = wSurf + nAtoB * rA;
                    const float3 wBOut = wProbe - nAtoB * rB;
                    if(sep <= maxDist){
                        chosenWorldN = nAtoB;
                        bestLocalTriA = closeTri;
                        bestLocalTriB = NWB_PHYSICS_INVALID_INDEX;
                        nwbPhysicsPushMeshCand(wAOut, wBOut, nAtoB, sep, nwbPhysicsMakeFeatureId(3u, closeTri & 0xffu, 3u, 0u), candWA, candWB, candNorm, candSep, candFeat, candCount);
                    }
                }
            }
        }
    }

    if(overflow){
        result.pairInfo.w = 1u;
        return;
    }
    if(candCount > 0u){
        if((bestLocalTriA != NWB_PHYSICS_INVALID_INDEX && bestLocalTriA >= 0xffffu) || (bestLocalTriB != NWB_PHYSICS_INVALID_INDEX && bestLocalTriB >= 0xffffu)){
            result.pairInfo.w = 1u;
            return;
        }
        nwbPhysicsFinalizeMeshCand(candWA, candWB, candNorm, candSep, candFeat, candCount, chosenWorldN, bodyPosA, bodyPosB, result);
        const uint packTriA = (bestLocalTriA != NWB_PHYSICS_INVALID_INDEX) ? (bestLocalTriA & 0xffffu) : 0xffffu;
        const uint packTriB = (bestLocalTriB != NWB_PHYSICS_INVALID_INDEX) ? (bestLocalTriB & 0xffffu) : 0xffffu;
        result.normalPad.w = asfloat(packTriA | (packTriB << 16u));
    }
}

bool nwbPhysicsFetchMeshLeafTriangleInAabb(
    const uint meshTriSlot,
    const uint triIdx,
    const float3 queryMin,
    const float3 queryMax,
    inout bool anyClosedMesh,
    out float3 outV0,
    out float3 outV1,
    out float3 outV2,
    out float3 outTMin,
    out float3 outTMax,
    out uint outEdgeFlags,
    out bool outIsClosed,
    out bool outCullBackfaces,
    out float3 outTriN
){
    const float4 mt0 = g_NwbPhysicsFloat4Buffers[meshTriSlot][triIdx * 3u + 0u];
    const float4 mt1 = g_NwbPhysicsFloat4Buffers[meshTriSlot][triIdx * 3u + 1u];
    const float4 mt2 = g_NwbPhysicsFloat4Buffers[meshTriSlot][triIdx * 3u + 2u];
    outV0 = mt0.xyz;
    outV1 = mt1.xyz;
    outV2 = mt2.xyz;
    outTMin = min(outV0, min(outV1, outV2));
    outTMax = max(outV0, max(outV1, outV2));
    if(!nwbPhysicsAabbOverlaps(outTMin, outTMax, queryMin, queryMax)){
        outEdgeFlags = 0u;
        outIsClosed = false;
        outCullBackfaces = false;
        outTriN = float3(0.0f, 1.0f, 0.0f);
        return false;
    }

    outEdgeFlags = asuint(mt0.w);
    const uint meshFlags = asuint(mt1.w);
    outIsClosed = (meshFlags & NWB_PHYSICS_MESH_FLAG_CLOSED) != 0u;
    outCullBackfaces = outIsClosed || ((meshFlags & NWB_PHYSICS_MESH_FLAG_CULL_BACKFACES) != 0u);
    if(outIsClosed)
        anyClosedMesh = true;
    outTriN = nwbPhysicsSafeNormal(cross(outV1 - outV0, outV2 - outV0), float3(0.0f, 1.0f, 0.0f));
    return true;
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

    const NwbPhysicsGpuSupportCache otherCache = nwbPhysicsBuildSupportCache(otherP, relOrig, relRot);
    float3 sphereLc = float3(0.0f, 0.0f, 0.0f);
    float3 wSphereCenter = float3(0.0f, 0.0f, 0.0f);
    float3 capL0 = float3(0.0f, 0.0f, 0.0f);
    float3 capL1 = float3(0.0f, 0.0f, 0.0f);
    float3 capMidL = float3(0.0f, 0.0f, 0.0f);
    float3 queryMin = float3(0.0f, 0.0f, 0.0f);
    float3 queryMax = float3(0.0f, 0.0f, 0.0f);
    if(otherType == NWB_PHYSICS_SHAPE_TYPE_SPHERE){
        sphereLc = otherCache.p0OrCenter;
        wSphereCenter = meshOrig + nwbPhysicsQuatRotate(sphereLc, meshRot);
        queryMin = sphereLc - float3(queryR, queryR, queryR);
        queryMax = sphereLc + float3(queryR, queryR, queryR);
    }
    else{
        capL0 = otherCache.p0OrCenter;
        capL1 = otherCache.p1OrExtent;
        capMidL = (capL0 + capL1) * 0.5f;
        queryMin = min(capL0, capL1) - float3(queryR, queryR, queryR);
        queryMax = max(capL0, capL1) + float3(queryR, queryR, queryR);
    }

    float bestSep = 1.0e20f;
    float3 chosenWorldN = float3(0.0f, -1.0f, 0.0f);
    float3 bestWorldOther = float3(0.0f, 0.0f, 0.0f);
    float3 bestWorldMesh = float3(0.0f, 0.0f, 0.0f);
    float3 bestTriV0 = float3(0.0f, 0.0f, 0.0f);
    float3 bestTriV1 = float3(0.0f, 0.0f, 0.0f);
    float3 bestTriV2 = float3(0.0f, 0.0f, 0.0f);
    float3 bestTriRawN = float3(0.0f, 1.0f, 0.0f);
    float3 bestLocalOtherToMesh = float3(0.0f, -1.0f, 0.0f);
    uint bestLocalTriIdx = NWB_PHYSICS_INVALID_INDEX;
    bool foundSingle = false;
    bool anyClosedMesh = false;

    const float queryRSq = queryR * queryR;
    const float probeRBound = max(queryR - 1.0e-4f, 0.0f);
    const float probeRBoundSq = probeRBound * probeRBound;
    bool hasProbeClosest[3] = { false, false, false };
    bool hasProbeFrontTie[3] = { false, false, false };
    bool hasProbeBackTie[3] = { false, false, false };
    float probeClosestDistSq[3] = { probeRBoundSq, probeRBoundSq, probeRBoundSq };
    float3 probeClosestPt[3] = { float3(0.0f, 0.0f, 0.0f), float3(0.0f, 0.0f, 0.0f), float3(0.0f, 0.0f, 0.0f) };
    float3 probeClosestN[3] = { float3(0.0f, 1.0f, 0.0f), float3(0.0f, 1.0f, 0.0f), float3(0.0f, 1.0f, 0.0f) };
    uint probeClosestTri[3] = { 0u, 0u, 0u };
    const float3 capProbes[3] = { capMidL, capL0, capL1 };

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
            [loop] for(uint ti = 0u; ti < triCount; ++ti){
                const uint localTriIdx = firstTri + ti;
                const uint triIdx = triBase + localTriIdx;
                float3 v0;
                float3 v1;
                float3 v2;
                float3 tMin;
                float3 tMax;
                uint edgeFlags;
                bool isClosed;
                bool cullBackfaces;
                float3 triN;
                if(!nwbPhysicsFetchMeshLeafTriangleInAabb(meshTriSlot, triIdx, queryMin, queryMax, anyClosedMesh, v0, v1, v2, tMin, tMax, edgeFlags, isClosed, cullBackfaces, triN))
                    continue;

                if(otherType == NWB_PHYSICS_SHAPE_TYPE_SPHERE){
                    const float3 lc = sphereLc;
                    const float side = dot(triN, lc - v0);
                    float3 qL = float3(0.0f, 0.0f, 0.0f);
                    float dSq = 1.0e30f;
                    bool hasClosestPt = false;
                    if(isClosed){
                        qL = nwbPhysicsClosestPointOnTriangle(lc, v0, v1, v2);
                        const float3 dLProbe = qL - lc;
                        dSq = dot(dLProbe, dLProbe);
                        hasClosestPt = true;
                        if(dSq <= probeRBoundSq){
                            const bool front = side >= -1.0e-5f;
                            const bool back = side <= 1.0e-5f;
                            if(!hasProbeClosest[0] || dSq < probeClosestDistSq[0] - 1.0e-5f){
                                hasProbeClosest[0] = true;
                                probeClosestDistSq[0] = dSq;
                                probeClosestPt[0] = qL;
                                probeClosestN[0] = triN;
                                probeClosestTri[0] = localTriIdx;
                                hasProbeFrontTie[0] = front;
                                hasProbeBackTie[0] = back;
                            }
                            else if(dSq <= probeClosestDistSq[0] + 1.0e-5f){
                                if(front)
                                    hasProbeFrontTie[0] = true;
                                if(back)
                                    hasProbeBackTie[0] = true;
                                if(dSq < probeClosestDistSq[0]){
                                    probeClosestDistSq[0] = dSq;
                                    probeClosestPt[0] = qL;
                                    probeClosestN[0] = triN;
                                    probeClosestTri[0] = localTriIdx;
                                }
                            }
                        }
                    }
                    if(cullBackfaces && side < -NWB_PHYSICS_LINEAR_SLOP)
                        continue;
                    if(!hasClosestPt){
                        qL = nwbPhysicsClosestPointOnTriangle(lc, v0, v1, v2);
                        const float3 dL = qL - lc;
                        dSq = dot(dL, dL);
                    }
                    if(dSq > queryRSq)
                        continue;
                    const float3 dL = qL - lc;
                    const float dist = sqrt(dSq);
                    float sep = dist - totalR;
                    float3 nL = -triN;
                    if(dist > 1.0e-5f){
                        const float3 candN = dL / dist;
                        if(!cullBackfaces && dot(candN, triN) > -0.95f)
                            nL = candN;
                        else if(cullBackfaces && dot(candN, triN) < 0.0f)
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
                    if(cullBackfaces && dot(nL, triN) >= 0.0f)
                        continue;
                    const float3 wN = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(nL, meshRot), float3(0.0f, -1.0f, 0.0f));
                    const float3 wQ = meshOrig + nwbPhysicsQuatRotate(qL, meshRot);
                    if(!foundSingle || sep < bestSep){
                        foundSingle = true;
                        bestSep = sep;
                        chosenWorldN = meshIsA ? -wN : wN;
                        bestLocalOtherToMesh = nL;
                        bestWorldOther = wSphereCenter + wN * otherR;
                        bestWorldMesh = wQ - wN * meshR;
                        bestLocalTriIdx = localTriIdx;
                    }
                }
                else{
                    if(isClosed){
                        for(uint pi = 0u; pi < 3u; ++pi){
                            const float3 pL = capProbes[pi];
                            const float3 dTri = max(float3(0.0f, 0.0f, 0.0f), max(tMin - pL, pL - tMax));
                            if(dot(dTri, dTri) > probeClosestDistSq[pi] + 1.0e-5f)
                                continue;
                            const float3 qProbe = nwbPhysicsClosestPointOnTriangle(pL, v0, v1, v2);
                            const float3 diffProbe = pL - qProbe;
                            const float dProbeSq = dot(diffProbe, diffProbe);
                            if(dProbeSq <= probeRBoundSq){
                                const float side = dot(triN, pL - v0);
                                const bool front = side >= -1.0e-5f;
                                const bool back = side <= 1.0e-5f;
                                if(!hasProbeClosest[pi] || dProbeSq < probeClosestDistSq[pi] - 1.0e-5f){
                                    hasProbeClosest[pi] = true;
                                    probeClosestDistSq[pi] = dProbeSq;
                                    probeClosestPt[pi] = qProbe;
                                    probeClosestN[pi] = triN;
                                    probeClosestTri[pi] = localTriIdx;
                                    hasProbeFrontTie[pi] = front;
                                    hasProbeBackTie[pi] = back;
                                }
                                else if(dProbeSq <= probeClosestDistSq[pi] + 1.0e-5f){
                                    if(front)
                                        hasProbeFrontTie[pi] = true;
                                    if(back)
                                        hasProbeBackTie[pi] = true;
                                    if(dProbeSq < probeClosestDistSq[pi]){
                                        probeClosestDistSq[pi] = dProbeSq;
                                        probeClosestPt[pi] = qProbe;
                                        probeClosestN[pi] = triN;
                                        probeClosestTri[pi] = localTriIdx;
                                    }
                                }
                            }
                        }
                    }

                    float3 triNLocal = triN;
                    if(!cullBackfaces && dot(capMidL - v0, triN) < 0.0f)
                        triNLocal = -triN;
                    const float capD0 = dot(triNLocal, capL0 - v0);
                    const float capD1 = dot(triNLocal, capL1 - v0);
                    if(cullBackfaces && capD0 < -NWB_PHYSICS_LINEAR_SLOP && capD1 < -NWB_PHYSICS_LINEAR_SLOP)
                        continue;

                    float3 gjkPtOtherL = float3(0.0f, 0.0f, 0.0f);
                    float3 gjkPtTriL = float3(0.0f, 0.0f, 0.0f);
                    float gjkDist = 0.0f;
                    bool gjkDeep = false;
                    nwbPhysicsRunGjkDistanceCached(otherCache, false, float3(0.0f, 0.0f, 0.0f), float3(0.0f, 0.0f, 0.0f), float3(0.0f, 0.0f, 0.0f), otherCache, true, v0, v1, v2, hullVertexSlot, gjkPtOtherL, gjkPtTriL, gjkDist, gjkDeep);

                    if(!gjkDeep && gjkDist > 1.0e-5f){
                        float sep = gjkDist - totalR;
                        if(sep > maxDist)
                            continue;
                        float3 nOtherToMeshL = nwbPhysicsSafeNormal(gjkPtTriL - gjkPtOtherL, -triNLocal);
                        float3 smoothedL = -nOtherToMeshL;
                        float3 mPtL = gjkPtTriL;
                        const bool smoothed = nwbPhysicsSmoothMeshNormal(triN, edgeFlags, gjkPtTriL, v0, v1, v2, -nOtherToMeshL, smoothedL);
                        if(smoothed){
                            const float planeDist = dot(smoothedL, gjkPtOtherL - v0);
                            if(planeDist - totalR > maxDist)
                                continue;
                            sep = planeDist - totalR;
                            nOtherToMeshL = -smoothedL;
                        }
                        if(cullBackfaces && dot(nOtherToMeshL, triN) >= 0.0f)
                            continue;
                        const float3 nOtherToMesh = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(nOtherToMeshL, meshRot), float3(0.0f, -1.0f, 0.0f));
                        const float3 wO = meshOrig + nwbPhysicsQuatRotate(gjkPtOtherL, meshRot) + nOtherToMesh * otherR;
                        const float3 wM = smoothed
                            ? (wO + nOtherToMesh * sep)
                            : (meshOrig + nwbPhysicsQuatRotate(mPtL, meshRot) - nOtherToMesh * meshR);
                        const float3 wNOut = meshIsA ? -nOtherToMesh : nOtherToMesh;
                        if(!foundSingle || sep < bestSep){
                            foundSingle = true;
                            bestSep = sep;
                            chosenWorldN = wNOut;
                            bestLocalOtherToMesh = nOtherToMeshL;
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
                        const float3 deepL = cullBackfaces ? ((capD0 < capD1) ? capL0 : capL1) : (abs(capD1) < abs(capD0) ? capL1 : capL0);
                        const float sep = cullBackfaces ? (min(capD0, capD1) - totalR) : (-min(abs(capD0), abs(capD1)) - totalR);
                        const float3 deepW = meshOrig + nwbPhysicsQuatRotate(deepL, meshRot);
                        const float3 nOtherToMeshL = -triNLocal;
                        const float3 nOtherToMesh = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(nOtherToMeshL, meshRot), float3(0.0f, -1.0f, 0.0f));
                        const float3 wNOut = meshIsA ? -nOtherToMesh : nOtherToMesh;
                        if(!foundSingle || sep < bestSep){
                            foundSingle = true;
                            bestSep = sep;
                            chosenWorldN = wNOut;
                            bestLocalOtherToMesh = nOtherToMeshL;
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
        if(anyClosedMesh || ((asuint(firstTri1.w) & NWB_PHYSICS_MESH_FLAG_CLOSED) != 0u)){
            bool meshOverflow = false;
            if(otherType == NWB_PHYSICS_SHAPE_TYPE_SPHERE){
                const float3 lc = sphereLc;
                bool insideSphere = false;
                bool usedCachedClosest = false;
                float3 qL = float3(0.0f, 0.0f, 0.0f);
                float3 triNL = float3(0.0f, 1.0f, 0.0f);
                uint localTri = 0u;
                float dist = 0.0f;
                if(hasProbeClosest[0] && hasProbeFrontTie[0] && !hasProbeBackTie[0]){
                    insideSphere = false;
                }
                else if(hasProbeClosest[0] && hasProbeBackTie[0] && !hasProbeFrontTie[0]){
                    insideSphere = true;
                    usedCachedClosest = true;
                    qL = probeClosestPt[0];
                    triNL = probeClosestN[0];
                    localTri = probeClosestTri[0];
                    dist = sqrt(probeClosestDistSq[0]);
                }
                else{
                    insideSphere = nwbPhysicsIsPointInsideClosedMesh(nodeBase, triBase, meshNodeSlot, meshTriSlot, lc, meshOverflow);
                    if(meshOverflow){
                        result.pairInfo.w = 1u;
                        return;
                    }
                    if(insideSphere && hasProbeClosest[0]){
                        usedCachedClosest = true;
                        qL = probeClosestPt[0];
                        triNL = probeClosestN[0];
                        localTri = probeClosestTri[0];
                        dist = sqrt(probeClosestDistSq[0]);
                    }
                }
                if(insideSphere){
                    const bool foundClosest = usedCachedClosest
                        ? true
                        : nwbPhysicsFindClosestPointOnClosedMesh(nodeBase, triBase, meshNodeSlot, meshTriSlot, lc, qL, triNL, localTri, dist, meshOverflow);
                    if(meshOverflow){
                        result.pairInfo.w = 1u;
                        return;
                    }
                    if(foundClosest){
                        const float3 nL = -triNL;
                        const float sep = -dist - totalR;
                        if(sep <= maxDist && (!foundSingle || sep < bestSep)){
                            const float3 wN = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(nL, meshRot), float3(0.0f, -1.0f, 0.0f));
                            const float3 wQ = meshOrig + nwbPhysicsQuatRotate(qL, meshRot);
                            foundSingle = true;
                            usedInterior = true;
                            bestSep = sep;
                            chosenWorldN = meshIsA ? -wN : wN;
                            bestLocalOtherToMesh = nL;
                            bestWorldOther = wSphereCenter + wN * otherR;
                            bestWorldMesh = wQ - wN * meshR;
                            bestLocalTriIdx = localTri;
                        }
                    }
                }
            }
            else{
                for(uint pi = 0u; pi < 3u; ++pi){
                    const float3 pL = capProbes[pi];
                    bool insidePt = false;
                    bool usedCachedClosest = false;
                    float3 qL = float3(0.0f, 0.0f, 0.0f);
                    float3 triNL = float3(0.0f, 1.0f, 0.0f);
                    uint localTri = 0u;
                    float dist = 0.0f;
                    if(hasProbeClosest[pi] && hasProbeFrontTie[pi] && !hasProbeBackTie[pi]){
                        insidePt = false;
                    }
                    else if(hasProbeClosest[pi] && hasProbeBackTie[pi] && !hasProbeFrontTie[pi]){
                        insidePt = true;
                        usedCachedClosest = true;
                        qL = probeClosestPt[pi];
                        triNL = probeClosestN[pi];
                        localTri = probeClosestTri[pi];
                        dist = sqrt(probeClosestDistSq[pi]);
                    }
                    else{
                        insidePt = nwbPhysicsIsPointInsideClosedMesh(nodeBase, triBase, meshNodeSlot, meshTriSlot, pL, meshOverflow);
                        if(meshOverflow){
                            result.pairInfo.w = 1u;
                            return;
                        }
                        if(insidePt && hasProbeClosest[pi]){
                            usedCachedClosest = true;
                            qL = probeClosestPt[pi];
                            triNL = probeClosestN[pi];
                            localTri = probeClosestTri[pi];
                            dist = sqrt(probeClosestDistSq[pi]);
                        }
                    }
                    if(!insidePt)
                        continue;
                    const bool foundClosest = usedCachedClosest
                        ? true
                        : nwbPhysicsFindClosestPointOnClosedMesh(nodeBase, triBase, meshNodeSlot, meshTriSlot, pL, qL, triNL, localTri, dist, meshOverflow);
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
                            bestLocalOtherToMesh = nL;
                            bestWorldOther = wP + wN * otherR;
                            bestWorldMesh = wQ - wN * meshR;
                            bestLocalTriIdx = localTri;
                        }
                    }
                }
            }
        }
    }
    if(foundSingle && !usedInterior && (otherType == NWB_PHYSICS_SHAPE_TYPE_CAPSULE || otherType == NWB_PHYSICS_SHAPE_TYPE_SEGMENT)){
        const float3 nL = nwbPhysicsSafeNormal(-bestLocalOtherToMesh, bestTriRawN);
        if(abs(dot(nL, bestTriRawN)) > 0.99f){
            const float3 l0 = capL0;
            const float3 l1 = capL1;
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
                        const float3 triPts[2] = { clipA, clipB };
                        const float triDs[2] = { dA, dB };
                        const float triSeps[2] = { sepA, sepB };
                        const float3 wPlaneN = meshIsA ? -chosenWorldN : chosenWorldN;
                        uint emitted = 0u;
                        for(uint k = 0u; k < 2u; ++k){
                            if(triSeps[k] <= maxDist){
                                const float3 ptCapL = triPts[k];
                                const float3 ptMeshL = ptCapL - nL * triDs[k];
                                const float3 wCap = meshOrig + nwbPhysicsQuatRotate(ptCapL, meshRot) + wPlaneN * otherR;
                                const float3 wMesh = meshOrig + nwbPhysicsQuatRotate(ptMeshL, meshRot) - wPlaneN * meshR;
                                const float3 wAOut = meshIsA ? wMesh : wCap;
                                const float3 wBOut = meshIsA ? wCap : wMesh;
                                result.anchorAFeature[emitted] = float4(wAOut - bodyPosA, asfloat(nwbPhysicsMakeFeatureId(3u, emitted, 3u, bestLocalTriIdx & 0xffu)));
                                result.anchorBSeparation[emitted] = float4(wBOut - bodyPosB, triSeps[k]);
                                ++emitted;
                            }
                        }
                        if(emitted > 0u){
                            result.normalPad = float4(chosenWorldN, asfloat(bestLocalTriIdx));
                            result.pairInfo.z = emitted;
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
        result.normalPad = float4(chosenWorldN, asfloat(bestLocalTriIdx));
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
    const uint triBase = asuint(meshP.localRotationOrMeta.x);
    if(nodeBase == NWB_PHYSICS_INVALID_INDEX || triBase == NWB_PHYSICS_INVALID_INDEX){
        result.pairInfo.w = 1u;
        return;
    }

    const uint otherType = otherP.bodyIndexTypeShape.y;
    if(otherType == NWB_PHYSICS_SHAPE_TYPE_SPHERE || otherType == NWB_PHYSICS_SHAPE_TYPE_CAPSULE || otherType == NWB_PHYSICS_SHAPE_TYPE_SEGMENT){
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

    const NwbPhysicsGpuSupportCache otherCache = nwbPhysicsBuildSupportCache(otherP, relOrig, relRot);
    const bool otherIsBox = otherCache.isBox;
    float3 relAxX = otherCache.axX;
    float3 relAxY = otherCache.axY;
    float3 relAxZ = otherCache.axZ;
    if(otherIsBox && otherP.localRotationOrMeta.w < 0.999999f)
        nwbPhysicsQuatToAxes(relRot, relAxX, relAxY, relAxZ);

    float3 lc = relOrig + relAxX * otherP.localCenterRadius.x + relAxY * otherP.localCenterRadius.y + relAxZ * otherP.localCenterRadius.z;
    const float3 axX = abs(otherCache.axX);
    const float3 axY = abs(otherCache.axY);
    const float3 axZ = abs(otherCache.axZ);
    const float3 h = otherP.localExtentOrP1.xyz;
    float3 ext = axX * h.x + axY * h.y + axZ * h.z + float3(queryR, queryR, queryR);
    float3 queryMin = lc - ext;
    float3 queryMax = lc + ext;
    if(otherType == NWB_PHYSICS_SHAPE_TYPE_CYLINDER || otherType == NWB_PHYSICS_SHAPE_TYPE_CONE){
        const float cylR = otherCache.extraParams.x;
        const float cylHh = otherCache.extraParams.y;
        lc = otherCache.p0OrCenter;
        ext = axX * cylR + axY * cylHh + axZ * cylR + float3(queryR, queryR, queryR);
        queryMin = lc - ext;
        queryMax = lc + ext;
    }
    else if(otherType == NWB_PHYSICS_SHAPE_TYPE_TAPERED_CAPSULE){
        const float r0e = otherCache.extraParams.x + queryR;
        const float r1e = otherCache.extraParams.y + queryR;
        queryMin = min(otherCache.p0OrCenter - float3(r0e, r0e, r0e), otherCache.p1OrExtent - float3(r1e, r1e, r1e));
        queryMax = max(otherCache.p0OrCenter + float3(r0e, r0e, r0e), otherCache.p1OrExtent + float3(r1e, r1e, r1e));
        lc = (otherCache.p0OrCenter + otherCache.p1OrExtent) * 0.5f;
        ext = max(abs(queryMax - lc), abs(lc - queryMin));
    }

    float bestSep = 1.0e20f;
    float3 chosenWorldN = float3(0.0f, -1.0f, 0.0f);
    uint bestLocalTri = NWB_PHYSICS_INVALID_INDEX;
    bool anyClosedMesh = false;

    const float hullProbeBound = max(min(ext.x, min(ext.y, ext.z)) - 1.0e-4f, 0.0f);
    const float hullProbeBoundSq = hullProbeBound * hullProbeBound;
    bool hasHullClosest = false;
    bool hasHullFrontTie = false;
    bool hasHullBackTie = false;
    float closestHullDistSq = hullProbeBoundSq;
    float3 closestHullPt = float3(0.0f, 0.0f, 0.0f);
    float3 closestHullN = float3(0.0f, 1.0f, 0.0f);
    uint closestHullTri = 0u;

    float3 candWA[NWB_PHYSICS_MAX_MANIFOLD_POINTS];
    float3 candWB[NWB_PHYSICS_MAX_MANIFOLD_POINTS];
    float3 candNorm[NWB_PHYSICS_MAX_MANIFOLD_POINTS];
    float candSep[NWB_PHYSICS_MAX_MANIFOLD_POINTS];
    uint candFeat[NWB_PHYSICS_MAX_MANIFOLD_POINTS];
    [loop] for(uint kInit = 0u; kInit < NWB_PHYSICS_MAX_MANIFOLD_POINTS; ++kInit){
        candWA[kInit] = float3(0.0f, 0.0f, 0.0f);
        candWB[kInit] = float3(0.0f, 0.0f, 0.0f);
        candNorm[kInit] = float3(0.0f, 1.0f, 0.0f);
        candSep[kInit] = 0.0f;
        candFeat[kInit] = 0u;
    }
    uint candCount = 0u;
    bool overflow = false;

    uint stack[64];
    uint sp = 0u;
    stack[sp++] = 0u;
    while(sp > 0u && !overflow){
        const uint nodeRel = stack[--sp];
        const float4 n0 = g_NwbPhysicsFloat4Buffers[meshNodeSlot][(nodeBase + nodeRel) * 2u + 0u];
        const float4 n1 = g_NwbPhysicsFloat4Buffers[meshNodeSlot][(nodeBase + nodeRel) * 2u + 1u];
        if(!nwbPhysicsAabbOverlaps(n0.xyz, n1.xyz, queryMin, queryMax))
            continue;

        const uint meta = asuint(n1.w);
        if((meta & 0x80000000u) != 0u){
            const uint firstTri = asuint(n0.w);
            const uint triCount = meta & 0x7fffffffu;
            [loop] for(uint ti = 0u; ti < triCount; ++ti){
                const uint localTriIdx = firstTri + ti;
                const uint triIdx = triBase + localTriIdx;
                float3 v0;
                float3 v1;
                float3 v2;
                float3 tMin;
                float3 tMax;
                uint edgeFlags;
                bool isClosed;
                bool cullBackfaces;
                float3 triN;
                if(!nwbPhysicsFetchMeshLeafTriangleInAabb(meshTriSlot, triIdx, queryMin, queryMax, anyClosedMesh, v0, v1, v2, tMin, tMax, edgeFlags, isClosed, cullBackfaces, triN))
                    continue;

                if(isClosed){
                    const float3 dTri = max(float3(0.0f, 0.0f, 0.0f), max(tMin - lc, lc - tMax));
                    if(dot(dTri, dTri) <= closestHullDistSq + 1.0e-5f){
                        const float3 qHull = nwbPhysicsClosestPointOnTriangle(lc, v0, v1, v2);
                        const float3 diffHull = lc - qHull;
                        const float dHullSq = dot(diffHull, diffHull);
                        if(dHullSq <= hullProbeBoundSq){
                            const float side = dot(triN, lc - v0);
                            const bool front = side >= -1.0e-5f;
                            const bool back = side <= 1.0e-5f;
                            if(!hasHullClosest || dHullSq < closestHullDistSq - 1.0e-5f){
                                hasHullClosest = true;
                                closestHullDistSq = dHullSq;
                                closestHullPt = qHull;
                                closestHullN = triN;
                                closestHullTri = localTriIdx;
                                hasHullFrontTie = front;
                                hasHullBackTie = back;
                            }
                            else if(dHullSq <= closestHullDistSq + 1.0e-5f){
                                if(front)
                                    hasHullFrontTie = true;
                                if(back)
                                    hasHullBackTie = true;
                                if(dHullSq < closestHullDistSq){
                                    closestHullDistSq = dHullSq;
                                    closestHullPt = qHull;
                                    closestHullN = triN;
                                    closestHullTri = localTriIdx;
                                }
                            }
                        }
                    }
                }

                if(otherIsBox){
                    const float3 bh = otherCache.p1OrExtent;
                    const float boxProjN =
                        abs(dot(otherCache.axX, triN)) * bh.x
                        + abs(dot(otherCache.axY, triN)) * bh.y
                        + abs(dot(otherCache.axZ, triN)) * bh.z;
                    if(abs(dot(otherCache.p0OrCenter - v0, triN)) - boxProjN > queryR)
                        continue;

                    const float3 d0 = v0 - otherCache.p0OrCenter;
                    const float3 d1 = v1 - otherCache.p0OrCenter;
                    const float3 d2 = v2 - otherCache.p0OrCenter;
                    const float px0 = dot(d0, otherCache.axX);
                    const float px1 = dot(d1, otherCache.axX);
                    const float px2 = dot(d2, otherCache.axX);
                    const float limX = bh.x + queryR;
                    if(min(px0, min(px1, px2)) > limX || max(px0, max(px1, px2)) < -limX)
                        continue;

                    const float py0 = dot(d0, otherCache.axY);
                    const float py1 = dot(d1, otherCache.axY);
                    const float py2 = dot(d2, otherCache.axY);
                    const float limY = bh.y + queryR;
                    if(min(py0, min(py1, py2)) > limY || max(py0, max(py1, py2)) < -limY)
                        continue;

                    const float pz0 = dot(d0, otherCache.axZ);
                    const float pz1 = dot(d1, otherCache.axZ);
                    const float pz2 = dot(d2, otherCache.axZ);
                    const float limZ = bh.z + queryR;
                    if(min(pz0, min(pz1, pz2)) > limZ || max(pz0, max(pz1, pz2)) < -limZ)
                        continue;
                }

                float3 triNLocal = triN;
                if(!cullBackfaces && dot(lc - v0, triN) < 0.0f)
                    triNLocal = -triN;

                const bool specialPrismOrTapered =
                    (otherType == NWB_PHYSICS_SHAPE_TYPE_CYLINDER)
                    || (otherType == NWB_PHYSICS_SHAPE_TYPE_CONE)
                    || (otherType == NWB_PHYSICS_SHAPE_TYPE_TAPERED_CAPSULE);
                const uint surfStatus = nwbPhysicsCollideConvexWithSurfaceTriangle(
                    otherP,
                    otherCache,
                    relOrig,
                    relRot,
                    meshOrig,
                    meshRot,
                    meshIsA,
                    otherR,
                    meshR,
                    hullVertexSlot,
                    edgeFlags,
                    false,
                    0u,
                    0u,
                    0u,
                    float3(0.0f, 0.0f, 0.0f),
                    0.0f,
                    0.0f,
                    0.0f,
                    0u,
                    0u,
                    0u,
                    0u,
                    float3(0.0f, 0.0f, 0.0f),
                    float3(0.0f, 0.0f, 0.0f),
                    float3(0.0f, 0.0f, 0.0f),
                    float3(0.0f, 0.0f, 0.0f),
                    v0,
                    v1,
                    v2,
                    triN,
                    triNLocal,
                    cullBackfaces,
                    specialPrismOrTapered,
                    false,
                    specialPrismOrTapered,
                    maxDist,
                    4u,
                    0u,
                    3u,
                    localTriIdx,
                    bestSep,
                    chosenWorldN,
                    bestLocalTri,
                    candWA,
                    candWB,
                    candNorm,
                    candSep,
                    candFeat,
                    candCount
                );
                if(surfStatus == 1u){
                    result.pairInfo.w = 1u;
                    return;
                }
                if(surfStatus == 0u)
                    continue;

                const uint fCountO = nwbPhysicsHullFaceCount(otherP);
                    const uint eCountO = nwbPhysicsHullEdgeCount(otherP);
                    const uint vCountO = nwbPhysicsHullVertexCount(otherP);
                    if(fCountO == 0u || fCountO > NWB_PHYSICS_MAX_HULL_FACES || eCountO > NWB_PHYSICS_MAX_HULL_EDGES || vCountO == 0u || vCountO > NWB_PHYSICS_MAX_HULL_VERTICES){
                        result.pairInfo.w = 1u;
                        return;
                    }

                    uint tmpIdx = 0u;
                    const float3 supInB = nwbPhysicsSupportCached(otherCache, hullVertexSlot, -triNLocal, tmpIdx);
                    const float sepTri = dot(triNLocal, supInB - v0) - totalR;
                    if(sepTri > maxDist)
                        continue;

                    float bestHullSep = -1.0e20f;
                    uint bestHullFace = 0u;
                    float3 bestHullN = float3(0.0f, 1.0f, 0.0f);
                    float bestHullOff = 0.0f;
                    bool separated = false;
                    [loop] for(uint fi = 0u; fi < fCountO; ++fi){
                        const float4 pl = nwbPhysicsHullLocalFacePlane(otherP, hullVertexSlot, fi);
                        const float3 fNLocal = nwbPhysicsSafeNormal(relAxX * pl.x + relAxY * pl.y + relAxZ * pl.z, float3(0.0f, 1.0f, 0.0f));
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

                    [loop] for(uint eA = 0u; eA < eCountO; ++eA){
                        const uint edgePack = nwbPhysicsHullEdgePacked(otherP, hullVertexSlot, eA);
                        const uint vA0Idx = edgePack & 0xffu;
                        const uint vA1Idx = (edgePack >> 8u) & 0xffu;
                        if(vA0Idx >= vCountO || vA1Idx >= vCountO)
                            continue;
                        const uint fA0Idx = (edgePack >> 16u) & 0xffu;
                        const uint fA1Idx = (edgePack >> 24u) & 0xffu;
                        const bool cullMinkowski = (fA0Idx < fCountO && fA1Idx < fCountO);
                        float3 nA0 = float3(0.0f, 0.0f, 0.0f);
                        float3 nA1 = float3(0.0f, 0.0f, 0.0f);
                        if(cullMinkowski){
                            const float3 ln0 = nwbPhysicsHullLocalFacePlane(otherP, hullVertexSlot, fA0Idx).xyz;
                            const float3 ln1 = nwbPhysicsHullLocalFacePlane(otherP, hullVertexSlot, fA1Idx).xyz;
                            nA0 = relAxX * ln0.x + relAxY * ln0.y + relAxZ * ln0.z;
                            nA1 = relAxX * ln1.x + relAxY * ln1.y + relAxZ * ln1.z;
                        }
                        const float3 lv0 = nwbPhysicsHullLocalVertex(otherP, hullVertexSlot, vA0Idx);
                        const float3 lv1 = nwbPhysicsHullLocalVertex(otherP, hullVertexSlot, vA1Idx);
                        const float3 eA0 = relOrig + relAxX * lv0.x + relAxY * lv0.y + relAxZ * lv0.z;
                        const float3 eA1 = relOrig + relAxX * lv1.x + relAxY * lv1.y + relAxZ * lv1.z;
                        const float3 dirA = eA1 - eA0;
                        const float dirALenSq = dot(dirA, dirA);

                        [loop] for(uint tk = 0u; tk < 3u; ++tk){
                            float3 dirB = te0;
                            if(tk == 1u)
                                dirB = te1;
                            else if(tk == 2u)
                                dirB = te2;
                            if(cullMinkowski && (dot(nA0, dirB) * dot(nA1, dirB) > 1.0e-5f * dot(dirB, dirB)))
                                continue;
                            const float3 rawCross = cross(dirA, dirB);
                            const float lenSq = dot(rawCross, rawCross);
                            const float normTol = 1.0e-6f * max(dirALenSq * dot(dirB, dirB), 1.0e-12f);
                            if(lenSq <= normTol)
                                continue;

                            const float3 axis = rawCross * rsqrt(lenSq);
                            float minA = 0.0f;
                            float maxA = 0.0f;
                            nwbPhysicsSupportMinMaxCached(otherCache, hullVertexSlot, axis, minA, maxA);
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

                    if(cullBackfaces && dot(winNInB, triNLocal) >= 0.0f)
                        continue;

                    const float3 worldWinN = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(winNInB, meshRot), float3(0.0f, -1.0f, 0.0f));
                    const float3 wNOut = meshIsA ? -worldWinN : worldWinN;

                    if(winKind == 2u){
                        const uint edgePack = nwbPhysicsHullEdgePacked(otherP, hullVertexSlot, bestEdgeHullIdx);
                        const uint vA0Idx = edgePack & 0xffu;
                        const uint vA1Idx = (edgePack >> 8u) & 0xffu;
                        const float3 lv0 = nwbPhysicsHullLocalVertex(otherP, hullVertexSlot, vA0Idx);
                        const float3 lv1 = nwbPhysicsHullLocalVertex(otherP, hullVertexSlot, vA1Idx);
                        const float3 eA0 = relOrig + relAxX * lv0.x + relAxY * lv0.y + relAxZ * lv0.z;
                        const float3 eA1 = relOrig + relAxX * lv1.x + relAxY * lv1.y + relAxZ * lv1.z;
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
                            bestLocalTri = localTriIdx;
                        }
                        const float3 wAOut = meshIsA ? wM : wO;
                        const float3 wBOut = meshIsA ? wO : wM;
                        nwbPhysicsPushMeshCand(wAOut, wBOut, wNOut, winSep, nwbPhysicsMakeFeatureId(4u, bestEdgeHullIdx & 0xffu, 3u, (localTriIdx & 0xffu) ^ bestEdgeTriIdx), candWA, candWB, candNorm, candSep, candFeat, candCount);
                    }
                    else if(winKind == 1u){
                        float4 refPoly[NWB_PHYSICS_GPU_MAX_CLIP_POINTS];
                        float3 refNUnused = bestHullN;
                        float refOff = 0.0f;
                        const uint refPolyCount = nwbPhysicsGatherHullWorldFacePoly(otherP, relOrig, relRot, hullVertexSlot, bestHullFace, refPoly, refNUnused, refOff);
                        if(refPolyCount < 3u){
                            overflow = true;
                        }
                        else{
                            float4 poly0[NWB_PHYSICS_GPU_MAX_CLIP_POINTS];
                            float4 poly1[NWB_PHYSICS_GPU_MAX_CLIP_POINTS];
                            poly0[0] = float4(v0, 0.0f);
                            poly0[1] = float4(v1, 0.0f);
                            poly0[2] = float4(v2, 0.0f);
                            uint polyCount = 3u;
                            const float3 rv0First = refPoly[0].xyz;
                            const float3 rv1First = refPoly[1].xyz;
                            const float3 sideNFirst = nwbPhysicsSafeNormal(cross(rv1First - rv0First, refNUnused), float3(1.0f, 0.0f, 0.0f));
                            polyCount = nwbPhysicsClipPolygonPlane(poly0, polyCount, sideNFirst, dot(sideNFirst, rv0First), 0u, poly1, overflow);
                            bool polyIn0 = false;
                            for(uint i = 1u; i < refPolyCount && !overflow && polyCount > 0u; ++i){
                                const float3 rv0 = refPoly[i].xyz;
                                const float3 rv1 = refPoly[(i + 1u < refPolyCount) ? (i + 1u) : 0u].xyz;
                                const float3 sideN = nwbPhysicsSafeNormal(cross(rv1 - rv0, refNUnused), float3(1.0f, 0.0f, 0.0f));
                                const float sideOff = dot(sideN, rv0);
                                if(polyIn0)
                                    polyCount = nwbPhysicsClipPolygonPlane(poly0, polyCount, sideN, sideOff, i, poly1, overflow);
                                else
                                    polyCount = nwbPhysicsClipPolygonPlane(poly1, polyCount, sideN, sideOff, i, poly0, overflow);
                                polyIn0 = !polyIn0;
                            }
                            if(!overflow){
                                for(uint k = 0u; k < polyCount && !overflow; ++k){
                                    const float3 ptB = polyIn0 ? poly0[k].xyz : poly1[k].xyz;
                                    const float sep = dot(bestHullN, ptB) - bestHullOff - totalR;
                                    if(sep > maxDist)
                                        continue;
                                    const float3 wM = meshOrig + nwbPhysicsQuatRotate(ptB, meshRot) - worldWinN * meshR;
                                    const float3 wO = meshOrig + nwbPhysicsQuatRotate(ptB - bestHullN * (sep + totalR - otherR), meshRot);
                                    if(sep < bestSep){
                                        bestSep = sep;
                                        chosenWorldN = wNOut;
                                        bestLocalTri = localTriIdx;
                                    }
                                    const float3 wAOut = meshIsA ? wM : wO;
                                    const float3 wBOut = meshIsA ? wO : wM;
                                    nwbPhysicsPushMeshCand(wAOut, wBOut, wNOut, sep, nwbPhysicsMakeFeatureId(4u, bestHullFace & 0xffu, 3u, (localTriIdx & 0xffu) ^ k), candWA, candWB, candNorm, candSep, candFeat, candCount);
                                }
                            }
                        }
                    }
                    else{
                        const float3 triNInOther = float3(dot(relAxX, triNLocal), dot(relAxY, triNLocal), dot(relAxZ, triNLocal));
                        uint incFaceIdx = 0u;
                        float minDot = 2.0f;
                        [loop] for(uint fi = 0u; fi < fCountO; ++fi){
                            const float4 lp = nwbPhysicsHullLocalFacePlane(otherP, hullVertexSlot, fi);
                            const float d = dot(lp.xyz, triNInOther);
                            if(d < minDot){
                                minDot = d;
                                incFaceIdx = fi;
                            }
                        }
                        float4 poly0[NWB_PHYSICS_GPU_MAX_CLIP_POINTS];
                        float4 poly1[NWB_PHYSICS_GPU_MAX_CLIP_POINTS];
                        float3 incNUnused = triNLocal;
                        float incOffUnused = 0.0f;
                        uint polyCount = nwbPhysicsGatherHullWorldFacePoly(otherP, relOrig, relRot, hullVertexSlot, incFaceIdx, poly0, incNUnused, incOffUnused);
                        if(polyCount < 3u){
                            overflow = true;
                        }
                        else{
                            const float3 sn0 = nwbPhysicsSafeNormal(cross(te0, triN), float3(1.0f, 0.0f, 0.0f));
                            polyCount = nwbPhysicsClipPolygonPlane(poly0, polyCount, sn0, dot(sn0, v0) + 5.0e-3f, 0u, poly1, overflow);
                            if(!overflow && polyCount > 0u){
                                const float3 sn1 = nwbPhysicsSafeNormal(cross(te1, triN), float3(1.0f, 0.0f, 0.0f));
                                polyCount = nwbPhysicsClipPolygonPlane(poly1, polyCount, sn1, dot(sn1, v1) + 5.0e-3f, 1u, poly0, overflow);
                            }
                            if(!overflow && polyCount > 0u){
                                const float3 sn2 = nwbPhysicsSafeNormal(cross(te2, triN), float3(1.0f, 0.0f, 0.0f));
                                polyCount = nwbPhysicsClipPolygonPlane(poly0, polyCount, sn2, dot(sn2, v2) + 5.0e-3f, 2u, poly1, overflow);
                            }
                            if(!overflow){
                                [loop] for(uint k = 0u; k < polyCount && !overflow; ++k){
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
                                        bestLocalTri = localTriIdx;
                                    }
                                    const float3 wAOut = meshIsA ? wM : wO;
                                    const float3 wBOut = meshIsA ? wO : wM;
                                    nwbPhysicsPushMeshCand(wAOut, wBOut, wNOut, sep, nwbPhysicsMakeFeatureId(4u, k & 0xffu, 3u, localTriIdx & 0xffu), candWA, candWB, candNorm, candSep, candFeat, candCount);
                                }
                            }
                        }
                    }
            }
        }
        else{
            if(sp + 2u > 64u){
                overflow = true;
            }
            else{
                stack[sp++] = asuint(n0.w);
                stack[sp++] = meta & 0x7fffffffu;
            }
        }
    }

    if(!overflow && (candCount == 0u || bestSep > 0.0f)){
        const float4 firstTri1 = g_NwbPhysicsFloat4Buffers[meshTriSlot][triBase * 3u + 1u];
        if(anyClosedMesh || ((asuint(firstTri1.w) & NWB_PHYSICS_MESH_FLAG_CLOSED) != 0u)){
            const float3 hullCenterL = lc;
            bool insideHullCenter = false;
            bool usedCachedClosest = false;
            float3 qL = float3(0.0f, 0.0f, 0.0f);
            float3 triNL = float3(0.0f, 1.0f, 0.0f);
            uint localTri = 0u;
            float dist = 0.0f;
            if(hasHullClosest && hasHullFrontTie && !hasHullBackTie){
                insideHullCenter = false;
            }
            else if(hasHullClosest && hasHullBackTie && !hasHullFrontTie){
                insideHullCenter = true;
                usedCachedClosest = true;
                qL = closestHullPt;
                triNL = closestHullN;
                localTri = closestHullTri;
                dist = sqrt(closestHullDistSq);
            }
            else{
                insideHullCenter = nwbPhysicsIsPointInsideClosedMesh(nodeBase, triBase, meshNodeSlot, meshTriSlot, hullCenterL, overflow);
                if(!overflow && insideHullCenter && hasHullClosest){
                    usedCachedClosest = true;
                    qL = closestHullPt;
                    triNL = closestHullN;
                    localTri = closestHullTri;
                    dist = sqrt(closestHullDistSq);
                }
            }
            if(!overflow && insideHullCenter){
                const bool foundClosest = usedCachedClosest
                    ? true
                    : nwbPhysicsFindClosestPointOnClosedMesh(nodeBase, triBase, meshNodeSlot, meshTriSlot, hullCenterL, qL, triNL, localTri, dist, overflow);
                if(!overflow && foundClosest){
                    uint tmpSupIdx = 0u;
                    const float3 supL = nwbPhysicsSupportCached(otherCache, hullVertexSlot, -triNL, tmpSupIdx);
                    const float depth = dot(qL - supL, triNL);
                    const float sep = -(max(depth, 0.0f) + totalR);
                    const float3 worldOut = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(triNL, meshRot), float3(0.0f, 1.0f, 0.0f));
                    const float3 wNOut = meshIsA ? worldOut : -worldOut;
                    const float3 supW = meshOrig + nwbPhysicsQuatRotate(supL, meshRot);
                    const float3 closeW = meshOrig + nwbPhysicsQuatRotate(qL, meshRot);
                    const float3 wO = supW - worldOut * otherR;
                    const float3 wM = closeW + worldOut * meshR;
                    const float3 wAOut = meshIsA ? wM : wO;
                    const float3 wBOut = meshIsA ? wO : wM;
                    if(candCount == 0u || sep < bestSep){
                        bestSep = sep;
                        chosenWorldN = wNOut;
                        bestLocalTri = localTri;
                    }
                    nwbPhysicsPushMeshCand(wAOut, wBOut, wNOut, sep, nwbPhysicsMakeFeatureId(4u, 0u, 3u, localTri & 0xffu), candWA, candWB, candNorm, candSep, candFeat, candCount);
                }
            }
        }
    }
    if(overflow){
        result.pairInfo.w = 1u;
        return;
    }
    if(candCount > 0u){
        nwbPhysicsFinalizeMeshCand(candWA, candWB, candNorm, candSep, candFeat, candCount, chosenWorldN, bodyPosA, bodyPosB, result);
        result.normalPad.w = asfloat(bestLocalTri);
    }
}

void nwbPhysicsCollidePlaneConvexPair(
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
    const bool planeIsA = (proxyA.bodyIndexTypeShape.y == NWB_PHYSICS_SHAPE_TYPE_PLANE);
    const NwbPhysicsGpuShapeProxy planeP = planeIsA ? proxyA : proxyB;
    const NwbPhysicsGpuShapeProxy otherP = planeIsA ? proxyB : proxyA;
    const float3 planeOrig = planeIsA ? originA : originB;
    const float4 planeRot = planeIsA ? rotA : rotB;
    const float3 otherOrig = planeIsA ? originB : originA;
    const float4 otherRot = planeIsA ? rotB : rotA;
    const uint otherType = otherP.bodyIndexTypeShape.y;
    if(otherType == NWB_PHYSICS_SHAPE_TYPE_PLANE || otherType == NWB_PHYSICS_SHAPE_TYPE_MESH || otherType == NWB_PHYSICS_SHAPE_TYPE_HEIGHTFIELD){
        result.pairInfo.w = 1u;
        return;
    }

    const float4 planeWorldQ = nwbPhysicsQuatNormalize(nwbPhysicsQuatMul(planeRot, planeP.localRotationOrMeta));
    const float3 pPlaneW = planeOrig + nwbPhysicsQuatRotate(planeP.localCenterRadius.xyz, planeRot);
    float3 t1W;
    float3 t2W;
    float3 nPlaneW;
    nwbPhysicsQuatToAxes(planeWorldQ, t1W, t2W, nPlaneW);

    const float otherR = otherP.localCenterRadius.w;
    const float3 wNormal = planeIsA ? nPlaneW : -nPlaneW;
    const bool isFinitePlane = (planeP.localExtentOrP1.x > 0.0f) && (planeP.localExtentOrP1.z > 0.0f);
    const float limX = planeP.localExtentOrP1.x - 1.0e-3f;
    const float limZ = planeP.localExtentOrP1.z - 1.0e-3f;
    const bool otherIsBox = (otherType == NWB_PHYSICS_SHAPE_TYPE_HULL) && ((otherP.bodyIndexTypeShape.w & NWB_PHYSICS_SHAPE_FLAG_IS_BOX) != 0u);
    if(otherType == NWB_PHYSICS_SHAPE_TYPE_SPHERE){
        const float3 cO = otherOrig + nwbPhysicsQuatRotate(otherP.localCenterRadius.xyz, otherRot);
        const float sep = dot(cO - pPlaneW, nPlaneW) - otherR;
        if(sep > maxDist)
            return;
        if(isFinitePlane){
            const float3 rel = cO - pPlaneW;
            const float cT1 = dot(rel, t1W);
            const float cT2 = dot(rel, t2W);
            if((cT1 - otherR < -limX) || (cT1 + otherR > limX) || (cT2 - otherR < -limZ) || (cT2 + otherR > limZ)){
                result.pairInfo.w = 1u;
                return;
            }
        }
        const float3 wO = cO - nPlaneW * otherR;
        const float3 wPl = wO - nPlaneW * sep;
        result.pairInfo.z = 1u;
        result.normalPad = float4(wNormal, asfloat(NWB_PHYSICS_INVALID_INDEX));
        result.anchorAFeature[0] = float4((planeIsA ? wPl : wO) - bodyPosA, asfloat(nwbPhysicsMakeFeatureId(8u, 0u, 2u, 0u)));
        result.anchorBSeparation[0] = float4((planeIsA ? wO : wPl) - bodyPosB, sep);
        return;
    }

    float3 candWA[NWB_PHYSICS_MAX_MANIFOLD_POINTS];
    float3 candWB[NWB_PHYSICS_MAX_MANIFOLD_POINTS];
    float3 candNorm[NWB_PHYSICS_MAX_MANIFOLD_POINTS];
    float candSep[NWB_PHYSICS_MAX_MANIFOLD_POINTS];
    uint candFeat[NWB_PHYSICS_MAX_MANIFOLD_POINTS];
    [loop] for(uint kInit = 0u; kInit < NWB_PHYSICS_MAX_MANIFOLD_POINTS; ++kInit){
        candWA[kInit] = float3(0.0f, 0.0f, 0.0f);
        candWB[kInit] = float3(0.0f, 0.0f, 0.0f);
        candNorm[kInit] = float3(0.0f, 1.0f, 0.0f);
        candSep[kInit] = 0.0f;
        candFeat[kInit] = 0u;
    }
    uint candCount = 0u;

    if(otherIsBox){
        const float4 qBox = (otherP.localRotationOrMeta.w >= 0.999999f)
            ? otherRot
            : nwbPhysicsQuatNormalize(nwbPhysicsQuatMul(otherRot, otherP.localRotationOrMeta));
        const float3 cBox = otherOrig + nwbPhysicsQuatRotate(otherP.localCenterRadius.xyz, otherRot);
        float3 bx;
        float3 by;
        float3 bz;
        nwbPhysicsQuatToAxes(qBox, bx, by, bz);
        const float3 h = otherP.localExtentOrP1.xyz;
        const float3 relC = cBox - pPlaneW;
        const float3 nInBox = float3(dot(bx, nPlaneW), dot(by, nPlaneW), dot(bz, nPlaneW));
        const float centerDist = dot(relC, nPlaneW);
        const float minSep = centerDist - dot(abs(nInBox), h) - otherR;
        if(minSep > maxDist)
            return;
        if(isFinitePlane){
            const float3 t1InBox = float3(dot(bx, t1W), dot(by, t1W), dot(bz, t1W));
            const float3 t2InBox = float3(dot(bx, t2W), dot(by, t2W), dot(bz, t2W));
            const float cT1 = dot(relC, t1W);
            const float eT1 = dot(abs(t1InBox), h) + otherR;
            const float cT2 = dot(relC, t2W);
            const float eT2 = dot(abs(t2InBox), h) + otherR;
            if((cT1 - eT1 < -limX) || (cT1 + eT1 > limX) || (cT2 - eT2 < -limZ) || (cT2 + eT2 > limZ)){
                result.pairInfo.w = 1u;
                return;
            }
        }
        [loop] for(uint k = 0u; k < 8u; ++k){
            const float sx = ((k & 1u) != 0u) ? h.x : -h.x;
            const float sy = ((k & 2u) != 0u) ? h.y : -h.y;
            const float sz = ((k & 4u) != 0u) ? h.z : -h.z;
            const float sep = (centerDist + nInBox.x * sx + nInBox.y * sy + nInBox.z * sz) - otherR;
            if(sep <= maxDist){
                const float3 wPt = cBox + bx * sx + by * sy + bz * sz;
                const float3 wO = wPt - nPlaneW * otherR;
                const float3 wPl = wO - nPlaneW * sep;
                nwbPhysicsPushMeshCand(planeIsA ? wPl : wO, planeIsA ? wO : wPl, wNormal, sep, nwbPhysicsMakeFeatureId(8u, 0u, 4u, k), candWA, candWB, candNorm, candSep, candFeat, candCount);
            }
        }
    }
    else if(otherType == NWB_PHYSICS_SHAPE_TYPE_HULL){
        const uint vOffset = otherP.filterData.w;
        const uint metaPack = asuint(otherP.localRotationOrMeta.x);
        const uint vCount = min(metaPack & 0xffu, NWB_PHYSICS_MAX_HULL_VERTICES);
        if(vCount == 0u || vOffset == NWB_PHYSICS_INVALID_INDEX){
            result.pairInfo.w = 1u;
            return;
        }
        float3 hAxX;
        float3 hAxY;
        float3 hAxZ;
        nwbPhysicsQuatToAxes(otherRot, hAxX, hAxY, hAxZ);
        const float3 relH = otherOrig - pPlaneW;
        const float3 nPlaneLocal = float3(dot(hAxX, nPlaneW), dot(hAxY, nPlaneW), dot(hAxZ, nPlaneW));
        const float baseN = dot(relH, nPlaneW) - otherR;
        float3 t1Local = float3(0.0f, 0.0f, 0.0f);
        float3 t2Local = float3(0.0f, 0.0f, 0.0f);
        float baseT1 = 0.0f;
        float baseT2 = 0.0f;
        if(isFinitePlane){
            t1Local = float3(dot(hAxX, t1W), dot(hAxY, t1W), dot(hAxZ, t1W));
            t2Local = float3(dot(hAxX, t2W), dot(hAxY, t2W), dot(hAxZ, t2W));
            baseT1 = dot(relH, t1W);
            baseT2 = dot(relH, t2W);
        }
        float minSep = 1.0e20f;
        bool outOfFiniteBounds = false;
        [loop] for(uint k = 0u; k < vCount; ++k){
            const float3 vL = g_NwbPhysicsFloat4Buffers[hullVertexSlot][vOffset + k].xyz;
            const float sep = baseN + dot(nPlaneLocal, vL);
            minSep = min(minSep, sep);
            if(isFinitePlane){
                const float u1 = baseT1 + dot(t1Local, vL);
                const float u2 = baseT2 + dot(t2Local, vL);
                if((u1 - otherR < -limX) || (u1 + otherR > limX) || (u2 - otherR < -limZ) || (u2 + otherR > limZ))
                    outOfFiniteBounds = true;
            }
            if(sep <= maxDist){
                const float3 wPt = otherOrig + hAxX * vL.x + hAxY * vL.y + hAxZ * vL.z;
                const float3 wO = wPt - nPlaneW * otherR;
                const float3 wPl = wO - nPlaneW * sep;
                nwbPhysicsPushMeshCand(planeIsA ? wPl : wO, planeIsA ? wO : wPl, wNormal, sep, nwbPhysicsMakeFeatureId(8u, 0u, 4u, k & 0xffu), candWA, candWB, candNorm, candSep, candFeat, candCount);
            }
        }
        if(minSep > maxDist)
            return;
        if(outOfFiniteBounds){
            result.pairInfo.w = 1u;
            return;
        }
    }
    else{
        const NwbPhysicsGpuSupportCache otherCache = nwbPhysicsBuildSupportCache(otherP, otherOrig, otherRot);
        uint supIdx = 0u;
        const float3 coreDeep = nwbPhysicsSupportCached(otherCache, hullVertexSlot, -nPlaneW, supIdx);
        const float minSep = dot(coreDeep - pPlaneW, nPlaneW) - otherR;
        if(minSep > maxDist)
            return;

        if(isFinitePlane){
            float minT1 = 0.0f;
            float maxT1 = 0.0f;
            float minT2 = 0.0f;
            float maxT2 = 0.0f;
            nwbPhysicsSupportMinMaxCached(otherCache, hullVertexSlot, t1W, minT1, maxT1);
            nwbPhysicsSupportMinMaxCached(otherCache, hullVertexSlot, t2W, minT2, maxT2);
            const float pT1 = dot(pPlaneW, t1W);
            const float pT2 = dot(pPlaneW, t2W);
            if((minT1 - otherR < pT1 - limX) || (maxT1 + otherR > pT1 + limX) || (minT2 - otherR < pT2 - limZ) || (maxT2 + otherR > pT2 + limZ)){
                result.pairInfo.w = 1u;
                return;
            }
        }

        if(otherType == NWB_PHYSICS_SHAPE_TYPE_CAPSULE || otherType == NWB_PHYSICS_SHAPE_TYPE_SEGMENT){
            const float3 pts[2] = { otherCache.p0OrCenter, otherCache.p1OrExtent };
            [loop] for(uint k = 0u; k < 2u; ++k){
                const float sep = dot(pts[k] - pPlaneW, nPlaneW) - otherR;
                if(sep <= maxDist){
                    const float3 wO = pts[k] - nPlaneW * otherR;
                    const float3 wPl = wO - nPlaneW * sep;
                    nwbPhysicsPushMeshCand(planeIsA ? wPl : wO, planeIsA ? wO : wPl, wNormal, sep, nwbPhysicsMakeFeatureId(8u, 0u, otherType & 0xfu, k), candWA, candWB, candNorm, candSep, candFeat, candCount);
                }
            }
        }
        else if(otherType == NWB_PHYSICS_SHAPE_TYPE_TAPERED_CAPSULE){
            const float3 pts[2] = { otherCache.p0OrCenter, otherCache.p1OrExtent };
            const float effRs[2] = { otherR + otherCache.extraParams.x, otherR + otherCache.extraParams.y };
            [loop] for(uint k = 0u; k < 2u; ++k){
                const float sep = dot(pts[k] - pPlaneW, nPlaneW) - effRs[k];
                if(sep <= maxDist){
                    const float3 wO = pts[k] - nPlaneW * effRs[k];
                    const float3 wPl = wO - nPlaneW * sep;
                    nwbPhysicsPushMeshCand(planeIsA ? wPl : wO, planeIsA ? wO : wPl, wNormal, sep, nwbPhysicsMakeFeatureId(8u, 0u, otherType & 0xfu, k), candWA, candWB, candNorm, candSep, candFeat, candCount);
                }
            }
        }
        else if(otherType == NWB_PHYSICS_SHAPE_TYPE_CYLINDER || otherType == NWB_PHYSICS_SHAPE_TYPE_CONE){
            float4 patchVerts[NWB_PHYSICS_GPU_MAX_CLIP_POINTS];
            float3 patchN = -nPlaneW;
            const uint patchCnt = nwbPhysicsBuildProxyPlanarPatchWorld(otherP, otherOrig, otherRot, hullVertexSlot, -nPlaneW, patchVerts, patchN);
            if(patchCnt >= 2u){
                [loop] for(uint k = 0u; k < patchCnt; ++k){
                    const float sep = dot(patchVerts[k].xyz - pPlaneW, nPlaneW) - otherR;
                    if(sep <= maxDist){
                        const float3 wO = patchVerts[k].xyz - nPlaneW * otherR;
                        const float3 wPl = wO - nPlaneW * sep;
                        nwbPhysicsPushMeshCand(planeIsA ? wPl : wO, planeIsA ? wO : wPl, wNormal, sep, nwbPhysicsMakeFeatureId(8u, 0u, otherType & 0xfu, k & 0xffu), candWA, candWB, candNorm, candSep, candFeat, candCount);
                    }
                }
            }
            else{
                const float3 wO = coreDeep - nPlaneW * otherR;
                const float3 wPl = wO - nPlaneW * minSep;
                nwbPhysicsPushMeshCand(planeIsA ? wPl : wO, planeIsA ? wO : wPl, wNormal, minSep, nwbPhysicsMakeFeatureId(8u, 0u, otherType & 0xfu, 0u), candWA, candWB, candNorm, candSep, candFeat, candCount);
            }
        }
    }

    if(candCount > 0u){
        nwbPhysicsFinalizeMeshCand(candWA, candWB, candNorm, candSep, candFeat, candCount, wNormal, bodyPosA, bodyPosB, result);
        result.normalPad.w = asfloat(NWB_PHYSICS_INVALID_INDEX);
    }
}

void nwbPhysicsCollideHeightfieldPair(
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
    const bool hfIsA = (proxyA.bodyIndexTypeShape.y == NWB_PHYSICS_SHAPE_TYPE_HEIGHTFIELD);
    const NwbPhysicsGpuShapeProxy hfP = hfIsA ? proxyA : proxyB;
    const NwbPhysicsGpuShapeProxy otherP = hfIsA ? proxyB : proxyA;
    const float3 hfOrig = hfIsA ? originA : originB;
    const float4 hfRot = hfIsA ? rotA : rotB;
    const float3 otherOrig = hfIsA ? originB : originA;
    const float4 otherRot = hfIsA ? rotB : rotA;
    const uint otherType = otherP.bodyIndexTypeShape.y;
    if(
        otherType == NWB_PHYSICS_SHAPE_TYPE_HEIGHTFIELD
        || otherType == NWB_PHYSICS_SHAPE_TYPE_MESH
        || otherType == NWB_PHYSICS_SHAPE_TYPE_PLANE
    ){
        result.pairInfo.w = 1u;
        return;
    }

    const uint hfBase = hfP.filterData.w;
    NwbPhysicsHfHeader hf;
    if(hullVertexSlot == NWB_PHYSICS_INVALID_INDEX || hfBase == NWB_PHYSICS_INVALID_INDEX || !nwbPhysicsHfLoadHeader(hullVertexSlot, hfBase, hf)){
        result.pairInfo.w = 1u;
        return;
    }
    const float3 hfLocalOrigin = hf.origin;
    const float hfMaxY = hf.maxY;
    const float sx = hf.sx;
    const float sy = hf.sy;
    const float sz = hf.sz;
    const float hfMinY = hf.minY;
    const uint cols = hf.cols;
    const uint rows = hf.rows;
    const uint hfFlags = hf.flags;
    const float hfThickness = hf.thickness;

    const float otherR = otherP.localCenterRadius.w;
    const float totalR = otherR + hfThickness;
    const float queryR = totalR + maxDist;

    const float4 hfInvRot = float4(-hfRot.xyz, hfRot.w);
    const float3 relOrig = nwbPhysicsQuatRotate(otherOrig - hfOrig, hfInvRot);
    const float4 relRot = nwbPhysicsQuatNormalize(nwbPhysicsQuatMul(hfInvRot, otherRot));
    const NwbPhysicsGpuSupportCache otherCache = nwbPhysicsBuildSupportCache(otherP, relOrig, relRot);

    float minX = 0.0f;
    float maxX = 0.0f;
    float minY = 0.0f;
    float maxY = 0.0f;
    float minZ = 0.0f;
    float maxZ = 0.0f;
    nwbPhysicsSupportMinMaxCached(otherCache, hullVertexSlot, float3(1.0f, 0.0f, 0.0f), minX, maxX);
    nwbPhysicsSupportMinMaxCached(otherCache, hullVertexSlot, float3(0.0f, 1.0f, 0.0f), minY, maxY);
    nwbPhysicsSupportMinMaxCached(otherCache, hullVertexSlot, float3(0.0f, 0.0f, 1.0f), minZ, maxZ);
    const float3 qMin = float3(minX, minY, minZ) - float3(queryR, queryR, queryR);
    const float3 qMax = float3(maxX, maxY, maxZ) + float3(queryR, queryR, queryR);

    if(qMax.y < hfMinY - hfThickness - 1.0e-4f || qMin.y > hfMaxY + hfThickness + 1.0e-4f)
        return;

    const int maxCellX = int(cols - 2u);
    const int maxCellZ = int(rows - 2u);
    const int rawCx0 = int(floor((qMin.x - hfLocalOrigin.x) / sx));
    const int rawCx1 = int(floor((qMax.x - hfLocalOrigin.x) / sx));
    const int rawCz0 = int(floor((qMin.z - hfLocalOrigin.z) / sz));
    const int rawCz1 = int(floor((qMax.z - hfLocalOrigin.z) / sz));
    if(rawCx1 < 0 || rawCx0 > maxCellX || rawCz1 < 0 || rawCz0 > maxCellZ)
        return;
    const uint cx0 = uint(max(rawCx0, 0));
    const uint cx1 = uint(min(rawCx1, maxCellX));
    const uint cz0 = uint(max(rawCz0, 0));
    const uint cz1 = uint(min(rawCz1, maxCellZ));
    if(cx0 > cx1 || cz0 > cz1)
        return;
    const uint cellSpanCount = (cx1 - cx0 + 1u) * (cz1 - cz0 + 1u);
    if(cellSpanCount > 256u){
        result.pairInfo.w = 1u;
        return;
    }

    float3 candWA[NWB_PHYSICS_MAX_MANIFOLD_POINTS];
    float3 candWB[NWB_PHYSICS_MAX_MANIFOLD_POINTS];
    float3 candNorm[NWB_PHYSICS_MAX_MANIFOLD_POINTS];
    float candSep[NWB_PHYSICS_MAX_MANIFOLD_POINTS];
    uint candFeat[NWB_PHYSICS_MAX_MANIFOLD_POINTS];
    [loop] for(uint kInit = 0u; kInit < NWB_PHYSICS_MAX_MANIFOLD_POINTS; ++kInit){
        candWA[kInit] = float3(0.0f, 0.0f, 0.0f);
        candWB[kInit] = float3(0.0f, 0.0f, 0.0f);
        candNorm[kInit] = float3(0.0f, 1.0f, 0.0f);
        candSep[kInit] = 0.0f;
        candFeat[kInit] = 0u;
    }
    uint candCount = 0u;
    float bestSep = 1.0e20f;
    float3 chosenWorldN = float3(0.0f, -1.0f, 0.0f);
    uint bestCellIdx = NWB_PHYSICS_INVALID_INDEX;

    const bool flipDiag = (hfFlags & NWB_PHYSICS_HF_FLAG_FLIP_DIAGONAL) != 0u;
    const float3 zeroVec = float3(0.0f, 0.0f, 0.0f);

    [loop] for(uint cz = cz0; cz <= cz1; ++cz){
        [loop] for(uint cx = cx0; cx <= cx1; ++cx){
            float3 v00 = zeroVec;
            float3 v10 = zeroVec;
            float3 v01 = zeroVec;
            float3 v11 = zeroVec;
            if(!nwbPhysicsHfLoadCellVertices(hullVertexSlot, hfBase, cols, rows, hfLocalOrigin, sx, sz, sy, hfFlags, cx, cz, v00, v10, v01, v11))
                continue;
            const float cellMinY = min(min(v00.y, v10.y), min(v01.y, v11.y));
            const float cellMaxY = max(max(v00.y, v10.y), max(v01.y, v11.y));
            if(qMax.y < cellMinY - hfThickness || qMin.y > cellMaxY + hfThickness)
                continue;

            const uint cellIdx = cz * (cols - 1u) + cx;
            [loop] for(uint ti = 0u; ti < 2u; ++ti){
                float3 tv0 = zeroVec;
                float3 tv1 = zeroVec;
                float3 tv2 = zeroVec;
                nwbPhysicsHfExtractTriangle(flipDiag, ti, v00, v10, v01, v11, tv0, tv1, tv2);
                const float3 tMin = min(tv0, min(tv1, tv2));
                const float3 tMax = max(tv0, max(tv1, tv2));
                if(!nwbPhysicsAabbOverlaps(tMin, tMax, qMin, qMax))
                    continue;

                const float3 triN = nwbPhysicsHfTriNormal(tv0, tv1, tv2);
                nwbPhysicsCollideConvexWithSurfaceTriangle(
                    otherP,
                    otherCache,
                    relOrig,
                    relRot,
                    hfOrig,
                    hfRot,
                    hfIsA,
                    otherR,
                    hfThickness,
                    hullVertexSlot,
                    0u,
                    true,
                    hfBase,
                    cols,
                    rows,
                    hfLocalOrigin,
                    sx,
                    sz,
                    sy,
                    hfFlags,
                    cx,
                    cz,
                    ti,
                    v00,
                    v10,
                    v01,
                    v11,
                    tv0,
                    tv1,
                    tv2,
                    triN,
                    triN,
                    true,
                    true,
                    true,
                    true,
                    maxDist,
                    7u,
                    ti,
                    4u,
                    cellIdx,
                    bestSep,
                    chosenWorldN,
                    bestCellIdx,
                    candWA,
                    candWB,
                    candNorm,
                    candSep,
                    candFeat,
                    candCount
                );
            }
        }
    }

    if(candCount > 0u){
        nwbPhysicsFinalizeMeshCand(candWA, candWB, candNorm, candSep, candFeat, candCount, chosenWorldN, bodyPosA, bodyPosB, result);
        result.normalPad.w = asfloat(bestCellIdx);
    }
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

void nwbPhysicsEvalBulletShapePose(
    const NwbPhysicsGpuBodySim body,
    const NwbPhysicsGpuShapeProxy proxy,
    const float t,
    out float3 outOrigin,
    out float4 outRot
){
    float3 bOrigin = float3(0.0f, 0.0f, 0.0f);
    float4 bRot = float4(0.0f, 0.0f, 0.0f, 1.0f);
    nwbPhysicsEvalBulletSweep(body, t, bOrigin, bRot);
    outOrigin = bOrigin;
    outRot = bRot;
    nwbPhysicsResolveShapeWorldPose(proxy, bOrigin, bRot, outOrigin, outRot);
}

float nwbPhysicsQuatAngle(const float4 q0, const float4 q1){
    const float d = abs(clamp(dot(q0, q1), -1.0f, 1.0f));
    return 2.0f * acos(d);
}

void nwbPhysicsComputeBulletSweepLocalAabb(
    const NwbPhysicsGpuBodySim bodyA,
    const NwbPhysicsGpuBodySim bodyB,
    const NwbPhysicsGpuShapeProxy proxyB,
    const float3 staticOB,
    const float4 staticRB,
    const bool bMoved,
    const float totalRad,
    const float linearSlop,
    out float3 outQueryMin,
    out float3 outQueryMax
){
    outQueryMin = float3(1.0e20f, 1.0e20f, 1.0e20f);
    outQueryMax = float3(-1.0e20f, -1.0e20f, -1.0e20f);
    float boundR = bodyA.forceLinearDamping.w + totalRad + linearSlop;
    if(!bMoved){
        const float4 staticInvRB = float4(-staticRB.xyz, staticRB.w);
        const float3 cA0InB = nwbPhysicsQuatRotate(bodyA.forceLinearDamping.xyz - staticOB, staticInvRB);
        const float3 cA1InB = nwbPhysicsQuatRotate(bodyA.positionInvMass.xyz - staticOB, staticInvRB);
        outQueryMin = min(cA0InB, cA1InB);
        outQueryMax = max(cA0InB, cA1InB);
    }
    else{
        const float sampleTs[3] = { 0.0f, 0.5f, 1.0f };
        float maxRelDist = 0.0f;
        for(uint si = 0u; si < 3u; ++si){
            const float st = sampleTs[si];
            float3 oBt = staticOB;
            float4 rBt = staticRB;
            nwbPhysicsEvalBulletShapePose(bodyB, proxyB, st, oBt, rBt);
            const float3 cAt = lerp(bodyA.forceLinearDamping.xyz, bodyA.positionInvMass.xyz, st);
            const float3 cBt = lerp(bodyB.forceLinearDamping.xyz, bodyB.positionInvMass.xyz, st);
            maxRelDist = max(maxRelDist, length(cAt - cBt));
            const float4 bInvRot = float4(-rBt.xyz, rBt.w);
            const float3 cAInB = nwbPhysicsQuatRotate(cAt - oBt, bInvRot);
            outQueryMin = min(outQueryMin, cAInB);
            outQueryMax = max(outQueryMax, cAInB);
        }
        const float thetaB = nwbPhysicsQuatAngle(bodyB.torqueAngularDamping, bodyB.rotation);
        boundR += min(0.5f * thetaB, 2.0f) * maxRelDist;
    }
    outQueryMin -= float3(boundR, boundR, boundR);
    outQueryMax += float3(boundR, boundR, boundR);
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
    if(
        proxyA.bodyIndexTypeShape.y == NWB_PHYSICS_SHAPE_TYPE_HEIGHTFIELD
        || proxyA.bodyIndexTypeShape.y == NWB_PHYSICS_SHAPE_TYPE_PLANE
        || proxyA.bodyIndexTypeShape.y == NWB_PHYSICS_SHAPE_TYPE_MESH
        || proxyB.bodyIndexTypeShape.y == NWB_PHYSICS_SHAPE_TYPE_PLANE
    ){
        result.pairInfo.w = 2u;
        g_NwbPhysicsNarrowphaseManifoldBuffers[manifoldSlot][pairIdx] = result;
        return;
    }
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
        const uint triBase = asuint(proxyB.localRotationOrMeta.x);
        if(nodeBase == NWB_PHYSICS_INVALID_INDEX || triBase == NWB_PHYSICS_INVALID_INDEX){
            result.pairInfo.w = 2u;
            g_NwbPhysicsNarrowphaseManifoldBuffers[manifoldSlot][pairIdx] = result;
            return;
        }
        const bool bMoved = any(bodyB.forceLinearDamping.xyz != bodyB.positionInvMass.xyz) || any(bodyB.torqueAngularDamping != bodyB.rotation);
        float3 staticOB = float3(0.0f, 0.0f, 0.0f);
        float4 staticRB = float4(0.0f, 0.0f, 0.0f, 1.0f);
        nwbPhysicsEvalBulletShapePose(bodyB, proxyB, 0.0f, staticOB, staticRB);
        const float4 firstTri1 = g_NwbPhysicsFloat4Buffers[meshTriSlot][triBase * 3u + 1u];
        if((asuint(firstTri1.w) & NWB_PHYSICS_MESH_FLAG_CLOSED) != 0u){
            float3 oA0 = float3(0.0f, 0.0f, 0.0f);
            float4 rA0 = float4(0.0f, 0.0f, 0.0f, 1.0f);
            nwbPhysicsEvalBulletShapePose(bodyA, proxyA, 0.0f, oA0, rA0);
            const float3 startCenterA = oA0 + nwbPhysicsQuatRotate(proxyA.localCenterRadius.xyz, rA0);
            const float3 startCenterInB = nwbPhysicsQuatInvRotate(startCenterA - staticOB, staticRB);
            bool insideOverflow = false;
            const bool insideClosed = nwbPhysicsIsPointInsideClosedMesh(nodeBase, triBase, meshNodeSlot, meshTriSlot, startCenterInB, insideOverflow);
            if(insideOverflow){
                result.pairInfo.w = 2u;
                g_NwbPhysicsNarrowphaseManifoldBuffers[manifoldSlot][pairIdx] = result;
                return;
            }
            if(insideClosed){
                result.pairInfo.w = 1u;
                g_NwbPhysicsNarrowphaseManifoldBuffers[manifoldSlot][pairIdx] = result;
                return;
            }
        }

        float3 queryMin;
        float3 queryMax;
        nwbPhysicsComputeBulletSweepLocalAabb(bodyA, bodyB, proxyB, staticOB, staticRB, bMoved, totalRadius, linearSlop, queryMin, queryMax);

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

                    float3 staticWV0 = float3(0.0f, 0.0f, 0.0f);
                    float3 staticWV1 = float3(0.0f, 0.0f, 0.0f);
                    float3 staticWV2 = float3(0.0f, 0.0f, 0.0f);
                    if(!bMoved){
                        staticWV0 = staticOB + nwbPhysicsQuatRotate(tv0, staticRB);
                        staticWV1 = staticOB + nwbPhysicsQuatRotate(tv1, staticRB);
                        staticWV2 = staticOB + nwbPhysicsQuatRotate(tv2, staticRB);
                    }

                    float t1 = 0.0f;
                    bool atMax = false;
                    for(uint iter = 0u; iter <= NWB_PHYSICS_TOI_MAX_ITERATIONS; ++iter){
                        float3 oAt;
                        float4 rAt;
                        nwbPhysicsEvalBulletShapePose(bodyA, proxyA, t1, oAt, rAt);
                        float3 oBt = staticOB;
                        float4 rBt = staticRB;
                        float3 wV0 = staticWV0;
                        float3 wV1 = staticWV1;
                        float3 wV2 = staticWV2;
                        if(bMoved){
                            nwbPhysicsEvalBulletShapePose(bodyB, proxyB, t1, oBt, rBt);
                            wV0 = oBt + nwbPhysicsQuatRotate(tv0, rBt);
                            wV1 = oBt + nwbPhysicsQuatRotate(tv1, rBt);
                            wV2 = oBt + nwbPhysicsQuatRotate(tv2, rBt);
                        }
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
                                result.anchorAFeature[0] = float4((wA + wB) * 0.5f, asfloat(firstTri + ti));
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
    else if(proxyB.bodyIndexTypeShape.y == NWB_PHYSICS_SHAPE_TYPE_HEIGHTFIELD){
        const uint hfBase = proxyB.filterData.w;
        NwbPhysicsHfHeader hf;
        if(hullVertexSlot == NWB_PHYSICS_INVALID_INDEX || hfBase == NWB_PHYSICS_INVALID_INDEX || !nwbPhysicsHfLoadHeader(hullVertexSlot, hfBase, hf)){
            result.pairInfo.w = 2u;
            g_NwbPhysicsNarrowphaseManifoldBuffers[manifoldSlot][pairIdx] = result;
            return;
        }
        const float3 hfLocalOrigin = hf.origin;
        const float hfMaxY = hf.maxY;
        const float sx = hf.sx;
        const float sy = hf.sy;
        const float sz = hf.sz;
        const float hfMinY = hf.minY;
        const uint cols = hf.cols;
        const uint rows = hf.rows;
        const uint hfFlags = hf.flags;
        const float hfThickness = hf.thickness;

        const float hfTotalRadius = rA + hfThickness;
        const float hfTargetDist = hfTotalRadius + 0.25f * linearSlop;
        const bool bMoved = any(bodyB.forceLinearDamping.xyz != bodyB.positionInvMass.xyz) || any(bodyB.torqueAngularDamping != bodyB.rotation);
        float3 staticOB = float3(0.0f, 0.0f, 0.0f);
        float4 staticRB = float4(0.0f, 0.0f, 0.0f, 1.0f);
        nwbPhysicsEvalBulletShapePose(bodyB, proxyB, 0.0f, staticOB, staticRB);

        float3 queryMin;
        float3 queryMax;
        nwbPhysicsComputeBulletSweepLocalAabb(bodyA, bodyB, proxyB, staticOB, staticRB, bMoved, hfTotalRadius, linearSlop, queryMin, queryMax);

        if(queryMax.y < hfMinY - hfThickness - 1.0e-4f || queryMin.y > hfMaxY + hfThickness + 1.0e-4f){
            g_NwbPhysicsNarrowphaseManifoldBuffers[manifoldSlot][pairIdx] = result;
            return;
        }

        const int maxCellX = int(cols - 2u);
        const int maxCellZ = int(rows - 2u);
        const int rawCx0 = int(floor((queryMin.x - hfLocalOrigin.x) / sx));
        const int rawCx1 = int(floor((queryMax.x - hfLocalOrigin.x) / sx));
        const int rawCz0 = int(floor((queryMin.z - hfLocalOrigin.z) / sz));
        const int rawCz1 = int(floor((queryMax.z - hfLocalOrigin.z) / sz));
        if(rawCx1 < 0 || rawCx0 > maxCellX || rawCz1 < 0 || rawCz0 > maxCellZ){
            g_NwbPhysicsNarrowphaseManifoldBuffers[manifoldSlot][pairIdx] = result;
            return;
        }
        const uint cx0 = uint(max(rawCx0, 0));
        const uint cx1 = uint(min(rawCx1, maxCellX));
        const uint cz0 = uint(max(rawCz0, 0));
        const uint cz1 = uint(min(rawCz1, maxCellZ));
        if(cx0 > cx1 || cz0 > cz1){
            g_NwbPhysicsNarrowphaseManifoldBuffers[manifoldSlot][pairIdx] = result;
            return;
        }
        if((cx1 - cx0 + 1u) * (cz1 - cz0 + 1u) > 256u){
            result.pairInfo.w = 2u;
            g_NwbPhysicsNarrowphaseManifoldBuffers[manifoldSlot][pairIdx] = result;
            return;
        }

        float curMax = 1.0f;
        bool hasHit = false;
        const bool flipDiag = (hfFlags & NWB_PHYSICS_HF_FLAG_FLIP_DIAGONAL) != 0u;
        const float3 zeroVec = float3(0.0f, 0.0f, 0.0f);
        for(uint cz = cz0; cz <= cz1; ++cz){
            for(uint cx = cx0; cx <= cx1; ++cx){
                float3 v00 = zeroVec;
                float3 v10 = zeroVec;
                float3 v01 = zeroVec;
                float3 v11 = zeroVec;
                if(!nwbPhysicsHfLoadCellVertices(hullVertexSlot, hfBase, cols, rows, hfLocalOrigin, sx, sz, sy, hfFlags, cx, cz, v00, v10, v01, v11))
                    continue;
                const float cellMinY = min(min(v00.y, v10.y), min(v01.y, v11.y));
                const float cellMaxY = max(max(v00.y, v10.y), max(v01.y, v11.y));
                if(queryMax.y < cellMinY - hfThickness || queryMin.y > cellMaxY + hfThickness)
                    continue;

                const uint cellIdx = cz * (cols - 1u) + cx;
                for(uint ti = 0u; ti < 2u; ++ti){
                    float3 tv0 = zeroVec;
                    float3 tv1 = zeroVec;
                    float3 tv2 = zeroVec;
                    nwbPhysicsHfExtractTriangle(flipDiag, ti, v00, v10, v01, v11, tv0, tv1, tv2);
                    if(!nwbPhysicsAabbOverlaps(min(tv0, min(tv1, tv2)), max(tv0, max(tv1, tv2)), queryMin, queryMax))
                        continue;

                    float3 staticWV0 = zeroVec;
                    float3 staticWV1 = zeroVec;
                    float3 staticWV2 = zeroVec;
                    if(!bMoved){
                        staticWV0 = staticOB + nwbPhysicsQuatRotate(tv0, staticRB);
                        staticWV1 = staticOB + nwbPhysicsQuatRotate(tv1, staticRB);
                        staticWV2 = staticOB + nwbPhysicsQuatRotate(tv2, staticRB);
                    }

                    float t1 = 0.0f;
                    bool atMax = false;
                    for(uint iter = 0u; iter <= NWB_PHYSICS_TOI_MAX_ITERATIONS; ++iter){
                        float3 oAt = zeroVec;
                        float4 rAt = float4(0.0f, 0.0f, 0.0f, 1.0f);
                        nwbPhysicsEvalBulletShapePose(bodyA, proxyA, t1, oAt, rAt);
                        float3 oBt = staticOB;
                        float4 rBt = staticRB;
                        float3 wV0 = staticWV0;
                        float3 wV1 = staticWV1;
                        float3 wV2 = staticWV2;
                        if(bMoved){
                            nwbPhysicsEvalBulletShapePose(bodyB, proxyB, t1, oBt, rBt);
                            wV0 = oBt + nwbPhysicsQuatRotate(tv0, rBt);
                            wV1 = oBt + nwbPhysicsQuatRotate(tv1, rBt);
                            wV2 = oBt + nwbPhysicsQuatRotate(tv2, rBt);
                        }
                        float3 pA = zeroVec;
                        float3 pB = zeroVec;
                        float dist = 0.0f;
                        bool deep = false;
                        nwbPhysicsRunGjkDistance(proxyA, oAt, rAt, proxyB, oBt, rBt, true, wV0, wV1, wV2, hullVertexSlot, pA, pB, dist, deep);
                        if(deep)
                            dist = 0.0f;
                        if(dist <= hfTargetDist + tolerance){
                            if(t1 > 0.0f && (t1 < curMax || (!hasHit && t1 <= curMax))){
                                curMax = t1;
                                hasHit = true;
                                const float3 n = nwbPhysicsSafeNormal(pB - pA, float3(0.0f, 1.0f, 0.0f));
                                const float3 wA = pA + n * rA;
                                const float3 wB = pB - n * hfThickness;
                                result.pairInfo.z = 1u;
                                result.pairInfo.w = 0u;
                                result.normalPad = float4(n, t1);
                                result.anchorAFeature[0] = float4((wA + wB) * 0.5f, asfloat(cellIdx));
                            }
                            else if(t1 == 0.0f && (dist < hfTotalRadius - tolerance || (hfTotalRadius <= tolerance && dist <= tolerance))){
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
                        const float step = (dist - hfTargetDist) / approach;
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
        }
        g_NwbPhysicsNarrowphaseManifoldBuffers[manifoldSlot][pairIdx] = result;
        return;
    }

    float t1 = 0.0f;
    bool atMax = false;
    for(uint iter = 0u; iter <= NWB_PHYSICS_TOI_MAX_ITERATIONS; ++iter){
        float3 oAt;
        float4 rAt;
        nwbPhysicsEvalBulletShapePose(bodyA, proxyA, t1, oAt, rAt);
        float3 oBt;
        float4 rBt;
        nwbPhysicsEvalBulletShapePose(bodyB, proxyB, t1, oBt, rBt);
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
            result.anchorAFeature[0] = float4((wA + wB) * 0.5f, asfloat(NWB_PHYSICS_INVALID_INDEX));
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
    const uint activePairs = (counterSlot != NWB_PHYSICS_INVALID_INDEX)
        ? min(g_NwbPhysicsUintBuffers[counterSlot][0u], maxPairs)
        : maxPairs;
    if(idx >= activePairs)
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

        const float3 bodyOriginA = poseA.originPad.xyz;
        const float4 bodyRotA = poseA.rotation;
        const float3 bodyOriginB = poseB.originPad.xyz;
        const float4 bodyRotB = poseB.rotation;
        float3 originA = bodyOriginA;
        float4 rotA = bodyRotA;
        float3 originB = bodyOriginB;
        float4 rotB = bodyRotB;

        result.pairInfo = uint4(proxyA.bodyIndexTypeShape.z, proxyB.bodyIndexTypeShape.z, 0u, 0u);

#if NWB_PHYSICS_NP_PROFILE == 1
    nwbPhysicsResolveShapeWorldPose(proxyA, bodyOriginA, bodyRotA, originA, rotA);
    nwbPhysicsResolveShapeWorldPose(proxyB, bodyOriginB, bodyRotB, originB, rotB);
    if(typeA == NWB_PHYSICS_SHAPE_TYPE_HEIGHTFIELD || typeB == NWB_PHYSICS_SHAPE_TYPE_HEIGHTFIELD){
        nwbPhysicsCollideHeightfieldPair(proxyA, originA, rotA, bodyOriginA, proxyB, originB, rotB, bodyOriginB, hullVertexSlot, maxDist, result);
    }
    else if(typeA == NWB_PHYSICS_SHAPE_TYPE_PLANE || typeB == NWB_PHYSICS_SHAPE_TYPE_PLANE){
        nwbPhysicsCollidePlaneConvexPair(proxyA, originA, rotA, bodyOriginA, proxyB, originB, rotB, bodyOriginB, hullVertexSlot, maxDist, result);
    }
    else if(typeA == NWB_PHYSICS_SHAPE_TYPE_MESH || typeB == NWB_PHYSICS_SHAPE_TYPE_MESH){
        nwbPhysicsCollideMeshPair(proxyA, originA, rotA, bodyOriginA, proxyB, originB, rotB, bodyOriginB, meshNodeSlot, meshTriSlot, hullVertexSlot, maxDist, result);
    }
    else if(typeA >= NWB_PHYSICS_SHAPE_TYPE_CYLINDER || typeB >= NWB_PHYSICS_SHAPE_TYPE_CYLINDER){
        const float totalR = proxyA.localCenterRadius.w + proxyB.localCenterRadius.w;
        const NwbPhysicsGpuSupportCache cacheA = nwbPhysicsBuildSupportCache(proxyA, originA, rotA);
        const NwbPhysicsGpuSupportCache cacheB = nwbPhysicsBuildSupportCache(proxyB, originB, rotB);
        float3 pA = float3(0.0f, 0.0f, 0.0f);
        float3 pB = float3(0.0f, 0.0f, 0.0f);
        float dist = 0.0f;
        bool hitDeep = false;
        nwbPhysicsRunGjkDistanceCached(cacheA, false, float3(0.0f, 0.0f, 0.0f), float3(0.0f, 0.0f, 0.0f), float3(0.0f, 0.0f, 0.0f), cacheB, false, float3(0.0f, 0.0f, 0.0f), float3(0.0f, 0.0f, 0.0f), float3(0.0f, 0.0f, 0.0f), hullVertexSlot, pA, pB, dist, hitDeep);
        float sep = dist - totalR;
        float3 surfN = float3(0.0f, 1.0f, 0.0f);
        float3 ptA = pA;
        float3 ptB = pB;
        bool trySurface = false;
        if(hitDeep || dist <= 1.0e-5f){
            const float3 epaHintN = nwbPhysicsSafeNormal(originB - originA, float3(0.0f, 1.0f, 0.0f));
            float3 epaN = epaHintN;
            float3 epaPtA = originA;
            float3 epaPtB = originB;
            float epaDepth = 0.0f;
            if(!nwbPhysicsRunEpaCached(cacheA, false, float3(0.0f, 0.0f, 0.0f), float3(0.0f, 0.0f, 0.0f), float3(0.0f, 0.0f, 0.0f), cacheB, false, float3(0.0f, 0.0f, 0.0f), float3(0.0f, 0.0f, 0.0f), float3(0.0f, 0.0f, 0.0f), hullVertexSlot, epaHintN, epaN, epaPtA, epaPtB, epaDepth)){
                result.pairInfo.w = 1u;
            }
            else{
                sep = -epaDepth - totalR;
                surfN = epaN;
                ptA = epaPtA;
                ptB = epaPtB;
                trySurface = (sep <= maxDist);
            }
        }
        else if(sep <= maxDist){
            surfN = (pB - pA) / dist;
            trySurface = true;
        }
        if(trySurface){
            if(!nwbPhysicsBuildPlanarManifold(proxyA, originA, rotA, bodyOriginA, proxyB, originB, rotB, bodyOriginB, hullVertexSlot, surfN, maxDist, result)){
                result.pairInfo.z = 1u;
                result.normalPad = float4(surfN, asfloat(NWB_PHYSICS_INVALID_INDEX));
                result.anchorAFeature[0] = float4(ptA + surfN * proxyA.localCenterRadius.w - bodyOriginA, asfloat(20u));
                result.anchorBSeparation[0] = float4(ptB - surfN * proxyB.localCenterRadius.w - bodyOriginB, sep);
            }
        }
    }
    else if(typeA == NWB_PHYSICS_SHAPE_TYPE_HULL && typeB == NWB_PHYSICS_SHAPE_TYPE_HULL){
        nwbPhysicsSolveHullHullDeep(proxyA, originA, rotA, bodyOriginA, proxyB, originB, rotB, bodyOriginB, hullVertexSlot, maxDist, result);
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
                nwbPhysicsSolveSphereHullDeep(proxyA, originA, rotA, bodyOriginA, proxyB, originB, rotB, bodyOriginB, hullVertexSlot, maxDist, result);
            }
            else{
                const float sep = dist - totalR;
                if(sep <= maxDist){
                    const float3 n = (pB - pA) / dist;
                    result.pairInfo.z = 1u;
                    result.normalPad = float4(n, 0.0f);
                    result.anchorAFeature[0] = float4(pA + n * proxyA.localCenterRadius.w - bodyOriginA, asfloat(20u));
                    result.anchorBSeparation[0] = float4(pB - n * proxyB.localCenterRadius.w - bodyOriginB, sep);
                }
            }
        }
        else{
            if(hitDeep || dist <= 1.0e-5f){
                nwbPhysicsSolveCapsuleHullDeep(proxyA, originA, rotA, bodyOriginA, proxyB, originB, rotB, bodyOriginB, hullVertexSlot, maxDist, result);
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
                            const float3 worldOut = capIsA ? -n : n;
                            const float3 localOut = nwbPhysicsSafeNormal(nwbPhysicsQuatInvRotate(worldOut, qH), float3(0.0f, 1.0f, 0.0f));
                            bool foundAlignedFace = false;
                            uint bestFace = 0u;
                            float3 bestLocalN = float3(0.0f, 1.0f, 0.0f);
                            float bestFaceOff = 0.0f;
                            for(uint fi = 0u; fi < fCount; ++fi){
                                const float4 pl = nwbPhysicsHullLocalFacePlane(pH, hullVertexSlot, fi);
                                if(dot(pl.xyz, localOut) > 0.995f){
                                    foundAlignedFace = true;
                                    bestFace = fi;
                                    bestLocalN = pl.xyz;
                                    bestFaceOff = pl.w;
                                    break;
                                }
                            }
                            if(foundAlignedFace){
                                const float3 wP0 = origC + nwbPhysicsQuatRotate(pC.localCenterRadius.xyz, qC);
                                const float3 wP1 = origC + nwbPhysicsQuatRotate(pC.localExtentOrP1.xyz, qC);
                                const float3 l0 = nwbPhysicsQuatInvRotate(wP0 - origH, qH);
                                const float3 l1 = nwbPhysicsQuatInvRotate(wP1 - origH, qH);
                                const float faceSep = min(dot(bestLocalN, l0) - bestFaceOff, dot(bestLocalN, l1) - bestFaceOff);
                                if(faceSep <= totalR + maxDist){
                                    handled = nwbPhysicsTryClipCapsuleAgainstHullFace(
                                        capIsA,
                                        pH,
                                        origH,
                                        qH,
                                        bodyOriginA,
                                        bodyOriginB,
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
                        result.anchorAFeature[0] = float4(pA + n * proxyA.localCenterRadius.w - bodyOriginA, asfloat(20u));
                        result.anchorBSeparation[0] = float4(pB - n * proxyB.localCenterRadius.w - bodyOriginB, sep);
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
        const float3 q = nwbPhysicsClosestPointOnSegment(cA, p0, p1);
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
        const float3 q = nwbPhysicsClosestPointOnSegment(cB, p0, p1);
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
                nwbPhysicsComputeTangentBasis(dirA, t1, t2);
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
        const float4 qBox = sphereIsA
            ? ((proxyB.localRotationOrMeta.w >= 0.999999f) ? rotB : nwbPhysicsQuatNormalize(nwbPhysicsQuatMul(rotB, proxyB.localRotationOrMeta)))
            : ((proxyA.localRotationOrMeta.w >= 0.999999f) ? rotA : nwbPhysicsQuatNormalize(nwbPhysicsQuatMul(rotA, proxyA.localRotationOrMeta)));
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
            uint featureIdx = 0u;
            if(distSq > 1.0e-10f){
                const float dist = sqrt(distSq);
                localNormal = deltaLocal / dist;
                separation = dist - totalR;
                if(localS.x < -hBox.x)
                    featureIdx |= 1u;
                else if(localS.x > hBox.x)
                    featureIdx |= 2u;
                if(localS.y < -hBox.y)
                    featureIdx |= 4u;
                else if(localS.y > hBox.y)
                    featureIdx |= 8u;
                if(localS.z < -hBox.z)
                    featureIdx |= 16u;
                else if(localS.z > hBox.z)
                    featureIdx |= 32u;
            }
            else{
                const float3 dx = hBox - abs(localS);
                uint faceId = 0u;
                if(dx.x <= dx.y && dx.x <= dx.z){
                    faceId = localS.x >= 0.0f ? 0u : 1u;
                    localNormal = float3(localS.x >= 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f);
                    separation = -dx.x - totalR;
                    worldBoxPt = cBox + nwbPhysicsQuatRotate(float3(localNormal.x * hBox.x, localS.y, localS.z), qBox);
                }
                else if(dx.y <= dx.z){
                    faceId = localS.y >= 0.0f ? 2u : 3u;
                    localNormal = float3(0.0f, localS.y >= 0.0f ? 1.0f : -1.0f, 0.0f);
                    separation = -dx.y - totalR;
                    worldBoxPt = cBox + nwbPhysicsQuatRotate(float3(localS.x, localNormal.y * hBox.y, localS.z), qBox);
                }
                else{
                    faceId = localS.z >= 0.0f ? 4u : 5u;
                    localNormal = float3(0.0f, 0.0f, localS.z >= 0.0f ? 1.0f : -1.0f);
                    separation = -dx.z - totalR;
                    worldBoxPt = cBox + nwbPhysicsQuatRotate(float3(localS.x, localS.y, localNormal.z * hBox.z), qBox);
                }
                featureIdx = 64u + faceId;
            }
            const float3 outwardBoxNormal = nwbPhysicsQuatRotate(localNormal, qBox);
            const float3 nAtoB = sphereIsA ? -outwardBoxNormal : outwardBoxNormal;
            const float3 worldSpherePt = cS + (sphereIsA ? nAtoB : -nAtoB) * rS;
            const float3 finalBoxPt = worldBoxPt + outwardBoxNormal * rBox;
            result.pairInfo.z = 1u;
            result.normalPad = float4(nAtoB, 0.0f);
            result.anchorAFeature[0] = float4((sphereIsA ? worldSpherePt : finalBoxPt) - originA, asfloat(nwbPhysicsMakeFeatureId(0u, 0u, 1u, featureIdx)));
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
        const float4 qBox = capIsA
            ? ((proxyB.localRotationOrMeta.w >= 0.999999f) ? rotB : nwbPhysicsQuatNormalize(nwbPhysicsQuatMul(rotB, proxyB.localRotationOrMeta)))
            : ((proxyA.localRotationOrMeta.w >= 0.999999f) ? rotA : nwbPhysicsQuatNormalize(nwbPhysicsQuatMul(rotA, proxyA.localRotationOrMeta)));
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

        for(uint c = 0u; c < 3u; ++c){
            const float dc = (c == 0u) ? dSeg.x : ((c == 1u) ? dSeg.y : dSeg.z);
            const float lc = (c == 0u) ? l0.x : ((c == 1u) ? l0.y : l0.z);
            const float hc = (c == 0u) ? hBox.x : ((c == 1u) ? hBox.y : hBox.z);
            if(abs(dc) > 1.0e-8f){
                const float invD = 1.0f / dc;
                const float tNeg = (-hc - lc) * invD;
                if(tNeg > 0.0f && tNeg < 1.0f)
                    ts[tCount++] = tNeg;
                const float tPos = (hc - lc) * invD;
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
        uint bestFace = 0u;
        bool wonByEdge = false;
        bool edgeSeparated = false;
        if(bestDistSq > 1.0e-10f){
            const float dist = sqrt(bestDistSq);
            outLocalN = (bestSegPt - bestBoxPt) / dist;
            singleSep = dist - totalR;
            const float nax = abs(outLocalN.x);
            const float nay = abs(outLocalN.y);
            const float naz = abs(outLocalN.z);
            if(nax >= nay && nax >= naz)
                bestFace = (outLocalN.x >= 0.0f) ? 0u : 1u;
            else if(nay >= naz)
                bestFace = (outLocalN.y >= 0.0f) ? 2u : 3u;
            else
                bestFace = (outLocalN.z >= 0.0f) ? 4u : 5u;
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
                        bestFace = k * 2u + s;
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
                bestFace = 6u + bestEdgeK;
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
                            const uint feat = nwbPhysicsMakeFeatureId(0u, emitted, 1u, bestFace);
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
                result.anchorAFeature[0] = float4((capIsA ? capSurf : boxSurf) - originA, asfloat(nwbPhysicsMakeFeatureId(0u, 2u, 1u, bestFace)));
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

        if((result.pairInfo.z > 0u) || (result.pairInfo.w != 0u)){
            keepManifold = true;
        }
        else if(fastOrBulletPair && !isSensorPair && speculativeDist > 0.0f){
            const float3 relDisp = poseB.sweepDispFlags.xyz - poseA.sweepDispFlags.xyz;
            keepManifold = (dot(relDisp, relDisp) > speculativeDist * speculativeDist);
        }
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
            [unroll]
            for(uint k = 0u; k < NWB_PHYSICS_MAX_MANIFOLD_POINTS; ++k){
                if(k < emitCount){
                    g_NwbPhysicsNarrowphaseManifoldBuffers[manifoldSlot][outIdx].anchorAFeature[k] = result.anchorAFeature[k];
                    g_NwbPhysicsNarrowphaseManifoldBuffers[manifoldSlot][outIdx].anchorBSeparation[k] = result.anchorBSeparation[k];
                }
            }
        }
    }
    else if(idx < maxPairs){
        if(!keepManifold){
            result.pairInfo.z = 0u;
            result.pairInfo.w = 0u;
        }
        g_NwbPhysicsNarrowphaseManifoldBuffers[manifoldSlot][idx].pairInfo = result.pairInfo;
        g_NwbPhysicsNarrowphaseManifoldBuffers[manifoldSlot][idx].normalPad = result.normalPad;
        const uint emitCount = min(result.pairInfo.z, NWB_PHYSICS_MAX_MANIFOLD_POINTS);
        [unroll]
        for(uint k = 0u; k < NWB_PHYSICS_MAX_MANIFOLD_POINTS; ++k){
            if(k < emitCount){
                g_NwbPhysicsNarrowphaseManifoldBuffers[manifoldSlot][idx].anchorAFeature[k] = result.anchorAFeature[k];
                g_NwbPhysicsNarrowphaseManifoldBuffers[manifoldSlot][idx].anchorBSeparation[k] = result.anchorBSeparation[k];
            }
        }
    }
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

