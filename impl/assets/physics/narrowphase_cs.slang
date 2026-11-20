// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "physics_common.slangi"


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

uint nwbPhysicsClipPolygonPlane(
    const float3 inPts[8],
    const uint inCount,
    const float3 planeNormal,
    const float planeOffset,
    out float3 outPts[8]
){
    for(uint k = 0u; k < 8u; ++k)
        outPts[k] = float3(0.0f, 0.0f, 0.0f);
    uint outCount = 0u;
    if(inCount == 0u)
        return 0u;

    float3 prevPt = inPts[inCount - 1u];
    float prevDist = dot(planeNormal, prevPt) - planeOffset;

    for(uint i = 0u; i < inCount; ++i){
        const float3 curPt = inPts[i];
        const float curDist = dot(planeNormal, curPt) - planeOffset;

        if(curDist <= 0.0f){
            if(prevDist > 0.0f){
                const float denom = prevDist - curDist;
                const float t = abs(denom) > 1.0e-12f ? (prevDist / denom) : 0.0f;
                if(outCount < 8u){
                    outPts[outCount] = lerp(prevPt, curPt, clamp(t, 0.0f, 1.0f));
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
                outPts[outCount] = lerp(prevPt, curPt, clamp(t, 0.0f, 1.0f));
                ++outCount;
            }
        }

        prevPt = curPt;
        prevDist = curDist;
    }
    return outCount;
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

    if(outABD){
        float3 cW[4] = { a, b, d, float3(0.0f, 0.0f, 0.0f) };
        float3 cWA[4] = { wAA, wAB, wAD, float3(0.0f, 0.0f, 0.0f) };
        float3 cWB[4] = { wBA, wBB, wBD, float3(0.0f, 0.0f, 0.0f) };
        uint cIdxA[4] = { iAA, iAB, iAD, 0u };
        uint cIdxB[4] = { iBA, iBB, iBD, 0u };
        float cU[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
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
    if(outACD){
        float3 cW[4] = { a, c, d, float3(0.0f, 0.0f, 0.0f) };
        float3 cWA[4] = { wAA, wAC, wAD, float3(0.0f, 0.0f, 0.0f) };
        float3 cWB[4] = { wBA, wBC, wBD, float3(0.0f, 0.0f, 0.0f) };
        uint cIdxA[4] = { iAA, iAC, iAD, 0u };
        uint cIdxB[4] = { iBA, iBC, iBD, 0u };
        float cU[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
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
    if(outBCD){
        float3 cW[4] = { b, c, d, float3(0.0f, 0.0f, 0.0f) };
        float3 cWA[4] = { wAB, wAC, wAD, float3(0.0f, 0.0f, 0.0f) };
        float3 cWB[4] = { wBB, wBC, wBD, float3(0.0f, 0.0f, 0.0f) };
        uint cIdxA[4] = { iAB, iAC, iAD, 0u };
        uint cIdxB[4] = { iBB, iBC, iBD, 0u };
        float cU[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
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
    if(abs(signDet) <= 1.0e-10f && outABC){
        float3 cW[4] = { a, b, c, float3(0.0f, 0.0f, 0.0f) };
        float3 cWA[4] = { wAA, wAB, wAC, float3(0.0f, 0.0f, 0.0f) };
        float3 cWB[4] = { wBA, wBB, wBC, float3(0.0f, 0.0f, 0.0f) };
        uint cIdxA[4] = { iAA, iAB, iAC, 0u };
        uint cIdxB[4] = { iBA, iBB, iBC, 0u };
        float cU[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
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
    const uint vCount = min(metaPack & 0xffu, 128u);
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

void nwbPhysicsReduceManifoldTo4(
    const float3 clipRefN,
    const float3 candWRef[8],
    const float3 candWInc[8],
    const float3 candPos[8],
    const float candSep[8],
    const uint candFeat[8],
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

void nwbPhysicsComputeBasis(const float3 n, out float3 outU1, out float3 outU2){
    const float3 up = abs(n.y) < 0.9f ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
    outU1 = nwbPhysicsSafeNormal(cross(up, n), float3(1.0f, 0.0f, 0.0f));
    outU2 = cross(n, outU1);
}

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
    sWA[0] = nwbPhysicsSupportWorld(proxyA, originA, rotA, hullVertexSlot, float3(1.0f, 0.0f, 0.0f), sIdxA[0]);
    nwbPhysicsSupportProxyOrTri(proxyB, originB, rotB, hullVertexSlot, triB, tv0, tv1, tv2, float3(-1.0f, 0.0f, 0.0f), sIdxB[0], sWB[0]);
    sW[0] = sWB[0] - sWA[0];
    sU[0] = 1.0f;

    outHitDeep = false;
    for(uint iter = 0u; iter < 24u; ++iter){
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
        const float3 wA = nwbPhysicsSupportWorld(proxyA, originA, rotA, hullVertexSlot, -d, idxA);
        float3 wB = float3(0.0f, 0.0f, 0.0f);
        nwbPhysicsSupportProxyOrTri(proxyB, originB, rotB, hullVertexSlot, triB, tv0, tv1, tv2, d, idxB, wB);

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
    const float4 packedVec = g_NwbPhysicsFloat4Buffers[hullVertexSlot][vOffset + vCount + fCount + (edgeIdx >> 2u)];
    const uint lane = edgeIdx & 3u;
    if(lane == 0u) return asuint(packedVec.x);
    if(lane == 1u) return asuint(packedVec.y);
    if(lane == 2u) return asuint(packedVec.z);
    return asuint(packedVec.w);
}

bool nwbPhysicsIsMinkowskiFace(const float3 a, const float3 b, const float3 edgeBA, const float3 c, const float3 d, const float3 edgeDC){
    const float cba = dot(c, edgeBA);
    const float dba = dot(d, edgeBA);
    const float adc = dot(a, edgeDC);
    const float bdc = dot(b, edgeDC);
    return (cba * dba < 0.0f) && (adc * bdc < 0.0f) && (cba * bdc > 0.0f);
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

    const float tolSq = 1.0e-8f;
    const float3 dv0 = localPt - v0;
    const float3 dv1 = localPt - v1;
    const float3 dv2 = localPt - v2;
    bool smoothFeature = false;
    if(dot(dv0, dv0) <= tolSq)
        smoothFeature = (edgeFlags & 8u) != 0u;
    else if(dot(dv1, dv1) <= tolSq)
        smoothFeature = (edgeFlags & 16u) != 0u;
    else if(dot(dv2, dv2) <= tolSq)
        smoothFeature = (edgeFlags & 32u) != 0u;
    else{
        const float3 pe0 = localPt - nwbPhysicsClosestPointOnSegment(v0, v1, localPt);
        const float3 pe1 = localPt - nwbPhysicsClosestPointOnSegment(v1, v2, localPt);
        const float3 pe2 = localPt - nwbPhysicsClosestPointOnSegment(v2, v0, localPt);
        const float d0Sq = dot(pe0, pe0);
        const float d1Sq = dot(pe1, pe1);
        const float d2Sq = dot(pe2, pe2);
        if(d0Sq <= tolSq && d0Sq <= d1Sq && d0Sq <= d2Sq)
            smoothFeature = (edgeFlags & 1u) != 0u;
        else if(d1Sq <= tolSq && d1Sq <= d2Sq)
            smoothFeature = (edgeFlags & 2u) != 0u;
        else if(d2Sq <= tolSq)
            smoothFeature = (edgeFlags & 4u) != 0u;
    }
    if(!smoothFeature)
        return false;

    const float nDot = dot(rawMeshToOtherN, triN);
    const float3 targetN = (nDot >= 0.0f) ? triN : -triN;
    if(dot(rawMeshToOtherN, targetN) < 0.9999f){
        outSmoothedN = targetN;
        return true;
    }
    return false;
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
    out float3 outVerts[8],
    out float3 outWorldNormal,
    out float outWorldOffset
){
    for(uint k = 0u; k < 8u; ++k)
        outVerts[k] = float3(0.0f, 0.0f, 0.0f);

    if((proxy.bodyIndexTypeShape.w & NWB_PHYSICS_SHAPE_FLAG_IS_BOX) != 0u){
        const float3 cBox = origin + nwbPhysicsQuatRotate(proxy.localCenterRadius.xyz, rot);
        const float4 qBox = nwbPhysicsQuatNormalize(nwbPhysicsQuatMul(rot, proxy.localRotation));
        const float3 ax = nwbPhysicsQuatRotate(float3(1.0f, 0.0f, 0.0f), qBox);
        const float3 ay = nwbPhysicsQuatRotate(float3(0.0f, 1.0f, 0.0f), qBox);
        const float3 az = nwbPhysicsQuatRotate(float3(0.0f, 0.0f, 1.0f), qBox);
        const float3 h = proxy.localExtentOrP1.xyz;
        float3 fv[4];
        nwbPhysicsGetBoxFace(cBox, ax, ay, az, h, int(faceIdx), fv);
        for(uint k = 0u; k < 4u; ++k)
            outVerts[k] = fv[k];
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
    float3 localVerts[8];
    uint count = 0u;
    float3 center = float3(0.0f, 0.0f, 0.0f);
    for(uint i = 0u; i < vCount; ++i){
        const float3 v = g_NwbPhysicsFloat4Buffers[hullVertexSlot][vOffset + i].xyz;
        if(abs(dot(lN, v) - lOff) <= 1.5e-3f){
            if(count >= 8u)
                return 0u;
            localVerts[count] = v;
            center += v;
            ++count;
        }
    }
    if(count < 3u)
        return 0u;
    center /= float(count);

    float3 u1;
    float3 u2;
    nwbPhysicsComputeBasis(lN, u1, u2);
    float angles[8];
    for(uint i = 0u; i < count; ++i){
        const float3 rel = localVerts[i] - center;
        angles[i] = atan2(dot(rel, u2), dot(rel, u1));
    }
    for(uint i = 1u; i < count; ++i){
        const float3 keyV = localVerts[i];
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
        outVerts[i] = origin + nwbPhysicsQuatRotate(localVerts[i], rot);
    return count;
}

void nwbPhysicsSolveHullDeep(
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
    const uint typeA = proxyA.bodyIndexTypeShape.y;
    const uint typeB = proxyB.bodyIndexTypeShape.y;
    const float rA = proxyA.localCenterRadius.w;
    const float rB = proxyB.localCenterRadius.w;
    const float totalR = rA + rB;

    if(typeA == NWB_PHYSICS_SHAPE_TYPE_SPHERE || typeB == NWB_PHYSICS_SHAPE_TYPE_SPHERE){
        const bool sphereIsA = (typeA == NWB_PHYSICS_SHAPE_TYPE_SPHERE);
        const NwbPhysicsGpuShapeProxy pS = sphereIsA ? proxyA : proxyB;
        const NwbPhysicsGpuShapeProxy pH = sphereIsA ? proxyB : proxyA;
        const float3 origS = sphereIsA ? originA : originB;
        const float4 qS = sphereIsA ? rotA : rotB;
        const float3 origH = sphereIsA ? originB : originA;
        const float4 qH = sphereIsA ? rotB : rotA;
        const uint fCount = nwbPhysicsHullFaceCount(pH);
        const uint vCount = nwbPhysicsHullVertexCount(pH);
        if(fCount == 0u || fCount > 128u || vCount == 0u){
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
        return;
    }

    if(typeA == NWB_PHYSICS_SHAPE_TYPE_CAPSULE || typeB == NWB_PHYSICS_SHAPE_TYPE_CAPSULE){
        const bool capIsA = (typeA == NWB_PHYSICS_SHAPE_TYPE_CAPSULE);
        const NwbPhysicsGpuShapeProxy pC = capIsA ? proxyA : proxyB;
        const NwbPhysicsGpuShapeProxy pH = capIsA ? proxyB : proxyA;
        const float3 origC = capIsA ? originA : originB;
        const float4 qC = capIsA ? rotA : rotB;
        const float3 origH = capIsA ? originB : originA;
        const float4 qH = capIsA ? rotB : rotA;
        const uint fCount = nwbPhysicsHullFaceCount(pH);
        const uint vCount = nwbPhysicsHullVertexCount(pH);
        if(fCount == 0u || fCount > 128u || vCount == 0u){
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
        const float3 worldOut = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(bestLocalN, qH), float3(0.0f, 1.0f, 0.0f));
        const float3 nAtoB = capIsA ? -worldOut : worldOut;
        result.normalPad = float4(nAtoB, 0.0f);

        float3 clipA = l0;
        float3 clipB = l1;
        bool clippedOut = false;
        for(uint fi = 0u; fi < fCount; ++fi){
            if(fi == bestFace)
                continue;
            const float4 pl = nwbPhysicsHullLocalFacePlane(pH, hullVertexSlot, fi);
            const float dA = dot(pl.xyz, clipA) - (pl.w + totalR);
            const float dB = dot(pl.xyz, clipB) - (pl.w + totalR);
            if(dA > 0.0f && dB > 0.0f){
                clippedOut = true;
                break;
            }
            if(dA > 0.0f){
                const float t = dA / max(dA - dB, 1.0e-12f);
                clipA = lerp(clipA, clipB, clamp(t, 0.0f, 1.0f));
            }
            else if(dB > 0.0f){
                const float t = dA / max(dA - dB, 1.0e-12f);
                clipB = lerp(clipA, clipB, clamp(t, 0.0f, 1.0f));
            }
        }
        if(!clippedOut){
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
                result.pairInfo.z = emitted;
                return;
            }
        }

        const float3 deepLocal = (bestD1 < bestD0) ? l1 : l0;
        const float minD = min(bestD0, bestD1);
        const float3 deepWorld = origH + nwbPhysicsQuatRotate(deepLocal, qH);
        const float3 wC = deepWorld - worldOut * rC;
        const float3 wH = deepWorld + worldOut * (rH - minD);
        result.pairInfo.z = 1u;
        result.anchorAFeature[0] = float4((capIsA ? wC : wH) - bodyPosA, asfloat(22u + bestFace));
        result.anchorBSeparation[0] = float4((capIsA ? wH : wC) - bodyPosB, minD - totalR);
        return;
    }

    const uint fCountA = nwbPhysicsHullFaceCount(proxyA);
    const uint fCountB = nwbPhysicsHullFaceCount(proxyB);
    const uint vCountA = nwbPhysicsHullVertexCount(proxyA);
    const uint vCountB = nwbPhysicsHullVertexCount(proxyB);
    const uint eCountA = nwbPhysicsHullEdgeCount(proxyA);
    const uint eCountB = nwbPhysicsHullEdgeCount(proxyB);
    if(fCountA == 0u || fCountB == 0u || fCountA > 64u || fCountB > 64u || vCountA == 0u || vCountB == 0u || vCountA > 64u || vCountB > 64u || eCountA > 128u || eCountB > 128u){
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

    float3 refPoly[8];
    float3 refNUnused = refWorldN;
    float refOff = 0.0f;
    const uint refPolyCount = nwbPhysicsGatherHullWorldFacePoly(refProxy, refOrig, refRot, hullVertexSlot, refFace, refPoly, refNUnused, refOff);

    float3 poly0[8];
    float3 poly1[8];
    for(uint k = 0u; k < 8u; ++k)
        poly1[k] = float3(0.0f, 0.0f, 0.0f);
    float3 incNUnused = refWorldN;
    float incOffUnused = 0.0f;
    uint polyCount = nwbPhysicsGatherHullWorldFacePoly(incProxy, incOrig, incRot, hullVertexSlot, incFace, poly0, incNUnused, incOffUnused);

    if(refPolyCount < 3u || polyCount < 3u){
        result.pairInfo.w = 1u;
        return;
    }

    for(uint i = 0u; i < refPolyCount; ++i){
        const float3 v0 = refPoly[i];
        const float3 v1 = refPoly[(i + 1u) % refPolyCount];
        const float3 sideN = nwbPhysicsSafeNormal(cross(v1 - v0, refNUnused), float3(1.0f, 0.0f, 0.0f));
        const float sideOff = dot(sideN, v0);
        polyCount = nwbPhysicsClipPolygonPlane(poly0, polyCount, sideN, sideOff, poly1);
        if(polyCount == 0u){
            result.pairInfo.w = 1u;
            return;
        }
        for(uint k = 0u; k < polyCount; ++k)
            poly0[k] = poly1[k];
    }

    float3 candWRef[8];
    float3 candWInc[8];
    float3 candPos[8];
    float candSep[8];
    uint candFeat[8];
    uint candCount = 0u;
    for(uint k = 0u; k < polyCount && candCount < 8u; ++k){
        const float sep = dot(refNUnused, poly0[k]) - refOff - totalR;
        if(sep <= maxDist){
            candWRef[candCount] = poly0[k] - refNUnused * (sep + totalR - refR);
            candWInc[candCount] = poly0[k] - refNUnused * incR;
            candPos[candCount] = poly0[k];
            candSep[candCount] = sep;
            candFeat[candCount] = 200u + k;
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
        result.pairInfo.w = 1u;
        return;
    }

    const bool meshIsA = (typeA == NWB_PHYSICS_SHAPE_TYPE_MESH);
    const NwbPhysicsGpuShapeProxy meshP = meshIsA ? proxyA : proxyB;
    const NwbPhysicsGpuShapeProxy otherP = meshIsA ? proxyB : proxyA;
    const float3 meshOrig = meshIsA ? originA : originB;
    const float4 meshRot = meshIsA ? rotA : rotB;
    const float3 otherOrig = meshIsA ? originB : originA;
    const float4 otherRot = meshIsA ? rotB : rotA;
    const float3 meshBodyPos = meshIsA ? bodyPosA : bodyPosB;
    const float3 otherBodyPos = meshIsA ? bodyPosB : bodyPosA;

    const uint nodeBase = meshP.filterData.w;
    const uint triBase = asuint(meshP.aabbMaxPad.w);
    if(nodeBase == NWB_PHYSICS_INVALID_INDEX || triBase == NWB_PHYSICS_INVALID_INDEX){
        result.pairInfo.w = 1u;
        return;
    }

    const uint otherType = otherP.bodyIndexTypeShape.y;
    const float meshR = meshP.localCenterRadius.w;
    const float otherR = otherP.localCenterRadius.w;
    const float totalR = otherR + meshR;
    const float queryR = totalR + maxDist;

    const float4 meshInvRot = float4(-meshRot.xyz, meshRot.w);
    const float3 relOrig = nwbPhysicsQuatRotate(otherOrig - meshOrig, meshInvRot);
    const float4 relRot = nwbPhysicsQuatNormalize(nwbPhysicsQuatMul(meshInvRot, otherRot));

    float3 queryMin = float3(0.0f, 0.0f, 0.0f);
    float3 queryMax = float3(0.0f, 0.0f, 0.0f);
    if(otherType == NWB_PHYSICS_SHAPE_TYPE_SPHERE){
        const float3 lc = relOrig + nwbPhysicsQuatRotate(otherP.localCenterRadius.xyz, relRot);
        queryMin = lc - float3(queryR, queryR, queryR);
        queryMax = lc + float3(queryR, queryR, queryR);
    }
    else if(otherType == NWB_PHYSICS_SHAPE_TYPE_CAPSULE){
        const float3 l0 = relOrig + nwbPhysicsQuatRotate(otherP.localCenterRadius.xyz, relRot);
        const float3 l1 = relOrig + nwbPhysicsQuatRotate(otherP.localExtentOrP1.xyz, relRot);
        queryMin = min(l0, l1) - float3(queryR, queryR, queryR);
        queryMax = max(l0, l1) + float3(queryR, queryR, queryR);
    }
    else{
        const float4 localBoxRot = nwbPhysicsQuatNormalize(nwbPhysicsQuatMul(relRot, otherP.localRotation));
        const float3 lc = relOrig + nwbPhysicsQuatRotate(otherP.localCenterRadius.xyz, relRot);
        const float3 axX = abs(nwbPhysicsQuatRotate(float3(1.0f, 0.0f, 0.0f), localBoxRot));
        const float3 axY = abs(nwbPhysicsQuatRotate(float3(0.0f, 1.0f, 0.0f), localBoxRot));
        const float3 axZ = abs(nwbPhysicsQuatRotate(float3(0.0f, 0.0f, 1.0f), localBoxRot));
        const float3 h = otherP.localExtentOrP1.xyz;
        const float3 ext = axX * h.x + axY * h.y + axZ * h.z + float3(queryR, queryR, queryR);
        queryMin = lc - ext;
        queryMax = lc + ext;
    }

    float bestSep = 1.0e20f;
    float3 chosenWorldN = float3(0.0f, -1.0f, 0.0f);
    float3 bestWorldOther = float3(0.0f, 0.0f, 0.0f);
    float3 bestWorldMesh = float3(0.0f, 0.0f, 0.0f);
    uint bestTriFeat = 0u;
    bool foundSingle = false;
    float hullSeps[4] = { 1.0e20f, 1.0e20f, 1.0e20f, 1.0e20f };

    uint stack[32];
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
                const float4 mt0 = g_NwbPhysicsFloat4Buffers[meshTriSlot][triIdx * 3u + 0u];
                const float4 mt1 = g_NwbPhysicsFloat4Buffers[meshTriSlot][triIdx * 3u + 1u];
                const float4 mt2 = g_NwbPhysicsFloat4Buffers[meshTriSlot][triIdx * 3u + 2u];
                const float3 v0 = mt0.xyz;
                const float3 v1 = mt1.xyz;
                const float3 v2 = mt2.xyz;
                const uint edgeFlags = asuint(mt0.w);
                const bool isClosed = (mt1.w != 0.0f);
                const float3 triN = nwbPhysicsSafeNormal(cross(v1 - v0, v2 - v0), float3(0.0f, 1.0f, 0.0f));
                const float3 tMin = min(v0, min(v1, v2));
                const float3 tMax = max(v0, max(v1, v2));
                if(!nwbPhysicsAabbOverlaps(tMin, tMax, queryMin, queryMax))
                    continue;

                if(otherType == NWB_PHYSICS_SHAPE_TYPE_SPHERE){
                    const float3 lc = relOrig + nwbPhysicsQuatRotate(otherP.localCenterRadius.xyz, relRot);
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
                        if(dot(candN, triN) > -0.95f)
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
                        bestWorldOther = wc + wN * otherR;
                        bestWorldMesh = wQ - wN * meshR;
                        bestTriFeat = (triIdx & 0xffu) | (3u << 8u);
                    }
                }
                else{
                    const float3 wV0 = meshOrig + nwbPhysicsQuatRotate(v0, meshRot);
                    const float3 wV1 = meshOrig + nwbPhysicsQuatRotate(v1, meshRot);
                    const float3 wV2 = meshOrig + nwbPhysicsQuatRotate(v2, meshRot);
                    float3 wTriN = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(triN, meshRot), float3(0.0f, 1.0f, 0.0f));
                    float3 triNLocal = triN;
                    const float3 wOtherC = otherOrig + nwbPhysicsQuatRotate(otherP.localCenterRadius.xyz, otherRot);
                    if(!isClosed && dot(wOtherC - wV0, wTriN) < 0.0f){
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
                        if(otherType == NWB_PHYSICS_SHAPE_TYPE_CAPSULE){
                            if(!foundSingle || sep < bestSep){
                                foundSingle = true;
                                bestSep = sep;
                                chosenWorldN = meshIsA ? -nOtherToMesh : nOtherToMesh;
                                bestWorldOther = wO;
                                bestWorldMesh = wM;
                                bestTriFeat = (triIdx & 0xffu) | (3u << 8u);
                            }
                        }
                        else{
                            if(sep < bestSep){
                                bestSep = sep;
                                chosenWorldN = meshIsA ? -nOtherToMesh : nOtherToMesh;
                            }
                            const float3 wAOut = meshIsA ? wM : wO;
                            const float3 wBOut = meshIsA ? wO : wM;
                            nwbPhysicsPushCandPoint(wAOut, wBOut, sep, 300u + (ti & 0xffu), bodyPosA, bodyPosB, result, hullSeps);
                        }
                    }
                    else if(otherType == NWB_PHYSICS_SHAPE_TYPE_CAPSULE){
                        const float3 l0 = relOrig + nwbPhysicsQuatRotate(otherP.localCenterRadius.xyz, relRot);
                        const float3 l1 = relOrig + nwbPhysicsQuatRotate(otherP.localExtentOrP1.xyz, relRot);
                        const float d0 = dot(triNLocal, l0 - v0);
                        const float d1 = dot(triNLocal, l1 - v0);
                        const float3 deepL = isClosed ? ((d0 < d1) ? l0 : l1) : (abs(d1) < abs(d0) ? l1 : l0);
                        const float sep = isClosed ? (min(d0, d1) - totalR) : (-min(abs(d0), abs(d1)) - totalR);
                        if(isClosed && sep < -(totalR + 0.25f))
                            continue;
                        const float3 deepW = meshOrig + nwbPhysicsQuatRotate(deepL, meshRot);
                        const float3 nOtherToMesh = -wTriN;
                        if(!foundSingle || sep < bestSep){
                            foundSingle = true;
                            bestSep = sep;
                            chosenWorldN = meshIsA ? -nOtherToMesh : nOtherToMesh;
                            bestWorldOther = deepW + nOtherToMesh * otherR;
                            bestWorldMesh = deepW - nOtherToMesh * meshR;
                            bestTriFeat = (triIdx & 0xffu) | (3u << 8u);
                        }
                    }
                    else{
                        const uint fCountO = nwbPhysicsHullFaceCount(otherP);
                        const uint eCountO = nwbPhysicsHullEdgeCount(otherP);
                        const uint vCountO = nwbPhysicsHullVertexCount(otherP);
                        if(fCountO == 0u || fCountO > 32u || eCountO > 64u || vCountO == 0u || vCountO > 64u){
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
                                chosenWorldN = meshIsA ? -worldWinN : worldWinN;
                            }
                            const float3 wAOut = meshIsA ? wM : wO;
                            const float3 wBOut = meshIsA ? wO : wM;
                            nwbPhysicsPushCandPoint(wAOut, wBOut, winSep, 500u + (bestEdgeHullIdx & 0xffu) + ((ti & 0xffu) << 8u), bodyPosA, bodyPosB, result, hullSeps);
                        }
                        else if(winKind == 1u){
                            float3 refPoly[8];
                            float3 refNUnused = bestHullN;
                            float refOff = 0.0f;
                            const uint refPolyCount = nwbPhysicsGatherHullWorldFacePoly(otherP, relOrig, relRot, hullVertexSlot, bestHullFace, refPoly, refNUnused, refOff);
                            if(refPolyCount < 3u){
                                result.pairInfo.w = 1u;
                                return;
                            }
                            float3 poly0[8];
                            float3 poly1[8];
                            for(uint k = 0u; k < 8u; ++k){
                                poly0[k] = float3(0.0f, 0.0f, 0.0f);
                                poly1[k] = float3(0.0f, 0.0f, 0.0f);
                            }
                            poly0[0] = v0;
                            poly0[1] = v1;
                            poly0[2] = v2;
                            uint polyCount = 3u;
                            for(uint i = 0u; i < refPolyCount; ++i){
                                const float3 rv0 = refPoly[i];
                                const float3 rv1 = refPoly[(i + 1u) % refPolyCount];
                                const float3 sideN = nwbPhysicsSafeNormal(cross(rv1 - rv0, refNUnused), float3(1.0f, 0.0f, 0.0f));
                                const float sideOff = dot(sideN, rv0);
                                polyCount = nwbPhysicsClipPolygonPlane(poly0, polyCount, sideN, sideOff, poly1);
                                if(polyCount == 0u)
                                    break;
                                for(uint k = 0u; k < polyCount; ++k)
                                    poly0[k] = poly1[k];
                            }
                            for(uint k = 0u; k < polyCount; ++k){
                                const float3 ptB = poly0[k];
                                const float sep = dot(bestHullN, ptB) - bestHullOff - totalR;
                                if(sep > maxDist)
                                    continue;
                                const float3 wM = meshOrig + nwbPhysicsQuatRotate(ptB, meshRot) - worldWinN * meshR;
                                const float3 wO = meshOrig + nwbPhysicsQuatRotate(ptB - bestHullN * (sep + totalR - otherR), meshRot);
                                if(sep < bestSep){
                                    bestSep = sep;
                                    chosenWorldN = meshIsA ? -worldWinN : worldWinN;
                                }
                                const float3 wAOut = meshIsA ? wM : wO;
                                const float3 wBOut = meshIsA ? wO : wM;
                                nwbPhysicsPushCandPoint(wAOut, wBOut, sep, 600u + (k & 0xffu) + ((ti & 0xffu) << 8u), bodyPosA, bodyPosB, result, hullSeps);
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
                            float3 poly0[8];
                            float3 poly1[8];
                            for(uint k = 0u; k < 8u; ++k)
                                poly1[k] = float3(0.0f, 0.0f, 0.0f);
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
                            polyCount = nwbPhysicsClipPolygonPlane(poly0, polyCount, sn0, dot(sn0, v0) + 5.0e-3f, poly1);
                            if(polyCount > 0u){
                                polyCount = nwbPhysicsClipPolygonPlane(poly1, polyCount, sn1, dot(sn1, v1) + 5.0e-3f, poly0);
                                if(polyCount > 0u){
                                    polyCount = nwbPhysicsClipPolygonPlane(poly0, polyCount, sn2, dot(sn2, v2) + 5.0e-3f, poly1);
                                    for(uint k = 0u; k < polyCount; ++k){
                                        const float3 ptB = poly1[k];
                                        const float planeDist = dot(triNLocal, ptB - v0);
                                        const float sep = planeDist - totalR;
                                        if(sep > maxDist)
                                            continue;
                                        const float3 wO = meshOrig + nwbPhysicsQuatRotate(ptB, meshRot) + worldWinN * otherR;
                                        const float3 wM = meshOrig + nwbPhysicsQuatRotate(ptB - triNLocal * planeDist, meshRot) - worldWinN * meshR;
                                        if(sep < bestSep){
                                            bestSep = sep;
                                            chosenWorldN = meshIsA ? -worldWinN : worldWinN;
                                        }
                                        const float3 wAOut = meshIsA ? wM : wO;
                                        const float3 wBOut = meshIsA ? wO : wM;
                                        nwbPhysicsPushCandPoint(wAOut, wBOut, sep, 700u + (k & 0xffu) + ((ti & 0xffu) << 8u), bodyPosA, bodyPosB, result, hullSeps);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        else{
            if(sp + 2u > 32u){
                result.pairInfo.w = 1u;
                return;
            }
            stack[sp++] = asuint(n0.w);
            stack[sp++] = meta & 0x7fffffffu;
        }
    }

    if(otherType == NWB_PHYSICS_SHAPE_TYPE_SPHERE || otherType == NWB_PHYSICS_SHAPE_TYPE_CAPSULE){
        if(foundSingle){
            const float3 wAOut = meshIsA ? bestWorldMesh : bestWorldOther;
            const float3 wBOut = meshIsA ? bestWorldOther : bestWorldMesh;
            result.pairInfo.z = 1u;
            result.normalPad = float4(chosenWorldN, 0.0f);
            result.anchorAFeature[0] = float4(wAOut - bodyPosA, asfloat(bestTriFeat));
            result.anchorBSeparation[0] = float4(wBOut - bodyPosB, bestSep);
        }
    }
    else if(result.pairInfo.z > 0u){
        result.normalPad = float4(chosenWorldN, 0.0f);
    }
}

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
        uint stack[32];
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
                    for(uint iter = 0u; iter < 24u; ++iter){
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
                            if(t1 > 0.0f && t1 < curMax){
                                curMax = t1;
                                const float3 n = nwbPhysicsSafeNormal(pB - pA, float3(0.0f, 1.0f, 0.0f));
                                const float3 wA = pA + n * rA;
                                const float3 wB = pB - n * rB;
                                result.pairInfo.z = 1u;
                                result.pairInfo.w = 0u;
                                result.normalPad = float4(n, t1);
                                result.anchorAFeature[0] = float4((wA + wB) * 0.5f, 0.0f);
                            }
                            else if(t1 == 0.0f && dist < totalRadius - tolerance){
                                result.pairInfo.w = 1u;
                            }
                            break;
                        }
                        const float3 n = nwbPhysicsSafeNormal(pB - pA, float3(0.0f, 1.0f, 0.0f));
                        const float approach = -dot(n, linDisp) + angSpeedBound;
                        if(approach <= 1.0e-6f)
                            break;
                        const float step = (dist - targetDist) / approach;
                        t1 += step <= 1.0e-6f ? (0.01f * curMax) : step;
                        if(t1 > curMax)
                            break;
                    }
                }
            }
            else{
                if(sp + 2u > 32u){
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
    for(uint iter = 0u; iter < 24u; ++iter){
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
            result.pairInfo.w = (t1 == 0.0f && dist < totalRadius - tolerance) ? 1u : 0u;
            result.normalPad = float4(n, t1);
            result.anchorAFeature[0] = float4((wA + wB) * 0.5f, 0.0f);
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
        if(t1 > 1.0f){
            nwbPhysicsEvalBulletSweep(bodyA, 1.0f, oAt, rAt);
            nwbPhysicsEvalBulletSweep(bodyB, 1.0f, oBt, rBt);
            nwbPhysicsRunGjkDistance(proxyA, oAt, rAt, proxyB, oBt, rBt, false, float3(0,0,0), float3(0,0,0), float3(0,0,0), hullVertexSlot, pA, pB, dist, deep);
            if(deep)
                dist = 0.0f;
            if(dist <= targetDist + tolerance){
                const float3 endN = nwbPhysicsSafeNormal(pB - pA, float3(0.0f, 1.0f, 0.0f));
                const float3 wA = pA + endN * rA;
                const float3 wB = pB - endN * rB;
                result.pairInfo.z = 1u;
                result.normalPad = float4(endN, 1.0f);
                result.anchorAFeature[0] = float4((wA + wB) * 0.5f, 0.0f);
            }
            g_NwbPhysicsNarrowphaseManifoldBuffers[manifoldSlot][pairIdx] = result;
            return;
        }
    }
    g_NwbPhysicsNarrowphaseManifoldBuffers[manifoldSlot][pairIdx] = result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_PHYSICS_WORKGROUP_SIZE, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint idx = dispatchThreadID.x;
    const uint counterSlot = g_NwbPhysicsPushConstants.slots1.w;
    uint pairCount = g_NwbPhysicsPushConstants.itemCount;
    if(counterSlot != NWB_PHYSICS_INVALID_INDEX)
        pairCount = min(pairCount, g_NwbPhysicsUintBuffers[counterSlot][1u]);
    if(idx >= pairCount)
        return;

    const uint mode = g_NwbPhysicsPushConstants.modeOrSecondaryCount;
    const uint bodySimSlot = g_NwbPhysicsPushConstants.slots0.x;
    const uint meshNodeSlot = g_NwbPhysicsPushConstants.slots0.y;
    const uint shapeSlot = g_NwbPhysicsPushConstants.slots0.z;
    const uint meshTriSlot = g_NwbPhysicsPushConstants.slots0.w;
    const uint pairSlot = g_NwbPhysicsPushConstants.slots1.x;
    const uint hullVertexSlot = g_NwbPhysicsPushConstants.slots1.y;
    const uint manifoldSlot = g_NwbPhysicsPushConstants.slots1.z;

    if(mode == NWB_PHYSICS_NARROWPHASE_MODE_BULLET_TOI){
        nwbPhysicsSolveBulletToi(idx, bodySimSlot, shapeSlot, pairSlot, hullVertexSlot, manifoldSlot, meshNodeSlot, meshTriSlot, g_NwbPhysicsPushConstants.linearSlop);
        return;
    }

    const float speculativeDist = g_NwbPhysicsPushConstants.gravitySpeculative.w;

    const NwbPhysicsGpuBroadphasePair pair = g_NwbPhysicsBroadphasePairBuffers[pairSlot][idx];
    const NwbPhysicsGpuShapeProxy proxyA = g_NwbPhysicsShapeProxyBuffers[shapeSlot][pair.indices.x];
    const NwbPhysicsGpuShapeProxy proxyB = g_NwbPhysicsShapeProxyBuffers[shapeSlot][pair.indices.y];
    const NwbPhysicsGpuBodySim bodyA = g_NwbPhysicsBodySimBuffers[bodySimSlot][pair.indices.z];
    const NwbPhysicsGpuBodySim bodyB = g_NwbPhysicsBodySimBuffers[bodySimSlot][pair.indices.w];

    const bool isSensorPair = ((proxyA.bodyIndexTypeShape.w | proxyB.bodyIndexTypeShape.w) & NWB_PHYSICS_SHAPE_FLAG_SENSOR) != 0u;
    const float maxDist = isSensorPair ? 0.0f : speculativeDist;

    float3 originA;
    float4 rotA;
    nwbPhysicsWorldTransform(bodyA, originA, rotA);
    float3 originB;
    float4 rotB;
    nwbPhysicsWorldTransform(bodyB, originB, rotB);

    NwbPhysicsGpuNarrowphaseManifold result;
    result.pairInfo = uint4(pair.indices.x, pair.indices.y, 0u, 0u);
    result.normalPad = float4(0.0f, 1.0f, 0.0f, 0.0f);
    for(uint k = 0u; k < NWB_PHYSICS_MAX_MANIFOLD_POINTS; ++k){
        result.anchorAFeature[k] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        result.anchorBSeparation[k] = float4(0.0f, 0.0f, 0.0f, 0.0f);
    }

    const uint typeA = proxyA.bodyIndexTypeShape.y;
    const uint typeB = proxyB.bodyIndexTypeShape.y;
    if(typeA == NWB_PHYSICS_SHAPE_TYPE_MESH || typeB == NWB_PHYSICS_SHAPE_TYPE_MESH){
        nwbPhysicsCollideMeshPair(proxyA, originA, rotA, bodyA.positionInvMass.xyz, proxyB, originB, rotB, bodyB.positionInvMass.xyz, meshNodeSlot, meshTriSlot, hullVertexSlot, maxDist, result);
        g_NwbPhysicsNarrowphaseManifoldBuffers[manifoldSlot][idx] = result;
        return;
    }

    const bool isBoxA = (typeA == NWB_PHYSICS_SHAPE_TYPE_HULL) && ((proxyA.bodyIndexTypeShape.w & NWB_PHYSICS_SHAPE_FLAG_IS_BOX) != 0u);
    const bool isBoxB = (typeB == NWB_PHYSICS_SHAPE_TYPE_HULL) && ((proxyB.bodyIndexTypeShape.w & NWB_PHYSICS_SHAPE_FLAG_IS_BOX) != 0u);

    if((typeA == NWB_PHYSICS_SHAPE_TYPE_HULL && !isBoxA) || (typeB == NWB_PHYSICS_SHAPE_TYPE_HULL && !isBoxB)){
        if(typeA == NWB_PHYSICS_SHAPE_TYPE_HULL && typeB == NWB_PHYSICS_SHAPE_TYPE_HULL){
            nwbPhysicsSolveHullDeep(proxyA, originA, rotA, bodyA.positionInvMass.xyz, proxyB, originB, rotB, bodyB.positionInvMass.xyz, hullVertexSlot, maxDist, result);
            g_NwbPhysicsNarrowphaseManifoldBuffers[manifoldSlot][idx] = result;
            return;
        }

        const float totalR = proxyA.localCenterRadius.w + proxyB.localCenterRadius.w;
        float3 pA = float3(0.0f, 0.0f, 0.0f);
        float3 pB = float3(0.0f, 0.0f, 0.0f);
        float dist = 0.0f;
        bool hitDeep = false;
        nwbPhysicsRunGjkDistance(proxyA, originA, rotA, proxyB, originB, rotB, false, float3(0,0,0), float3(0,0,0), float3(0,0,0), hullVertexSlot, pA, pB, dist, hitDeep);

        if(hitDeep || dist <= 1.0e-5f){
            nwbPhysicsSolveHullDeep(proxyA, originA, rotA, bodyA.positionInvMass.xyz, proxyB, originB, rotB, bodyB.positionInvMass.xyz, hullVertexSlot, maxDist, result);
            g_NwbPhysicsNarrowphaseManifoldBuffers[manifoldSlot][idx] = result;
            return;
        }

        const float sep = dist - totalR;
        if(sep <= maxDist){
            const float3 n = (pB - pA) / dist;
            if(typeA == NWB_PHYSICS_SHAPE_TYPE_CAPSULE || typeB == NWB_PHYSICS_SHAPE_TYPE_CAPSULE){
                const bool hullIsB = (typeB == NWB_PHYSICS_SHAPE_TYPE_HULL);
                const NwbPhysicsGpuShapeProxy pH = hullIsB ? proxyB : proxyA;
                const float4 qH = hullIsB ? rotB : rotA;
                const uint fCount = nwbPhysicsHullFaceCount(pH);
                for(uint fi = 0u; fi < fCount; ++fi){
                    const float4 lp = nwbPhysicsHullLocalFacePlane(pH, hullVertexSlot, fi);
                    const float3 worldFaceN = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(lp.xyz, qH), float3(0.0f, 1.0f, 0.0f));
                    if(abs(dot(worldFaceN, n)) > 0.995f){
                        nwbPhysicsSolveHullDeep(proxyA, originA, rotA, bodyA.positionInvMass.xyz, proxyB, originB, rotB, bodyB.positionInvMass.xyz, hullVertexSlot, maxDist, result);
                        g_NwbPhysicsNarrowphaseManifoldBuffers[manifoldSlot][idx] = result;
                        return;
                    }
                }
            }
            result.pairInfo.z = 1u;
            result.normalPad = float4(n, 0.0f);
            result.anchorAFeature[0] = float4(pA + n * proxyA.localCenterRadius.w - bodyA.positionInvMass.xyz, asfloat(20u));
            result.anchorBSeparation[0] = float4(pB - n * proxyB.localCenterRadius.w - bodyB.positionInvMass.xyz, sep);
        }
        g_NwbPhysicsNarrowphaseManifoldBuffers[manifoldSlot][idx] = result;
        return;
    }

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
            result.anchorAFeature[0] = float4(cA + n * rA - bodyA.positionInvMass.xyz, asfloat(1u));
            result.anchorBSeparation[0] = float4(cB - n * rB - bodyB.positionInvMass.xyz, dist - totalR);
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
            result.anchorAFeature[0] = float4(cA + n * rA - bodyA.positionInvMass.xyz, asfloat(2u));
            result.anchorBSeparation[0] = float4(q - n * rB - bodyB.positionInvMass.xyz, dist - totalR);
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
            result.anchorAFeature[0] = float4(q + n * rA - bodyA.positionInvMass.xyz, asfloat(3u));
            result.anchorBSeparation[0] = float4(cB - n * rB - bodyB.positionInvMass.xyz, dist - totalR);
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
                        result.anchorAFeature[0] = float4(pA0 + n * rA - bodyA.positionInvMass.xyz, asfloat(4u));
                        result.anchorBSeparation[0] = float4(qB0 - n * rB - bodyB.positionInvMass.xyz, sep);
                        result.anchorAFeature[1] = float4(pA1 + n * rA - bodyA.positionInvMass.xyz, asfloat(5u));
                        result.anchorBSeparation[1] = float4(qB1 - n * rB - bodyB.positionInvMass.xyz, sep);
                    }
                }
            }
            if(result.pairInfo.z == 0u){
                result.pairInfo.z = 1u;
                result.anchorAFeature[0] = float4(cA + n * rA - bodyA.positionInvMass.xyz, asfloat(4u));
                result.anchorBSeparation[0] = float4(cB - n * rB - bodyB.positionInvMass.xyz, dist - totalR);
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
            result.anchorAFeature[0] = float4((sphereIsA ? worldSpherePt : finalBoxPt) - bodyA.positionInvMass.xyz, asfloat(5u));
            result.anchorBSeparation[0] = float4((sphereIsA ? finalBoxPt : worldSpherePt) - bodyB.positionInvMass.xyz, separation);
        }
    }
    else if(isBoxA && isBoxB){
        const float3 cA = originA + nwbPhysicsQuatRotate(proxyA.localCenterRadius.xyz, rotA);
        const float4 qA = nwbPhysicsQuatNormalize(nwbPhysicsQuatMul(rotA, proxyA.localRotation));
        const float3 hA = proxyA.localExtentOrP1.xyz;
        const float rA = proxyA.localCenterRadius.w;

        const float3 cB = originB + nwbPhysicsQuatRotate(proxyB.localCenterRadius.xyz, rotB);
        const float4 qB = nwbPhysicsQuatNormalize(nwbPhysicsQuatMul(rotB, proxyB.localRotation));
        const float3 hB = proxyB.localExtentOrP1.xyz;
        const float rB = proxyB.localCenterRadius.w;

        const float3 axA = nwbPhysicsQuatRotate(float3(1.0f, 0.0f, 0.0f), qA);
        const float3 ayA = nwbPhysicsQuatRotate(float3(0.0f, 1.0f, 0.0f), qA);
        const float3 azA = nwbPhysicsQuatRotate(float3(0.0f, 0.0f, 1.0f), qA);
        const float3 axB = nwbPhysicsQuatRotate(float3(1.0f, 0.0f, 0.0f), qB);
        const float3 ayB = nwbPhysicsQuatRotate(float3(0.0f, 1.0f, 0.0f), qB);
        const float3 azB = nwbPhysicsQuatRotate(float3(0.0f, 0.0f, 1.0f), qB);

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
        float3 bestNormal = float3(0.0f, 1.0f, 0.0f);

        for(int i = 0; i < 3; ++i){
            const float signedProj = tArr[i];
            const float rBProj = hB.x * absR[i][0] + hB.y * absR[i][1] + hB.z * absR[i][2];
            const float sep = abs(signedProj) - (halfA[i] + rBProj) - totalR;
            if(sep > maxDist){
                g_NwbPhysicsNarrowphaseManifoldBuffers[manifoldSlot][idx] = result;
                return;
            }
            if(sep > bestSep){
                bestSep = sep;
                bestAxis = i;
                bestNormal = (signedProj < 0.0f) ? -axesA[i] : axesA[i];
            }
        }

        for(int j = 0; j < 3; ++j){
            const float signedProj = tVec.x * R[0][j] + tVec.y * R[1][j] + tVec.z * R[2][j];
            const float rAProj = hA.x * absR[0][j] + hA.y * absR[1][j] + hA.z * absR[2][j];
            const float sep = abs(signedProj) - (rAProj + halfB[j]) - totalR;
            if(sep > maxDist){
                g_NwbPhysicsNarrowphaseManifoldBuffers[manifoldSlot][idx] = result;
                return;
            }
            if(sep > bestSep + 1.0e-4f){
                bestSep = sep;
                bestAxis = 3 + j;
                bestNormal = (signedProj < 0.0f) ? -axesB[j] : axesB[j];
            }
        }

        for(int i = 0; i < 3; ++i){
            const int i1 = (i + 1) % 3;
            const int i2 = (i + 2) % 3;
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
                if(sep > maxDist){
                    g_NwbPhysicsNarrowphaseManifoldBuffers[manifoldSlot][idx] = result;
                    return;
                }
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
            result.anchorAFeature[0] = float4(cEdgeA + bestNormal * rA - bodyA.positionInvMass.xyz, asfloat(uint(100 + bestAxis)));
            result.anchorBSeparation[0] = float4(cEdgeB - bestNormal * rB - bodyB.positionInvMass.xyz, bestSep);
        }
        else{
            const bool refIsA = (bestAxis < 3);
            const float3 refNormal = refIsA ? bestNormal : -bestNormal;
            const float3 refCenter = refIsA ? cA : cB;
            const float3 incCenter = refIsA ? cB : cA;
            const float3 refAxes[3] = { refIsA ? axA : axB, refIsA ? ayA : ayB, refIsA ? azA : azB };
            const float3 incAxes[3] = { refIsA ? axB : axA, refIsA ? ayB : ayA, refIsA ? azB : azA };
            const float refH[3] = { refIsA ? hA.x : hB.x, refIsA ? hA.y : hB.y, refIsA ? hA.z : hB.z };
            const float incH[3] = { refIsA ? hB.x : hA.x, refIsA ? hB.y : hA.y, refIsA ? hB.z : hA.z };

            int refFace = 0;
            float bestRefDot = -2.0f;
            for(int k = 0; k < 3; ++k){
                const float d = dot(refAxes[k], refNormal);
                if(d > bestRefDot){
                    bestRefDot = d;
                    refFace = k * 2;
                }
                if(-d > bestRefDot){
                    bestRefDot = -d;
                    refFace = k * 2 + 1;
                }
            }

            int incFace = 0;
            float bestIncDot = 2.0f;
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

            float3 poly0[8];
            float3 poly1[8];
            for(uint p = 0u; p < 8u; ++p){
                poly0[p] = float3(0.0f, 0.0f, 0.0f);
                poly1[p] = float3(0.0f, 0.0f, 0.0f);
            }
            float3 incVerts[4];
            nwbPhysicsGetBoxFace(incCenter, incAxes[0], incAxes[1], incAxes[2], float3(incH[0], incH[1], incH[2]), incFace, incVerts);
            for(uint p = 0u; p < 4u; ++p)
                poly0[p] = incVerts[p];
            uint polyCount = 4u;

            const int refAxisIdx = refFace / 2;
            const int uIdx = (refAxisIdx + 1) % 3;
            const int vIdx = (refAxisIdx + 2) % 3;
            const float3 uAxis = refAxes[uIdx];
            const float3 vAxis = refAxes[vIdx];
            const float refPlaneDist = dot(refAxes[refAxisIdx] * ((refFace % 2 == 0) ? 1.0f : -1.0f), refCenter) + refH[refAxisIdx];
            const float3 clipRefN = refAxes[refAxisIdx] * ((refFace % 2 == 0) ? 1.0f : -1.0f);

            polyCount = nwbPhysicsClipPolygonPlane(poly0, polyCount, uAxis, dot(uAxis, refCenter) + refH[uIdx], poly1);
            polyCount = nwbPhysicsClipPolygonPlane(poly1, polyCount, -uAxis, dot(-uAxis, refCenter) + refH[uIdx], poly0);
            polyCount = nwbPhysicsClipPolygonPlane(poly0, polyCount, vAxis, dot(vAxis, refCenter) + refH[vIdx], poly1);
            polyCount = nwbPhysicsClipPolygonPlane(poly1, polyCount, -vAxis, dot(-vAxis, refCenter) + refH[vIdx], poly0);

            float3 candWRef[8];
            float3 candWInc[8];
            float3 candPos[8];
            float candSep[8];
            uint candFeat[8];
            uint candCount = 0u;
            for(uint p = 0u; p < polyCount && candCount < 8u; ++p){
                const float sep = dot(clipRefN, poly0[p]) - refPlaneDist - totalR;
                if(sep <= maxDist){
                    candWRef[candCount] = poly0[p] - clipRefN * (sep + totalR - (refIsA ? rA : rB));
                    candWInc[candCount] = poly0[p] - clipRefN * (refIsA ? rB : rA);
                    candPos[candCount] = poly0[p];
                    candSep[candCount] = sep;
                    candFeat[candCount] = uint(200u + p);
                    ++candCount;
                }
            }

            result.normalPad = float4(bestNormal, 0.0f);
            nwbPhysicsReduceManifoldTo4(clipRefN, candWRef, candWInc, candPos, candSep, candFeat, candCount, refIsA, bodyA.positionInvMass.xyz, bodyB.positionInvMass.xyz, result);
        }
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
        if(bestDistSq > 1.0e-10f){
            const float dist = sqrt(bestDistSq);
            outLocalN = (bestSegPt - bestBoxPt) / dist;
            singleSep = dist - totalR;
        }
        else{
            const float3 pDeep = bestSegPt;
            const float3 dx = hBox - abs(pDeep);
            if(dx.x <= dx.y && dx.x <= dx.z){
                outLocalN = float3(pDeep.x >= 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f);
                singleSep = -dx.x - totalR;
                localBoxSurface = float3(outLocalN.x * hBox.x, pDeep.y, pDeep.z);
            }
            else if(dx.y <= dx.z){
                outLocalN = float3(0.0f, pDeep.y >= 0.0f ? 1.0f : -1.0f, 0.0f);
                singleSep = -dx.y - totalR;
                localBoxSurface = float3(pDeep.x, outLocalN.y * hBox.y, pDeep.z);
            }
            else{
                outLocalN = float3(0.0f, 0.0f, pDeep.z >= 0.0f ? 1.0f : -1.0f);
                singleSep = -dx.z - totalR;
                localBoxSurface = float3(pDeep.x, pDeep.y, outLocalN.z * hBox.z);
            }
        }

        if(singleSep <= maxDist){
            const float3 outWorldN = nwbPhysicsQuatRotate(outLocalN, qBox);
            const float3 nAtoB = capIsA ? -outWorldN : outWorldN;
            result.normalPad = float4(nAtoB, 0.0f);

            const float axX = abs(outLocalN.x);
            const float axY = abs(outLocalN.y);
            const float axZ = abs(outLocalN.z);
            uint emitted = 0u;
            if(max(axX, max(axY, axZ)) > 0.9f){
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
                    const uint candCount = (tMax - tMin) > 1.0e-3f ? 2u : 1u;
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
                            result.anchorAFeature[emitted] = float4((capIsA ? capSurf : boxSurf) - bodyA.positionInvMass.xyz, asfloat(feat));
                            result.anchorBSeparation[emitted] = float4((capIsA ? boxSurf : capSurf) - bodyB.positionInvMass.xyz, ptSep);
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
                result.anchorAFeature[0] = float4((capIsA ? capSurf : boxSurf) - bodyA.positionInvMass.xyz, asfloat(6u));
                result.anchorBSeparation[0] = float4((capIsA ? boxSurf : capSurf) - bodyB.positionInvMass.xyz, singleSep);
                emitted = 1u;
            }
            result.pairInfo.z = emitted;
        }
    }
    else{
        result.pairInfo.w = 1u;
    }

    g_NwbPhysicsNarrowphaseManifoldBuffers[manifoldSlot][idx] = result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

