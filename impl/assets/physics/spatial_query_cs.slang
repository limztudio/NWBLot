// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "physics_common.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


float3 nwbPhysicsQuerySafeNormal(const float3 v, const float3 fallback){
    const float lenSq = dot(v, v);
    if(lenSq > 1.0e-12f)
        return v * rsqrt(lenSq);
    return fallback;
}

bool nwbPhysicsQueryAabbOverlaps(const float3 minA, const float3 maxA, const float3 minB, const float3 maxB){
    return all(minA <= maxB) && all(minB <= maxA);
}

bool nwbPhysicsQueryRayAabb(
    const float3 origin,
    const float3 invDir,
    const float3 bMin,
    const float3 bMax,
    const float maxFrac,
    out float outT,
    out float3 outNormal
){
    const float3 t0 = (bMin - origin) * invDir;
    const float3 t1 = (bMax - origin) * invDir;
    const float3 tMinVec = min(t0, t1);
    const float3 tMaxVec = max(t0, t1);

    float tNear = 0.0f;
    float tFar = maxFrac;
    int hitAxis = -1;
    if(tMinVec.x > tNear){
        tNear = tMinVec.x;
        hitAxis = 0;
    }
    if(tMinVec.y > tNear){
        tNear = tMinVec.y;
        hitAxis = 1;
    }
    if(tMinVec.z > tNear){
        tNear = tMinVec.z;
        hitAxis = 2;
    }
    tFar = min(tFar, min(tMaxVec.x, min(tMaxVec.y, tMaxVec.z)));
    if(tNear > tFar){
        outT = 0.0f;
        outNormal = float3(0.0f, 1.0f, 0.0f);
        return false;
    }
    outT = tNear;
    if(hitAxis == 0)
        outNormal = float3(invDir.x < 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f);
    else if(hitAxis == 1)
        outNormal = float3(0.0f, invDir.y < 0.0f ? 1.0f : -1.0f, 0.0f);
    else if(hitAxis == 2)
        outNormal = float3(0.0f, 0.0f, invDir.z < 0.0f ? 1.0f : -1.0f);
    else
        outNormal = float3(0.0f, 1.0f, 0.0f);
    return true;
}

bool nwbPhysicsQueryRayAabbFast(
    const float3 origin,
    const float3 invDir,
    const float3 bMin,
    const float3 bMax,
    const float maxFrac,
    out float outT
){
    const float3 t0 = (bMin - origin) * invDir;
    const float3 t1 = (bMax - origin) * invDir;
    const float3 tMinVec = min(t0, t1);
    const float3 tMaxVec = max(t0, t1);
    const float tNear = max(0.0f, max(tMinVec.x, max(tMinVec.y, tMinVec.z)));
    const float tFar = min(maxFrac, min(tMaxVec.x, min(tMaxVec.y, tMaxVec.z)));
    outT = tNear;
    return tNear <= tFar;
}

bool nwbPhysicsQueryRaySphere(
    const float3 origin,
    const float3 dir,
    const float maxFrac,
    const float3 center,
    const float radius,
    out float outT,
    out float3 outNormal
){
    const float3 m = origin - center;
    const float b = dot(m, dir);
    const float c = dot(m, m) - radius * radius;
    const float a = dot(dir, dir);
    if(a <= 1.0e-12f || (c > 0.0f && b > 0.0f)){
        outT = 0.0f;
        outNormal = float3(0.0f, 1.0f, 0.0f);
        return false;
    }
    const float disc = b * b - a * c;
    if(disc < 0.0f){
        outT = 0.0f;
        outNormal = float3(0.0f, 1.0f, 0.0f);
        return false;
    }
    float t = (-b - sqrt(disc)) / a;
    if(t < 0.0f){
        if(c <= 0.0f){
            outT = 0.0f;
            outNormal = nwbPhysicsQuerySafeNormal(m, nwbPhysicsQuerySafeNormal(-dir, float3(0.0f, 1.0f, 0.0f)));
            return true;
        }
        outT = 0.0f;
        outNormal = float3(0.0f, 1.0f, 0.0f);
        return false;
    }
    if(t > maxFrac){
        outT = 0.0f;
        outNormal = float3(0.0f, 1.0f, 0.0f);
        return false;
    }
    outT = t;
    outNormal = nwbPhysicsQuerySafeNormal((origin + dir * t) - center, nwbPhysicsQuerySafeNormal(-dir, float3(0.0f, 1.0f, 0.0f)));
    return true;
}

bool nwbPhysicsQueryRayCapsule(
    const float3 origin,
    const float3 dir,
    const float maxFrac,
    const float3 p0,
    const float3 p1,
    const float radius,
    out float outT,
    out float3 outNormal
){
    const float3 d = p1 - p0;
    const float dLenSq = dot(d, d);
    if(dLenSq <= 1.0e-10f)
        return nwbPhysicsQueryRaySphere(origin, dir, maxFrac, (p0 + p1) * 0.5f, radius, outT, outNormal);

    const float tProj = clamp(dot(origin - p0, d) / dLenSq, 0.0f, 1.0f);
    const float3 closestPt = p0 + d * tProj;
    const float3 toOrig = origin - closestPt;
    const float distSqStart = dot(toOrig, toOrig);
    if(distSqStart <= radius * radius){
        outT = 0.0f;
        if(distSqStart > 1.0e-12f){
            outNormal = toOrig * rsqrt(distSqStart);
        }
        else{
            const float3 axis0 = d * rsqrt(dLenSq);
            float3 radial = cross(axis0, float3(0.0f, 1.0f, 0.0f));
            if(dot(radial, radial) < 1.0e-6f)
                radial = cross(axis0, float3(1.0f, 0.0f, 0.0f));
            outNormal = nwbPhysicsQuerySafeNormal(radial, float3(0.0f, 1.0f, 0.0f));
        }
        return true;
    }

    bool anyHit = false;
    float bestT = maxFrac;
    float3 bestN = float3(0.0f, 1.0f, 0.0f);

    float tCap = 0.0f;
    float3 nCap = float3(0.0f, 1.0f, 0.0f);
    if(nwbPhysicsQueryRaySphere(origin, dir, bestT, p0, radius, tCap, nCap)){
        anyHit = true;
        bestT = tCap;
        bestN = nCap;
    }
    if(nwbPhysicsQueryRaySphere(origin, dir, bestT, p1, radius, tCap, nCap)){
        anyHit = true;
        bestT = tCap;
        bestN = nCap;
    }

    const float3 axis = d * rsqrt(dLenSq);
    const float3 m = origin - p0;
    const float mDotA = dot(m, axis);
    const float dirDotA = dot(dir, axis);
    const float3 mPerp = m - axis * mDotA;
    const float3 dirPerp = dir - axis * dirDotA;
    const float a = dot(dirPerp, dirPerp);
    const float b = dot(mPerp, dirPerp);
    const float c = dot(mPerp, mPerp) - radius * radius;
    if(a > 1.0e-12f){
        const float disc = b * b - a * c;
        if(disc >= 0.0f){
            const float sqrtDisc = sqrt(disc);
            const float t = (-b - sqrtDisc) / a;
            if(t >= 0.0f && t <= bestT){
                const float s = mDotA + t * dirDotA;
                const float dLen = sqrt(dLenSq);
                if(s >= 0.0f && s <= dLen){
                    const float3 hitP = origin + dir * t;
                    const float3 axisPt = p0 + axis * s;
                    anyHit = true;
                    bestT = t;
                    bestN = nwbPhysicsQuerySafeNormal(hitP - axisPt, float3(0.0f, 1.0f, 0.0f));
                }
            }
        }
    }

    outT = bestT;
    outNormal = bestN;
    return anyHit;
}

float3 nwbPhysicsClosestPointOnSegment(const float3 p, const float3 a, const float3 b){
    const float3 ab = b - a;
    const float lenSq = dot(ab, ab);
    if(lenSq <= 1.0e-12f)
        return a;
    const float t = clamp(dot(p - a, ab) / lenSq, 0.0f, 1.0f);
    return a + ab * t;
}

float nwbPhysicsQueryAabbDistSq(const float3 p, const float3 bMin, const float3 bMax){
    const float3 clamped = clamp(p, bMin, bMax);
    const float3 d = p - clamped;
    return dot(d, d);
}

bool nwbPhysicsQueryPassesFilter(const uint qCat, const uint qMask, const uint qFlags, const uint4 shapeFilter){
    if((qFlags & NWB_PHYSICS_QUERY_FLAG_USE_GROUP_INDEX) != 0u){
        const int qGroup = int(qFlags) >> 16;
        const int sGroup = asint(shapeFilter.z);
        if(qGroup != 0 && sGroup == qGroup)
            return qGroup > 0;
    }
    return ((qCat & shapeFilter.y) != 0u) && ((shapeFilter.x & qMask) != 0u);
}

float3 nwbPhysicsQueryClosestPtTriangle(const float3 p, const float3 a, const float3 b, const float3 c){
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

bool nwbPhysicsQueryRayTriangle(
    const float3 origin,
    const float3 dir,
    const float3 v0,
    const float3 v1,
    const float3 v2,
    const bool cullBackfaces,
    const float maxT,
    out float outT,
    out float3 outNormal
){
    outT = 0.0f;
    outNormal = float3(0.0f, 1.0f, 0.0f);
    const float3 e1 = v1 - v0;
    const float3 e2 = v2 - v0;
    const float3 faceCross = cross(e1, e2);
    if(cullBackfaces && dot(faceCross, dir) > 0.0f)
        return false;

    const float3 h = cross(dir, e2);
    const float det = dot(e1, h);
    if(abs(det) <= 1.0e-12f)
        return false;

    const float invDet = 1.0f / det;
    const float3 s = origin - v0;
    const float u = invDet * dot(s, h);
    if(u < -1.0e-5f || u > 1.00001f)
        return false;

    const float3 q = cross(s, e1);
    const float v = invDet * dot(dir, q);
    if(v < -1.0e-5f || (u + v) > 1.00001f)
        return false;

    const float t = invDet * dot(e2, q);
    if(t < 0.0f || t > maxT)
        return false;

    float3 n = nwbPhysicsQuerySafeNormal(faceCross, float3(0.0f, 1.0f, 0.0f));
    if(dot(n, dir) > 0.0f)
        n = -n;
    outT = t;
    outNormal = n;
    return true;
}

bool nwbPhysicsQueryRayTriangleParity(
    const float3 origin,
    const float3 dir,
    const float3 v0,
    const float3 v1,
    const float3 v2
){
    const float3 e1 = v1 - v0;
    const float3 e2 = v2 - v0;
    const float3 h = cross(dir, e2);
    const float det = dot(e1, h);
    if(abs(det) <= 1.0e-12f)
        return false;

    const float invDet = 1.0f / det;
    const float3 s = origin - v0;
    const float u = invDet * dot(s, h);
    if(u < -1.0e-5f || u > 1.00001f)
        return false;

    const float3 q = cross(s, e1);
    const float v = invDet * dot(dir, q);
    if(v < -1.0e-5f || (u + v) > 1.00001f)
        return false;

    const float t = invDet * dot(e2, q);
    return t >= 0.0f && t <= 1.0e20f;
}

bool nwbPhysicsQueryRaySegmentZero(
    const float3 origin,
    const float3 dir,
    const float maxFrac,
    const float3 p0,
    const float3 p1,
    out float outT,
    out float3 outNormal
){
    outT = 0.0f;
    outNormal = float3(0.0f, 1.0f, 0.0f);
    const float3 u = p1 - p0;
    const float3 w0 = origin - p0;
    const float aLenSq = dot(dir, dir);
    const float bDot = dot(dir, u);
    const float cLenSq = dot(u, u);
    const float dDot = dot(dir, w0);
    const float eDot = dot(u, w0);
    const float denom = aLenSq * cLenSq - bDot * bDot;
    float sc = 0.0f;
    float tc = 0.0f;
    if(denom > 1.0e-12f){
        sc = clamp((bDot * eDot - cLenSq * dDot) / denom, 0.0f, maxFrac);
        tc = clamp((aLenSq * eDot - bDot * dDot) / denom, 0.0f, 1.0f);
        sc = clamp((bDot * tc - dDot) / max(aLenSq, 1.0e-12f), 0.0f, maxFrac);
    }
    const float3 rayPt = origin + dir * sc;
    const float3 segPt = p0 + u * tc;
    const float3 diff = rayPt - segPt;
    if(dot(diff, diff) <= 1.0e-6f){
        outT = sc;
        outNormal = nwbPhysicsQuerySafeNormal(diff, nwbPhysicsQuerySafeNormal(-dir, float3(0.0f, 1.0f, 0.0f)));
        return true;
    }
    return false;
}

bool nwbPhysicsQueryRayPlane(
    const float3 origin,
    const float3 dir,
    const float maxFrac,
    const float3 pPlane,
    const float3 t1W,
    const float3 t2W,
    const float3 worldN,
    const bool isFinitePlane,
    const float hx,
    const float hz,
    out float outT,
    out float3 outNormal
){
    outT = 0.0f;
    outNormal = worldN;
    const float signed0 = dot(worldN, origin - pPlane);
    if(signed0 <= 0.0f){
        if(isFinitePlane){
            const float3 rel0 = origin - pPlane;
            if(abs(dot(rel0, t1W)) > hx || abs(dot(rel0, t2W)) > hz)
                return false;
        }
        outT = 0.0f;
        outNormal = worldN;
        return true;
    }

    const float denom = dot(worldN, dir);
    if(denom >= -1.0e-8f)
        return false;
    const float t = -signed0 / denom;
    if(t < 0.0f || t > maxFrac)
        return false;

    if(isFinitePlane){
        const float3 rel = (origin + dir * t) - pPlane;
        if(abs(dot(rel, t1W)) > hx || abs(dot(rel, t2W)) > hz)
            return false;
    }
    outT = t;
    outNormal = worldN;
    return true;
}

bool nwbPhysicsQueryRayCylinderSharp(
    const float3 origin,
    const float3 dir,
    const float maxFrac,
    const float3 center,
    const float4 worldQ,
    const float r,
    const float hh,
    out float outT,
    out float3 outNormal
){
    outT = 0.0f;
    outNormal = float3(0.0f, 1.0f, 0.0f);
    const float3 localOrig = nwbPhysicsQuatInvRotate(origin - center, worldQ);
    const float3 localDir = nwbPhysicsQuatInvRotate(dir, worldQ);
    const float radial0Sq = localOrig.x * localOrig.x + localOrig.z * localOrig.z;
    if(radial0Sq <= r * r && abs(localOrig.y) <= hh){
        const float radial = sqrt(radial0Sq);
        const float toSide = r - radial;
        const float toCap = hh - abs(localOrig.y);
        float3 localN = float3(0.0f, 1.0f, 0.0f);
        if(toCap <= toSide)
            localN = float3(0.0f, localOrig.y >= 0.0f ? 1.0f : -1.0f, 0.0f);
        else if(radial > 1.0e-12f)
            localN = float3(localOrig.x / radial, 0.0f, localOrig.z / radial);
        outT = 0.0f;
        outNormal = nwbPhysicsQuerySafeNormal(nwbPhysicsQuatRotate(localN, worldQ), float3(0.0f, 1.0f, 0.0f));
        return true;
    }

    float tMin = 0.0f;
    float tMax = maxFrac;
    float3 enterN = float3(0.0f, 1.0f, 0.0f);
    bool hasEnter = false;

    if(abs(localDir.y) <= 1.0e-12f){
        if(abs(localOrig.y) > hh)
            return false;
    }
    else{
        float ty0 = (-hh - localOrig.y) / localDir.y;
        float ty1 = (hh - localOrig.y) / localDir.y;
        float3 ny0 = float3(0.0f, -1.0f, 0.0f);
        if(ty0 > ty1){
            const float tmp = ty0;
            ty0 = ty1;
            ty1 = tmp;
            ny0 = float3(0.0f, 1.0f, 0.0f);
        }
        if(ty0 > tMin){
            tMin = ty0;
            enterN = ny0;
            hasEnter = true;
        }
        if(ty1 < tMax)
            tMax = ty1;
        if(tMin > tMax)
            return false;
    }

    const float a = localDir.x * localDir.x + localDir.z * localDir.z;
    const float b = localOrig.x * localDir.x + localOrig.z * localDir.z;
    const float c = radial0Sq - r * r;
    if(a <= 1.0e-12f){
        if(c > 0.0f)
            return false;
    }
    else{
        const float discr = b * b - a * c;
        if(discr < 0.0f)
            return false;
        const float sqrtD = sqrt(discr);
        const float tr0 = (-b - sqrtD) / a;
        const float tr1 = (-b + sqrtD) / a;
        if(tr0 > tMin){
            tMin = tr0;
            const float hx = localOrig.x + localDir.x * tr0;
            const float hz = localOrig.z + localDir.z * tr0;
            enterN = nwbPhysicsQuerySafeNormal(float3(hx, 0.0f, hz), float3(1.0f, 0.0f, 0.0f));
            hasEnter = true;
        }
        if(tr1 < tMax)
            tMax = tr1;
        if(tMin > tMax)
            return false;
    }

    if(hasEnter && tMin >= 0.0f && tMin <= maxFrac){
        outT = tMin;
        outNormal = nwbPhysicsQuerySafeNormal(nwbPhysicsQuatRotate(enterN, worldQ), float3(0.0f, 1.0f, 0.0f));
        return true;
    }
    return false;
}

bool nwbPhysicsQueryRayHull(
    const uint hullVertexSlot,
    const NwbPhysicsGpuShapeProxy spProxy,
    const float4 bodyRot,
    const float3 localOrig,
    const float3 localDir,
    const float maxFrac,
    out float outT,
    out float3 outNormal,
    out bool outNeedsFallback
){
    outT = 0.0f;
    outNormal = float3(0.0f, 1.0f, 0.0f);
    outNeedsFallback = false;
    const uint vOffset = spProxy.filterData.w;
    if(hullVertexSlot == NWB_PHYSICS_INVALID_INDEX || vOffset == NWB_PHYSICS_INVALID_INDEX)
        return false;

    const uint metaPack = asuint(spProxy.localRotationOrMeta.x);
    const uint vCount = metaPack & 0xffu;
    const uint fCount = (metaPack >> 8u) & 0xffu;
    if(vCount == 0u || fCount == 0u || fCount > NWB_PHYSICS_MAX_HULL_FACES)
        return false;

    const float skinR = max(spProxy.localCenterRadius.w, 0.0f);
    float tLower = 0.0f;
    float tUpper = maxFrac;
    uint hitFace = NWB_PHYSICS_INVALID_INDEX;
    float3 hitLocalN = float3(0.0f, 1.0f, 0.0f);
    float maxSignedDist = -1.0e20f;
    float3 shallowFaceN = float3(0.0f, 1.0f, 0.0f);

    for(uint fi = 0u; fi < fCount; ++fi){
        const float4 pl = g_NwbPhysicsFloat4Buffers[hullVertexSlot][vOffset + vCount + fi];
        const float effOffset = pl.w + skinR;
        const float signedDist = dot(pl.xyz, localOrig) - effOffset;
        if(signedDist > maxSignedDist){
            maxSignedDist = signedDist;
            shallowFaceN = pl.xyz;
        }
        const float denom = dot(pl.xyz, localDir);
        const float numer = -signedDist;
        if(abs(denom) <= 1.0e-12f){
            if(numer < 0.0f)
                return false;
        }
        else{
            const float ratio = numer / denom;
            if(denom < 0.0f){
                if(ratio > tLower){
                    tLower = ratio;
                    hitFace = fi;
                    hitLocalN = pl.xyz;
                }
            }
            else if(ratio < tUpper){
                tUpper = ratio;
            }
            if(tLower > tUpper)
                return false;
        }
    }

    if(tLower < 0.0f || tLower > maxFrac)
        return false;
    if(skinR > 1.0e-5f){
        if((hitFace == NWB_PHYSICS_INVALID_INDEX && maxSignedDist <= 0.0f) || hitFace != NWB_PHYSICS_INVALID_INDEX){
            outNeedsFallback = true;
            return false;
        }
    }
    if(hitFace == NWB_PHYSICS_INVALID_INDEX && maxSignedDist <= 0.0f){
        outT = 0.0f;
        outNormal = nwbPhysicsQuerySafeNormal(nwbPhysicsQuatRotate(shallowFaceN, bodyRot), float3(0.0f, 1.0f, 0.0f));
        return true;
    }
    if(hitFace != NWB_PHYSICS_INVALID_INDEX){
        outT = tLower;
        outNormal = nwbPhysicsQuerySafeNormal(nwbPhysicsQuatRotate(hitLocalN, bodyRot), float3(0.0f, 1.0f, 0.0f));
        return true;
    }
    return false;
}

bool nwbPhysicsQueryIsInsideClosedMesh(
    const uint nodeBase,
    const uint triBase,
    const uint meshNodeSlot,
    const uint meshTriSlot,
    const float3 localPoint,
    out bool outOverflow,
    inout uint stack[32]
){
    outOverflow = false;
    const float4 rootN0 = g_NwbPhysicsFloat4Buffers[meshNodeSlot][nodeBase * 2u + 0u];
    const float4 rootN1 = g_NwbPhysicsFloat4Buffers[meshNodeSlot][nodeBase * 2u + 1u];
    if(any(localPoint < rootN0.xyz) || any(localPoint > rootN1.xyz))
        return false;

    const float3 rayDir = float3(1.0f, 0.131f, 0.071f);
    const float3 invDir = float3(1.0f, 1.0f / 0.131f, 1.0f / 0.071f);
    stack[0] = 0u;
    uint sp = 1u;
    uint hitCount = 0u;

    while(sp > 0u){
        const uint ni = stack[--sp];
        const float4 n0 = g_NwbPhysicsFloat4Buffers[meshNodeSlot][(nodeBase + ni) * 2u + 0u];
        const float4 n1 = g_NwbPhysicsFloat4Buffers[meshNodeSlot][(nodeBase + ni) * 2u + 1u];
        const float3 t0 = (n0.xyz - localPoint) * invDir;
        const float3 t1 = (n1.xyz - localPoint) * invDir;
        const float3 tMin = min(t0, t1);
        const float3 tMax = max(t0, t1);
        const float enterT = max(max(tMin.x, tMin.y), max(tMin.z, 0.0f));
        const float exitT = min(min(tMax.x, tMax.y), tMax.z);
        if(exitT < enterT)
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
                if(nwbPhysicsQueryRayTriangleParity(localPoint, rayDir, tv0, tv1, tv2))
                    ++hitCount;
            }
        }
        else{
            if(sp + 2u > 32u){
                outOverflow = true;
                return false;
            }
            stack[sp++] = asuint(n0.w);
            stack[sp++] = meta & 0x7fffffffu;
        }
    }
    return (hitCount & 1u) != 0u;
}

bool nwbPhysicsQueryMeshClosestOutwardNormal(
    const uint nodeBase,
    const uint triBase,
    const uint meshNodeSlot,
    const uint meshTriSlot,
    const float3 localPoint,
    out float3 outNormalLocal,
    out bool outOverflow,
    inout uint stack[32],
    inout float stackMetric[32]
){
    outNormalLocal = float3(0.0f, 1.0f, 0.0f);
    outOverflow = false;
    bool found = false;
    float bestDistSq = 1.0e30f;
    float3 bestCross = float3(0.0f, 1.0f, 0.0f);
    stack[0] = 0u;
    stackMetric[0] = 0.0f;
    uint sp = 1u;

    while(sp > 0u){
        --sp;
        if(stackMetric[sp] > bestDistSq)
            continue;
        const uint ni = stack[sp];
        const float4 n0 = g_NwbPhysicsFloat4Buffers[meshNodeSlot][(nodeBase + ni) * 2u + 0u];
        const float4 n1 = g_NwbPhysicsFloat4Buffers[meshNodeSlot][(nodeBase + ni) * 2u + 1u];
        const uint meta = asuint(n1.w);
        if((meta & 0x80000000u) != 0u){
            const uint firstTri = asuint(n0.w);
            const uint triCount = meta & 0x7fffffffu;
            for(uint ti = 0u; ti < triCount; ++ti){
                const uint triIdx = triBase + firstTri + ti;
                const float3 tv0 = g_NwbPhysicsFloat4Buffers[meshTriSlot][triIdx * 3u + 0u].xyz;
                const float3 tv1 = g_NwbPhysicsFloat4Buffers[meshTriSlot][triIdx * 3u + 1u].xyz;
                const float3 tv2 = g_NwbPhysicsFloat4Buffers[meshTriSlot][triIdx * 3u + 2u].xyz;
                const float3 closePt = nwbPhysicsQueryClosestPtTriangle(localPoint, tv0, tv1, tv2);
                const float3 diff = localPoint - closePt;
                const float dSq = dot(diff, diff);
                if(dSq < bestDistSq){
                    bestDistSq = dSq;
                    found = true;
                    bestCross = cross(tv1 - tv0, tv2 - tv0);
                }
            }
        }
        else{
            const uint lIdx = asuint(n0.w);
            const uint rIdx = meta & 0x7fffffffu;
            const float4 ln0 = g_NwbPhysicsFloat4Buffers[meshNodeSlot][(nodeBase + lIdx) * 2u + 0u];
            const float4 ln1 = g_NwbPhysicsFloat4Buffers[meshNodeSlot][(nodeBase + lIdx) * 2u + 1u];
            const float4 rn0 = g_NwbPhysicsFloat4Buffers[meshNodeSlot][(nodeBase + rIdx) * 2u + 0u];
            const float4 rn1 = g_NwbPhysicsFloat4Buffers[meshNodeSlot][(nodeBase + rIdx) * 2u + 1u];
            const float dL = nwbPhysicsQueryAabbDistSq(localPoint, ln0.xyz, ln1.xyz);
            const float dR = nwbPhysicsQueryAabbDistSq(localPoint, rn0.xyz, rn1.xyz);
            const bool keepL = dL <= bestDistSq;
            const bool keepR = dR <= bestDistSq;
            if((keepL ? 1u : 0u) + (keepR ? 1u : 0u) + sp > 32u){
                outOverflow = true;
                return false;
            }
            if(keepL && keepR){
                if(dL < dR){
                    stack[sp] = rIdx; stackMetric[sp] = dR; ++sp;
                    stack[sp] = lIdx; stackMetric[sp] = dL; ++sp;
                }
                else{
                    stack[sp] = lIdx; stackMetric[sp] = dL; ++sp;
                    stack[sp] = rIdx; stackMetric[sp] = dR; ++sp;
                }
            }
            else if(keepL){
                stack[sp] = lIdx; stackMetric[sp] = dL; ++sp;
            }
            else if(keepR){
                stack[sp] = rIdx; stackMetric[sp] = dR; ++sp;
            }
        }
    }
    if(found)
        outNormalLocal = nwbPhysicsQuerySafeNormal(bestCross, float3(0.0f, 1.0f, 0.0f));
    return found;
}

bool nwbPhysicsQueryRayMesh(
    const uint meshNodeSlot,
    const uint meshTriSlot,
    const NwbPhysicsGpuShapeProxy spProxy,
    const float4 bodyRot,
    const uint qFlags,
    const float3 localOrig,
    const float3 localDir,
    const float maxFrac,
    out float outT,
    out float3 outNormal,
    out bool outNeedsFallback,
    inout uint stack[32],
    inout float stackMetric[32]
){
    outT = maxFrac;
    outNormal = float3(0.0f, 1.0f, 0.0f);
    outNeedsFallback = false;
    const uint nodeBase = spProxy.filterData.w;
    const uint triBase = asuint(spProxy.localRotationOrMeta.x);
    if(meshNodeSlot == NWB_PHYSICS_INVALID_INDEX || meshTriSlot == NWB_PHYSICS_INVALID_INDEX || nodeBase == NWB_PHYSICS_INVALID_INDEX || triBase == NWB_PHYSICS_INVALID_INDEX){
        outNeedsFallback = true;
        return false;
    }

    const float meshRadius = max(spProxy.localCenterRadius.w, 0.0f);
    const float3 padVec = float3(meshRadius, meshRadius, meshRadius);
    const float4 firstTri1 = g_NwbPhysicsFloat4Buffers[meshTriSlot][triBase * 3u + 1u];
    const uint meshFlags = asuint(firstTri1.w);
    const bool closedMesh = (meshFlags & NWB_PHYSICS_MESH_FLAG_CLOSED) != 0u;
    const bool cullBackfaces = ((meshFlags & NWB_PHYSICS_MESH_FLAG_CULL_BACKFACES) != 0u) || ((qFlags & NWB_PHYSICS_QUERY_FLAG_CULL_BACKFACES) != 0u);
    if(closedMesh){
        bool insideOverflow = false;
        if(nwbPhysicsQueryIsInsideClosedMesh(nodeBase, triBase, meshNodeSlot, meshTriSlot, localOrig, insideOverflow, stack)){
            if(meshRadius > 1.0e-5f){
                outNeedsFallback = true;
                return false;
            }
            float3 closeNLocal = float3(0.0f, 1.0f, 0.0f);
            bool closeOverflow = false;
            const bool foundClosest = nwbPhysicsQueryMeshClosestOutwardNormal(nodeBase, triBase, meshNodeSlot, meshTriSlot, localOrig, closeNLocal, closeOverflow, stack, stackMetric);
            if(closeOverflow){
                outNeedsFallback = true;
                return false;
            }
            outT = 0.0f;
            if(foundClosest)
                outNormal = nwbPhysicsQuerySafeNormal(nwbPhysicsQuatRotate(closeNLocal, bodyRot), float3(0.0f, 1.0f, 0.0f));
            else
                outNormal = nwbPhysicsQuerySafeNormal(-nwbPhysicsQuatRotate(localDir, bodyRot), float3(0.0f, 1.0f, 0.0f));
            return true;
        }
        if(insideOverflow){
            outNeedsFallback = true;
            return false;
        }
    }

    const float3 localInvDir = float3(
        abs(localDir.x) > 1.0e-12f ? (1.0f / localDir.x) : 1.0e30f,
        abs(localDir.y) > 1.0e-12f ? (1.0f / localDir.y) : 1.0e30f,
        abs(localDir.z) > 1.0e-12f ? (1.0f / localDir.z) : 1.0e30f
    );
    uint sp = 0u;
    const float4 rootN0 = g_NwbPhysicsFloat4Buffers[meshNodeSlot][nodeBase * 2u + 0u];
    const float4 rootN1 = g_NwbPhysicsFloat4Buffers[meshNodeSlot][nodeBase * 2u + 1u];
    float rootT = 0.0f;
    if(nwbPhysicsQueryRayAabbFast(localOrig, localInvDir, rootN0.xyz - padVec, rootN1.xyz + padVec, maxFrac, rootT)){
        stack[0] = 0u;
        stackMetric[0] = rootT;
        sp = 1u;
    }

    bool anyHit = false;
    float bestT = maxFrac;
    float3 bestLocalN = float3(0.0f, 1.0f, 0.0f);

    while(sp > 0u){
        --sp;
        if(stackMetric[sp] > bestT)
            continue;
        const uint ni = stack[sp];
        const float4 n0 = g_NwbPhysicsFloat4Buffers[meshNodeSlot][(nodeBase + ni) * 2u + 0u];
        const float4 n1 = g_NwbPhysicsFloat4Buffers[meshNodeSlot][(nodeBase + ni) * 2u + 1u];
        const uint meta = asuint(n1.w);
        if((meta & 0x80000000u) != 0u){
            const uint firstTri = asuint(n0.w);
            const uint triCount = meta & 0x7fffffffu;
            for(uint ti = 0u; ti < triCount; ++ti){
                const uint triIdx = triBase + firstTri + ti;
                const float3 tv0 = g_NwbPhysicsFloat4Buffers[meshTriSlot][triIdx * 3u + 0u].xyz;
                const float3 tv1 = g_NwbPhysicsFloat4Buffers[meshTriSlot][triIdx * 3u + 1u].xyz;
                const float3 tv2 = g_NwbPhysicsFloat4Buffers[meshTriSlot][triIdx * 3u + 2u].xyz;
                if(meshRadius > 1.0e-5f){
                    const float3 triMin = min(tv0, min(tv1, tv2)) - padVec;
                    const float3 triMax = max(tv0, max(tv1, tv2)) + padVec;
                    float triBoxT = 0.0f;
                    if(nwbPhysicsQueryRayAabbFast(localOrig, localInvDir, triMin, triMax, bestT, triBoxT)){
                        outNeedsFallback = true;
                        return false;
                    }
                    continue;
                }
                float tTri = 0.0f;
                float3 nTri = float3(0.0f, 1.0f, 0.0f);
                if(nwbPhysicsQueryRayTriangle(localOrig, localDir, tv0, tv1, tv2, cullBackfaces, bestT, tTri, nTri)){
                    if(!anyHit || tTri < bestT){
                        anyHit = true;
                        bestT = tTri;
                        bestLocalN = nTri;
                    }
                }
            }
        }
        else{
            const uint lIdx = asuint(n0.w);
            const uint rIdx = meta & 0x7fffffffu;
            const float4 ln0 = g_NwbPhysicsFloat4Buffers[meshNodeSlot][(nodeBase + lIdx) * 2u + 0u];
            const float4 ln1 = g_NwbPhysicsFloat4Buffers[meshNodeSlot][(nodeBase + lIdx) * 2u + 1u];
            const float4 rn0 = g_NwbPhysicsFloat4Buffers[meshNodeSlot][(nodeBase + rIdx) * 2u + 0u];
            const float4 rn1 = g_NwbPhysicsFloat4Buffers[meshNodeSlot][(nodeBase + rIdx) * 2u + 1u];
            float tL = 0.0f;
            float tR = 0.0f;
            const bool hitL = nwbPhysicsQueryRayAabbFast(localOrig, localInvDir, ln0.xyz - padVec, ln1.xyz + padVec, bestT, tL);
            const bool hitR = nwbPhysicsQueryRayAabbFast(localOrig, localInvDir, rn0.xyz - padVec, rn1.xyz + padVec, bestT, tR);
            if((hitL ? 1u : 0u) + (hitR ? 1u : 0u) + sp > 32u){
                outNeedsFallback = true;
                return false;
            }
            if(hitL && hitR){
                if(tL < tR){
                    stack[sp] = rIdx; stackMetric[sp] = tR; ++sp;
                    stack[sp] = lIdx; stackMetric[sp] = tL; ++sp;
                }
                else{
                    stack[sp] = lIdx; stackMetric[sp] = tL; ++sp;
                    stack[sp] = rIdx; stackMetric[sp] = tR; ++sp;
                }
            }
            else if(hitL){
                stack[sp] = lIdx; stackMetric[sp] = tL; ++sp;
            }
            else if(hitR){
                stack[sp] = rIdx; stackMetric[sp] = tR; ++sp;
            }
        }
    }

    if(!anyHit)
        return false;
    outT = bestT;
    outNormal = nwbPhysicsQuerySafeNormal(nwbPhysicsQuatRotate(bestLocalN, bodyRot), float3(0.0f, 1.0f, 0.0f));
    return true;
}

float nwbPhysicsQueryMeshPointDistSq(
    const uint meshNodeSlot,
    const uint meshTriSlot,
    const NwbPhysicsGpuShapeProxy spProxy,
    const float4 bodyRot,
    const float3 localPoint,
    const float maxQueryDist,
    out float3 outNormal,
    out bool outOverflow,
    inout uint stack[32],
    inout float stackMetric[32]
){
    outNormal = float3(0.0f, 1.0f, 0.0f);
    outOverflow = false;
    const uint nodeBase = spProxy.filterData.w;
    const uint triBase = asuint(spProxy.localRotationOrMeta.x);
    if(meshNodeSlot == NWB_PHYSICS_INVALID_INDEX || meshTriSlot == NWB_PHYSICS_INVALID_INDEX || nodeBase == NWB_PHYSICS_INVALID_INDEX || triBase == NWB_PHYSICS_INVALID_INDEX){
        outOverflow = true;
        return 1.0e30f;
    }

    const float skinR = max(spProxy.localCenterRadius.w, 0.0f);
    const float4 firstTri1 = g_NwbPhysicsFloat4Buffers[meshTriSlot][triBase * 3u + 1u];
    const uint meshFlags = asuint(firstTri1.w);
    const bool closedMesh = (meshFlags & NWB_PHYSICS_MESH_FLAG_CLOSED) != 0u;
    const float initBound = maxQueryDist + skinR;
    const float initBoundSq = initBound * initBound + 1.0e-4f;
    float minDistSq = initBoundSq;
    bool foundTri = false;
    float3 bestLocalN = float3(0.0f, 1.0f, 0.0f);
    float3 bestDiff = float3(0.0f, 0.0f, 0.0f);
    float3 bestTriCross = float3(0.0f, 1.0f, 0.0f);

    uint sp = 0u;
    const float4 rootN0 = g_NwbPhysicsFloat4Buffers[meshNodeSlot][nodeBase * 2u + 0u];
    const float4 rootN1 = g_NwbPhysicsFloat4Buffers[meshNodeSlot][nodeBase * 2u + 1u];
    const float rootDistSq = nwbPhysicsQueryAabbDistSq(localPoint, rootN0.xyz, rootN1.xyz);
    if(rootDistSq <= minDistSq){
        stack[0] = 0u;
        stackMetric[0] = rootDistSq;
        sp = 1u;
    }

    while(sp > 0u){
        --sp;
        if(stackMetric[sp] > minDistSq)
            continue;
        const uint ni = stack[sp];
        const float4 n0 = g_NwbPhysicsFloat4Buffers[meshNodeSlot][(nodeBase + ni) * 2u + 0u];
        const float4 n1 = g_NwbPhysicsFloat4Buffers[meshNodeSlot][(nodeBase + ni) * 2u + 1u];
        const uint meta = asuint(n1.w);
        if((meta & 0x80000000u) != 0u){
            const uint firstTri = asuint(n0.w);
            const uint triCount = meta & 0x7fffffffu;
            for(uint ti = 0u; ti < triCount; ++ti){
                const uint triIdx = triBase + firstTri + ti;
                const float3 tv0 = g_NwbPhysicsFloat4Buffers[meshTriSlot][triIdx * 3u + 0u].xyz;
                const float3 tv1 = g_NwbPhysicsFloat4Buffers[meshTriSlot][triIdx * 3u + 1u].xyz;
                const float3 tv2 = g_NwbPhysicsFloat4Buffers[meshTriSlot][triIdx * 3u + 2u].xyz;
                const float3 closePt = nwbPhysicsQueryClosestPtTriangle(localPoint, tv0, tv1, tv2);
                const float3 diff = localPoint - closePt;
                const float dSq = dot(diff, diff);
                if(dSq <= minDistSq){
                    minDistSq = dSq;
                    foundTri = true;
                    bestDiff = diff;
                    bestTriCross = cross(tv1 - tv0, tv2 - tv0);
                }
            }
        }
        else{
            const uint lIdx = asuint(n0.w);
            const uint rIdx = meta & 0x7fffffffu;
            const float4 ln0 = g_NwbPhysicsFloat4Buffers[meshNodeSlot][(nodeBase + lIdx) * 2u + 0u];
            const float4 ln1 = g_NwbPhysicsFloat4Buffers[meshNodeSlot][(nodeBase + lIdx) * 2u + 1u];
            const float4 rn0 = g_NwbPhysicsFloat4Buffers[meshNodeSlot][(nodeBase + rIdx) * 2u + 0u];
            const float4 rn1 = g_NwbPhysicsFloat4Buffers[meshNodeSlot][(nodeBase + rIdx) * 2u + 1u];
            const float dL = nwbPhysicsQueryAabbDistSq(localPoint, ln0.xyz, ln1.xyz);
            const float dR = nwbPhysicsQueryAabbDistSq(localPoint, rn0.xyz, rn1.xyz);
            const bool keepL = dL <= minDistSq;
            const bool keepR = dR <= minDistSq;
            if((keepL ? 1u : 0u) + (keepR ? 1u : 0u) + sp > 32u){
                outOverflow = true;
                return 1.0e30f;
            }
            if(keepL && keepR){
                if(dL < dR){
                    stack[sp] = rIdx; stackMetric[sp] = dR; ++sp;
                    stack[sp] = lIdx; stackMetric[sp] = dL; ++sp;
                }
                else{
                    stack[sp] = lIdx; stackMetric[sp] = dL; ++sp;
                    stack[sp] = rIdx; stackMetric[sp] = dR; ++sp;
                }
            }
            else if(keepL){
                stack[sp] = lIdx; stackMetric[sp] = dL; ++sp;
            }
            else if(keepR){
                stack[sp] = rIdx; stackMetric[sp] = dR; ++sp;
            }
        }
    }

    if(foundTri){
        const float3 fallbackN = nwbPhysicsQuerySafeNormal(bestTriCross, float3(0.0f, 1.0f, 0.0f));
        bestLocalN = nwbPhysicsQuerySafeNormal(bestDiff, fallbackN);
    }
    outNormal = nwbPhysicsQuerySafeNormal(nwbPhysicsQuatRotate(bestLocalN, bodyRot), float3(0.0f, 1.0f, 0.0f));
    if(foundTri && minDistSq <= skinR * skinR)
        return 0.0f;
    if(closedMesh){
        bool insideOverflow = false;
        const bool insideClosed = nwbPhysicsQueryIsInsideClosedMesh(nodeBase, triBase, meshNodeSlot, meshTriSlot, localPoint, insideOverflow, stack);
        if(insideOverflow){
            outOverflow = true;
            return 1.0e30f;
        }
        if(insideClosed)
            return 0.0f;
    }
    if(!foundTri)
        return 1.0e30f;
    const float d = max(sqrt(minDistSq) - skinR, 0.0f);
    return d * d;
}

float nwbPhysicsShapePointDistSq(
    const float3 p,
    const NwbPhysicsGpuBodyPose pose,
    const NwbPhysicsGpuShapeProxy sp,
    const uint hullVertexSlot,
    const uint meshNodeSlot,
    const uint meshTriSlot,
    const float maxQueryDist,
    out float3 outNormal,
    inout bool outNeedsFallback,
    inout uint meshStack[32],
    inout float meshStackMetric[32]
){
    const uint shapeType = sp.bodyIndexTypeShape.y;
    const uint shapeFlags = sp.bodyIndexTypeShape.w;
    const float3 origin = pose.originPad.xyz;
    const float4 q = pose.rotation;

    if(shapeType == NWB_PHYSICS_SHAPE_TYPE_SPHERE){
        const float3 c = origin + nwbPhysicsQuatRotate(sp.localCenterRadius.xyz, q);
        const float r = sp.localCenterRadius.w;
        const float3 d = p - c;
        const float lenSq = dot(d, d);
        if(lenSq <= r * r){
            outNormal = nwbPhysicsQuerySafeNormal(d, float3(0.0f, 1.0f, 0.0f));
            return 0.0f;
        }
        const float dist = sqrt(lenSq) - r;
        outNormal = d * rsqrt(max(lenSq, 1.0e-12f));
        return dist * dist;
    }

    if(shapeType == NWB_PHYSICS_SHAPE_TYPE_CAPSULE || shapeType == NWB_PHYSICS_SHAPE_TYPE_SEGMENT){
        const float3 p0 = origin + nwbPhysicsQuatRotate(sp.localCenterRadius.xyz, q);
        const float3 p1 = origin + nwbPhysicsQuatRotate(sp.localExtentOrP1.xyz, q);
        const float r = sp.localCenterRadius.w;
        const float3 closest = nwbPhysicsClosestPointOnSegment(p, p0, p1);
        const float3 d = p - closest;
        const float lenSq = dot(d, d);
        if(lenSq <= r * r){
            outNormal = nwbPhysicsQuerySafeNormal(d, float3(0.0f, 1.0f, 0.0f));
            return 0.0f;
        }
        const float dist = sqrt(lenSq) - r;
        outNormal = d * rsqrt(max(lenSq, 1.0e-12f));
        return dist * dist;
    }

    if(shapeType == NWB_PHYSICS_SHAPE_TYPE_CYLINDER){
        const float4 worldQ = (sp.localRotationOrMeta.w < 0.999999f)
            ? nwbPhysicsQuatNormalize(nwbPhysicsQuatMul(q, sp.localRotationOrMeta))
            : q;
        const float3 c = origin + nwbPhysicsQuatRotate(sp.localCenterRadius.xyz, q);
        const float cr = max(sp.localCenterRadius.w, 0.0f);
        const float rCore = sp.localExtentOrP1.x;
        const float hhCore = sp.localExtentOrP1.y;
        const float3 localP = nwbPhysicsQuatInvRotate(p - c, worldQ);
        const float radial = sqrt(localP.x * localP.x + localP.z * localP.z);
        float cx = localP.x;
        float cz = localP.z;
        if(radial > rCore && radial > 1.0e-12f){
            const float scale = rCore / radial;
            cx *= scale;
            cz *= scale;
        }
        const float cy = clamp(localP.y, -hhCore, hhCore);
        const float3 diff = localP - float3(cx, cy, cz);
        const float diffLenSq = dot(diff, diff);
        if(diffLenSq > 1.0e-12f){
            const float diffLen = sqrt(diffLenSq);
            const float3 nLocal = diff / diffLen;
            outNormal = nwbPhysicsQuerySafeNormal(nwbPhysicsQuatRotate(nLocal, worldQ), float3(0.0f, 1.0f, 0.0f));
            if(diffLen <= cr)
                return 0.0f;
            const float dist = diffLen - cr;
            return dist * dist;
        }
        const float toSide = rCore - radial;
        const float toCap = hhCore - abs(localP.y);
        float3 localN = float3(0.0f, 1.0f, 0.0f);
        if(toCap <= toSide)
            localN = float3(0.0f, localP.y >= 0.0f ? 1.0f : -1.0f, 0.0f);
        else if(radial > 1.0e-12f)
            localN = float3(localP.x / radial, 0.0f, localP.z / radial);
        outNormal = nwbPhysicsQuerySafeNormal(nwbPhysicsQuatRotate(localN, worldQ), float3(0.0f, 1.0f, 0.0f));
        return 0.0f;
    }

    if(shapeType == NWB_PHYSICS_SHAPE_TYPE_PLANE){
        const float4 worldQ = nwbPhysicsQuatNormalize(nwbPhysicsQuatMul(q, sp.localRotationOrMeta));
        const float3 pPlane = origin + nwbPhysicsQuatRotate(sp.localCenterRadius.xyz, q);
        float3 t1W;
        float3 t2W;
        float3 nW;
        nwbPhysicsQuatToAxes(worldQ, t1W, t2W, nW);
        outNormal = nW;
        const float3 rel = p - pPlane;
        const float signedDist = dot(nW, rel);
        if(sp.localExtentOrP1.x > 0.0f && sp.localExtentOrP1.z > 0.0f){
            const float u1 = dot(rel, t1W);
            const float u2 = dot(rel, t2W);
            const float c1 = clamp(u1, -sp.localExtentOrP1.x, sp.localExtentOrP1.x);
            const float c2 = clamp(u2, -sp.localExtentOrP1.z, sp.localExtentOrP1.z);
            if(signedDist <= 0.0f && abs(u1) <= sp.localExtentOrP1.x && abs(u2) <= sp.localExtentOrP1.z)
                return 0.0f;
            const float3 closest = pPlane + t1W * c1 + t2W * c2 + nW * min(0.0f, signedDist);
            const float3 d = p - closest;
            return dot(d, d);
        }
        if(signedDist <= 0.0f)
            return 0.0f;
        return signedDist * signedDist;
    }

    if(shapeType == NWB_PHYSICS_SHAPE_TYPE_HULL && (shapeFlags & NWB_PHYSICS_SHAPE_FLAG_IS_BOX) != 0u){
        const float4 worldQ = (sp.localRotationOrMeta.w < 0.999999f)
            ? nwbPhysicsQuatNormalize(nwbPhysicsQuatMul(q, sp.localRotationOrMeta))
            : q;
        const float3 c = origin + nwbPhysicsQuatRotate(sp.localCenterRadius.xyz, q);
        const float skinR = max(sp.localCenterRadius.w, 0.0f);
        const float3 h = sp.localExtentOrP1.xyz;
        const float3 localP = nwbPhysicsQuatInvRotate(p - c, worldQ);
        const float3 clamped = clamp(localP, -h, h);
        const float3 dLocal = localP - clamped;
        const float rawDistSq = dot(dLocal, dLocal);
        outNormal = nwbPhysicsQuerySafeNormal(nwbPhysicsQuatRotate(dLocal, worldQ), float3(0.0f, 1.0f, 0.0f));
        if(skinR <= 1.0e-5f)
            return rawDistSq;
        const float rawDist = sqrt(rawDistSq);
        if(rawDist <= skinR)
            return 0.0f;
        const float effDist = rawDist - skinR;
        return effDist * effDist;
    }

    if(shapeType == NWB_PHYSICS_SHAPE_TYPE_HULL){
        const uint vOffset = sp.filterData.w;
        const uint metaPack = asuint(sp.localRotationOrMeta.x);
        const uint vCount = metaPack & 0xffu;
        const uint fCount = (metaPack >> 8u) & 0xffu;
        const float skinR = max(sp.localCenterRadius.w, 0.0f);
        if(hullVertexSlot != NWB_PHYSICS_INVALID_INDEX && vOffset != NWB_PHYSICS_INVALID_INDEX && fCount > 0u){
            const float3 localP = nwbPhysicsQuatInvRotate(p - origin, q);
            float maxSd = -1.0e20f;
            float3 bestFaceN = float3(0.0f, 1.0f, 0.0f);
            for(uint fi = 0u; fi < fCount; ++fi){
                const float4 pl = g_NwbPhysicsFloat4Buffers[hullVertexSlot][vOffset + vCount + fi];
                const float sd = dot(pl.xyz, localP) - pl.w;
                if(sd > maxSd){
                    maxSd = sd;
                    bestFaceN = pl.xyz;
                }
            }
            if(maxSd <= skinR){
                outNormal = nwbPhysicsQuerySafeNormal(nwbPhysicsQuatRotate(bestFaceN, q), float3(0.0f, 1.0f, 0.0f));
                return 0.0f;
            }
            if(maxSd > skinR + maxQueryDist){
                outNormal = nwbPhysicsQuerySafeNormal(nwbPhysicsQuatRotate(bestFaceN, q), float3(0.0f, 1.0f, 0.0f));
                return 1.0e30f;
            }
            float3 proj = localP;
            uint lastMoved = fCount;
            for(uint fi = 0u; fi < fCount; ++fi){
                const float4 pl = g_NwbPhysicsFloat4Buffers[hullVertexSlot][vOffset + vCount + fi];
                const float dPl = dot(pl.xyz, proj) - pl.w;
                if(dPl > 0.0f){
                    proj -= pl.xyz * dPl;
                    lastMoved = fi;
                }
            }
            if(lastMoved < fCount){
                bool movedPass2 = false;
                for(uint fi = 0u; fi < fCount; ++fi){
                    if(!movedPass2 && fi > lastMoved)
                        break;
                    const float4 pl = g_NwbPhysicsFloat4Buffers[hullVertexSlot][vOffset + vCount + fi];
                    const float dPl = dot(pl.xyz, proj) - pl.w;
                    if(dPl > 0.0f){
                        proj -= pl.xyz * dPl;
                        movedPass2 = true;
                    }
                }
            }
            const float3 diff = localP - proj;
            const float rawDist = length(diff);
            const float dist = max(rawDist - skinR, 0.0f);
            const float3 locN = (rawDist > 1.0e-5f) ? diff : bestFaceN;
            outNormal = nwbPhysicsQuerySafeNormal(nwbPhysicsQuatRotate(locN, q), float3(0.0f, 1.0f, 0.0f));
            return dist * dist;
        }
    }

    if(shapeType == NWB_PHYSICS_SHAPE_TYPE_MESH){
        const uint nodeBase = sp.filterData.w;
        const uint triBase = asuint(sp.localRotationOrMeta.x);
        if(meshNodeSlot != NWB_PHYSICS_INVALID_INDEX && meshTriSlot != NWB_PHYSICS_INVALID_INDEX && nodeBase != NWB_PHYSICS_INVALID_INDEX && triBase != NWB_PHYSICS_INVALID_INDEX){
            const float3 localP = nwbPhysicsQuatInvRotate(p - origin, q);
            bool meshOverflow = false;
            const float dSq = nwbPhysicsQueryMeshPointDistSq(meshNodeSlot, meshTriSlot, sp, q, localP, maxQueryDist, outNormal, meshOverflow, meshStack, meshStackMetric);
            if(meshOverflow)
                outNeedsFallback = true;
            return dSq;
        }
    }

    outNeedsFallback = true;
    outNormal = float3(0.0f, 1.0f, 0.0f);
    return 1.0e30f;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void nwbPhysicsTestSpatialQueryShape(
    const uint shapeSlot,
    const uint bodyPoseSlot,
    const uint hullVertexSlot,
    const uint meshNodeSlot,
    const uint meshTriSlot,
    const uint queryType,
    const uint qCat,
    const uint qMask,
    const uint qFlags,
    const float3 rayOrig,
    const float3 rayDir,
    const float3 rayInvDir,
    const float3 sphereCenter,
    const float sphereRadius,
    const float3 qCenter,
    const uint shapeIdx,
    const float3 aMin,
    const float3 aMax,
    inout float bestMetric,
    inout float3 bestPoint,
    inout float3 bestNormal,
    inout uint bestShapeId,
    inout uint bestBodyIdx,
    inout uint hitCount,
    inout bool hasHit,
    inout bool needsCpuFallback,
    inout uint meshStack[32],
    inout float meshStackMetric[32]
){
    const uint4 meta = g_NwbPhysicsShapeProxyBuffers[shapeSlot][shapeIdx].bodyIndexTypeShape;
    if((meta.w & NWB_PHYSICS_SHAPE_FLAG_SENSOR) != 0u && (qFlags & NWB_PHYSICS_QUERY_FLAG_INCLUDE_SENSORS) == 0u)
        return;
    const uint4 filter = g_NwbPhysicsShapeProxyBuffers[shapeSlot][shapeIdx].filterData;
    if(!nwbPhysicsQueryPassesFilter(qCat, qMask, qFlags, filter))
        return;

    const uint shapeType = meta.y;
    if(queryType == NWB_PHYSICS_QUERY_TYPE_AABB_OVERLAP){
        if(
            (shapeType == NWB_PHYSICS_SHAPE_TYPE_HULL && (meta.w & NWB_PHYSICS_SHAPE_FLAG_IS_BOX) == 0u && filter.w == NWB_PHYSICS_INVALID_INDEX)
            || (shapeType == NWB_PHYSICS_SHAPE_TYPE_MESH && (filter.w == NWB_PHYSICS_INVALID_INDEX || asuint(g_NwbPhysicsShapeProxyBuffers[shapeSlot][shapeIdx].localRotationOrMeta.x) == NWB_PHYSICS_INVALID_INDEX))
        ){
            needsCpuFallback = true;
            return;
        }
        ++hitCount;
        const float3 center = (aMin + aMax) * 0.5f;
        const float3 diff = center - qCenter;
        const float distSq = dot(diff, diff);
        if(!hasHit || distSq < bestMetric){
            hasHit = true;
            bestMetric = distSq;
            bestPoint = center;
            bestNormal = nwbPhysicsQuerySafeNormal(diff, float3(0.0f, 1.0f, 0.0f));
            bestShapeId = meta.z;
            bestBodyIdx = meta.x;
        }
        return;
    }

    const bool isRay = (queryType == NWB_PHYSICS_QUERY_TYPE_RAYCAST_CLOSEST) || (queryType == NWB_PHYSICS_QUERY_TYPE_RAYCAST_ANY);
    if(
        shapeType != NWB_PHYSICS_SHAPE_TYPE_SPHERE
        && shapeType != NWB_PHYSICS_SHAPE_TYPE_CAPSULE
        && shapeType != NWB_PHYSICS_SHAPE_TYPE_SEGMENT
        && shapeType != NWB_PHYSICS_SHAPE_TYPE_PLANE
        && shapeType != NWB_PHYSICS_SHAPE_TYPE_CYLINDER
        && shapeType != NWB_PHYSICS_SHAPE_TYPE_HULL
        && shapeType != NWB_PHYSICS_SHAPE_TYPE_MESH
    ){
        needsCpuFallback = true;
        return;
    }
    if(isRay){
        if(shapeType == NWB_PHYSICS_SHAPE_TYPE_CYLINDER){
            if(max(g_NwbPhysicsShapeProxyBuffers[shapeSlot][shapeIdx].localCenterRadius.w, 0.0f) > 1.0e-5f){
                needsCpuFallback = true;
                return;
            }
        }
        else if(shapeType == NWB_PHYSICS_SHAPE_TYPE_HULL && (meta.w & NWB_PHYSICS_SHAPE_FLAG_IS_BOX) == 0u){
            if(hullVertexSlot == NWB_PHYSICS_INVALID_INDEX || filter.w == NWB_PHYSICS_INVALID_INDEX){
                needsCpuFallback = true;
                return;
            }
        }
        else if(shapeType == NWB_PHYSICS_SHAPE_TYPE_MESH){
            if(
                meshNodeSlot == NWB_PHYSICS_INVALID_INDEX
                || meshTriSlot == NWB_PHYSICS_INVALID_INDEX
                || filter.w == NWB_PHYSICS_INVALID_INDEX
                || asuint(g_NwbPhysicsShapeProxyBuffers[shapeSlot][shapeIdx].localRotationOrMeta.x) == NWB_PHYSICS_INVALID_INDEX
            ){
                needsCpuFallback = true;
                return;
            }
        }
    }
    else{
        if(shapeType == NWB_PHYSICS_SHAPE_TYPE_HULL && (meta.w & NWB_PHYSICS_SHAPE_FLAG_IS_BOX) == 0u){
            const uint metaPackPre = asuint(g_NwbPhysicsShapeProxyBuffers[shapeSlot][shapeIdx].localRotationOrMeta.x);
            if(hullVertexSlot == NWB_PHYSICS_INVALID_INDEX || filter.w == NWB_PHYSICS_INVALID_INDEX || ((metaPackPre >> 8u) & 0xffu) == 0u){
                needsCpuFallback = true;
                return;
            }
        }
        else if(shapeType == NWB_PHYSICS_SHAPE_TYPE_MESH){
            if(
                meshNodeSlot == NWB_PHYSICS_INVALID_INDEX
                || meshTriSlot == NWB_PHYSICS_INVALID_INDEX
                || filter.w == NWB_PHYSICS_INVALID_INDEX
                || asuint(g_NwbPhysicsShapeProxyBuffers[shapeSlot][shapeIdx].localRotationOrMeta.x) == NWB_PHYSICS_INVALID_INDEX
            ){
                needsCpuFallback = true;
                return;
            }
        }
    }

    const NwbPhysicsGpuShapeProxy spProxy = g_NwbPhysicsShapeProxyBuffers[shapeSlot][shapeIdx];
    const NwbPhysicsGpuBodyPose pose = g_NwbPhysicsBodyPoseBuffers[bodyPoseSlot][meta.x];
    if(isRay){
        float tHit = 0.0f;
        float3 nHit = float3(0.0f, 1.0f, 0.0f);
        bool hitShape = false;
        if(shapeType == NWB_PHYSICS_SHAPE_TYPE_SPHERE){
            const float3 c = pose.originPad.xyz + nwbPhysicsQuatRotate(spProxy.localCenterRadius.xyz, pose.rotation);
            hitShape = nwbPhysicsQueryRaySphere(rayOrig, rayDir, bestMetric, c, spProxy.localCenterRadius.w, tHit, nHit);
        }
        else if(shapeType == NWB_PHYSICS_SHAPE_TYPE_CAPSULE){
            const float3 p0 = pose.originPad.xyz + nwbPhysicsQuatRotate(spProxy.localCenterRadius.xyz, pose.rotation);
            const float3 p1 = pose.originPad.xyz + nwbPhysicsQuatRotate(spProxy.localExtentOrP1.xyz, pose.rotation);
            hitShape = nwbPhysicsQueryRayCapsule(rayOrig, rayDir, bestMetric, p0, p1, spProxy.localCenterRadius.w, tHit, nHit);
        }
        else if(shapeType == NWB_PHYSICS_SHAPE_TYPE_SEGMENT){
            const float3 p0 = pose.originPad.xyz + nwbPhysicsQuatRotate(spProxy.localCenterRadius.xyz, pose.rotation);
            const float3 p1 = pose.originPad.xyz + nwbPhysicsQuatRotate(spProxy.localExtentOrP1.xyz, pose.rotation);
            const float rSeg = spProxy.localCenterRadius.w;
            if(rSeg > 0.0f)
                hitShape = nwbPhysicsQueryRayCapsule(rayOrig, rayDir, bestMetric, p0, p1, rSeg, tHit, nHit);
            else
                hitShape = nwbPhysicsQueryRaySegmentZero(rayOrig, rayDir, bestMetric, p0, p1, tHit, nHit);
        }
        else if(shapeType == NWB_PHYSICS_SHAPE_TYPE_PLANE){
            const float4 worldQ = nwbPhysicsQuatNormalize(nwbPhysicsQuatMul(pose.rotation, spProxy.localRotationOrMeta));
            const float3 pPlane = pose.originPad.xyz + nwbPhysicsQuatRotate(spProxy.localCenterRadius.xyz, pose.rotation);
            float3 t1W;
            float3 t2W;
            float3 nW;
            nwbPhysicsQuatToAxes(worldQ, t1W, t2W, nW);
            const bool isFin = (spProxy.localExtentOrP1.x > 0.0f) && (spProxy.localExtentOrP1.z > 0.0f);
            hitShape = nwbPhysicsQueryRayPlane(rayOrig, rayDir, bestMetric, pPlane, t1W, t2W, nW, isFin, spProxy.localExtentOrP1.x, spProxy.localExtentOrP1.z, tHit, nHit);
        }
        else if(shapeType == NWB_PHYSICS_SHAPE_TYPE_CYLINDER){
            const float4 worldQ = (spProxy.localRotationOrMeta.w < 0.999999f)
                ? nwbPhysicsQuatNormalize(nwbPhysicsQuatMul(pose.rotation, spProxy.localRotationOrMeta))
                : pose.rotation;
            const float3 c = pose.originPad.xyz + nwbPhysicsQuatRotate(spProxy.localCenterRadius.xyz, pose.rotation);
            hitShape = nwbPhysicsQueryRayCylinderSharp(rayOrig, rayDir, bestMetric, c, worldQ, spProxy.localExtentOrP1.x, spProxy.localExtentOrP1.y, tHit, nHit);
        }
        else if(shapeType == NWB_PHYSICS_SHAPE_TYPE_HULL && (meta.w & NWB_PHYSICS_SHAPE_FLAG_IS_BOX) != 0u){
            const float4 worldQ = (spProxy.localRotationOrMeta.w < 0.999999f)
                ? nwbPhysicsQuatNormalize(nwbPhysicsQuatMul(pose.rotation, spProxy.localRotationOrMeta))
                : pose.rotation;
            const float3 c = pose.originPad.xyz + nwbPhysicsQuatRotate(spProxy.localCenterRadius.xyz, pose.rotation);
            const float3 localOrig = nwbPhysicsQuatInvRotate(rayOrig - c, worldQ);
            const float3 localDir = nwbPhysicsQuatInvRotate(rayDir, worldQ);
            const float3 localInvDir = float3(
                abs(localDir.x) > 1.0e-12f ? (1.0f / localDir.x) : 1.0e30f,
                abs(localDir.y) > 1.0e-12f ? (1.0f / localDir.y) : 1.0e30f,
                abs(localDir.z) > 1.0e-12f ? (1.0f / localDir.z) : 1.0e30f
            );
            const float skinR = max(spProxy.localCenterRadius.w, 0.0f);
            const float3 h = spProxy.localExtentOrP1.xyz + float3(skinR, skinR, skinR);
            float3 localN = float3(0.0f, 1.0f, 0.0f);
            hitShape = nwbPhysicsQueryRayAabb(localOrig, localInvDir, -h, h, bestMetric, tHit, localN);
            if(hitShape){
                if(skinR > 1.0e-5f){
                    needsCpuFallback = true;
                    hitShape = false;
                }
                else{
                    nHit = nwbPhysicsQuerySafeNormal(nwbPhysicsQuatRotate(localN, worldQ), float3(0.0f, 1.0f, 0.0f));
                }
            }
        }
        else if(shapeType == NWB_PHYSICS_SHAPE_TYPE_HULL){
            const float3 localOrig = nwbPhysicsQuatInvRotate(rayOrig - pose.originPad.xyz, pose.rotation);
            const float3 localDir = nwbPhysicsQuatInvRotate(rayDir, pose.rotation);
            bool hullFallback = false;
            hitShape = nwbPhysicsQueryRayHull(hullVertexSlot, spProxy, pose.rotation, localOrig, localDir, bestMetric, tHit, nHit, hullFallback);
            if(hullFallback)
                needsCpuFallback = true;
        }
        else{
            const float3 localOrig = nwbPhysicsQuatInvRotate(rayOrig - pose.originPad.xyz, pose.rotation);
            const float3 localDir = nwbPhysicsQuatInvRotate(rayDir, pose.rotation);
            bool meshFallback = false;
            hitShape = nwbPhysicsQueryRayMesh(meshNodeSlot, meshTriSlot, spProxy, pose.rotation, qFlags, localOrig, localDir, bestMetric, tHit, nHit, meshFallback, meshStack, meshStackMetric);
            if(meshFallback)
                needsCpuFallback = true;
        }

        if(hitShape){
            ++hitCount;
            if(!hasHit || tHit < bestMetric){
                hasHit = true;
                bestMetric = tHit;
                bestPoint = rayOrig + rayDir * tHit;
                bestNormal = nHit;
                bestShapeId = meta.z;
                bestBodyIdx = meta.x;
            }
        }
        return;
    }

    float3 nOut = float3(0.0f, 1.0f, 0.0f);
    bool overlapFallback = false;
    const float dSq = nwbPhysicsShapePointDistSq(sphereCenter, pose, spProxy, hullVertexSlot, meshNodeSlot, meshTriSlot, sphereRadius, nOut, overlapFallback, meshStack, meshStackMetric);
    if(overlapFallback)
        needsCpuFallback = true;
    else if(dSq <= sphereRadius * sphereRadius){
        ++hitCount;
        if(!hasHit || dSq < bestMetric){
            hasHit = true;
            bestMetric = dSq;
            const float d = sqrt(dSq);
            bestPoint = sphereCenter - nOut * min(d, sphereRadius);
            bestNormal = nOut;
            bestShapeId = meta.z;
            bestBodyIdx = meta.x;
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_PHYSICS_WORKGROUP_SIZE, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint tid = dispatchThreadID.x;
    if(tid >= g_NwbPhysicsPushConstants.itemCount)
        return;

    const uint shapeCount = g_NwbPhysicsPushConstants.itemOffset;
    const bool useLbvh = (g_NwbPhysicsPushConstants.modeOrSecondaryCount != 0u) && (shapeCount >= 2u);
    const uint bodyPoseSlot = g_NwbPhysicsPushConstants.slots0.x;
    const uint shapeAabbSlot = g_NwbPhysicsPushConstants.slots0.y;
    const uint shapeSlot = g_NwbPhysicsPushConstants.slots0.z;
    const uint lbvhNodeSlot = g_NwbPhysicsPushConstants.slots0.w;
    const uint querySlot = g_NwbPhysicsPushConstants.slots1.x;
    const uint resultSlot = g_NwbPhysicsPushConstants.slots1.y;
    const uint hullVertexSlot = g_NwbPhysicsPushConstants.slots1.z;
    const uint meshNodeSlot = g_NwbPhysicsPushConstants.slots1.w;
    const uint meshTriSlot = g_NwbPhysicsPushConstants.flags;

    const NwbPhysicsGpuSpatialQuery q = g_NwbPhysicsSpatialQueryBuffers[querySlot][tid];
    const uint queryType = q.params.x;
    const bool isRayQuery = (queryType == NWB_PHYSICS_QUERY_TYPE_RAYCAST_CLOSEST) || (queryType == NWB_PHYSICS_QUERY_TYPE_RAYCAST_ANY);
    const uint qCat = q.params.y;
    const uint qMask = q.params.z;
    const uint qFlags = q.params.w;

    const float3 rayOrig = q.originMin.xyz;
    const float3 rayDir = q.dirMax.xyz;
    const float3 rayInvDir = isRayQuery
        ? float3(
            abs(rayDir.x) > 1.0e-12f ? (1.0f / rayDir.x) : 1.0e30f,
            abs(rayDir.y) > 1.0e-12f ? (1.0f / rayDir.y) : 1.0e30f,
            abs(rayDir.z) > 1.0e-12f ? (1.0f / rayDir.z) : 1.0e30f
        )
        : float3(0.0f, 0.0f, 0.0f);
    const float maxFrac = q.dirMax.w;
    const float3 sphereCenter = q.originMin.xyz;
    const float sphereRadius = max(q.dirMax.w, 0.0f);

    float3 qBoxMin = q.originMin.xyz;
    float3 qBoxMax = q.dirMax.xyz;
    if(isRayQuery){
        const float3 rayEnd = rayOrig + rayDir * maxFrac;
        qBoxMin = min(rayOrig, rayEnd);
        qBoxMax = max(rayOrig, rayEnd);
    }
    else if(queryType == NWB_PHYSICS_QUERY_TYPE_SPHERE_OVERLAP){
        qBoxMin = sphereCenter - float3(sphereRadius, sphereRadius, sphereRadius);
        qBoxMax = sphereCenter + float3(sphereRadius, sphereRadius, sphereRadius);
    }
    const float3 qCenter = (qBoxMin + qBoxMax) * 0.5f;

    float bestMetric = isRayQuery ? maxFrac : 1.0e30f;
    float3 bestPoint = float3(0.0f, 0.0f, 0.0f);
    float3 bestNormal = float3(0.0f, 1.0f, 0.0f);
    uint bestShapeId = NWB_PHYSICS_INVALID_INDEX;
    uint bestBodyIdx = NWB_PHYSICS_INVALID_INDEX;
    uint hitCount = 0u;
    bool hasHit = false;
    bool needsCpuFallback = false;

    const bool traverseLbvh = useLbvh && (lbvhNodeSlot != NWB_PHYSICS_INVALID_INDEX);
    const uint leafStart = traverseLbvh ? (shapeCount - 1u) : 0u;
    if(shapeCount > 0u){
        uint stack[64];
        float stackT[64];
        uint meshStack[32];
        float meshStackMetric[32];
        stack[0] = 0u;
        stackT[0] = 0.0f;
        uint sp = 0u;
        uint bruteIdx = 0u;
        if(traverseLbvh){
            const NwbPhysicsGpuLbvhNode root = g_NwbPhysicsLbvhNodeReadBuffers[lbvhNodeSlot][0u];
            bool rootHit = false;
            float rootT = 0.0f;
            if(isRayQuery)
                rootHit = nwbPhysicsQueryRayAabbFast(rayOrig, rayInvDir, root.minLeft.xyz, root.maxRight.xyz, bestMetric, rootT);
            else
                rootHit = nwbPhysicsQueryAabbOverlaps(qBoxMin, qBoxMax, root.minLeft.xyz, root.maxRight.xyz);
            if(rootHit){
                stack[0] = 0u;
                stackT[0] = rootT;
                sp = 1u;
            }
        }

        while(((traverseLbvh && sp > 0u) || (!traverseLbvh && bruteIdx < shapeCount)) && !(queryType == NWB_PHYSICS_QUERY_TYPE_RAYCAST_ANY && hasHit) && !needsCpuFallback){
            uint leafCount = 0u;
            uint leafShapes[2] = { 0u, 0u };
            float3 leafMin[2] = { float3(0.0f, 0.0f, 0.0f), float3(0.0f, 0.0f, 0.0f) };
            float3 leafMax[2] = { float3(0.0f, 0.0f, 0.0f), float3(0.0f, 0.0f, 0.0f) };
            float leafT[2] = { 0.0f, 0.0f };
            if(traverseLbvh){
                --sp;
                if(isRayQuery && stackT[sp] > bestMetric)
                    continue;
                const uint nodeIdx = stack[sp];
                const NwbPhysicsGpuLbvhNode node = g_NwbPhysicsLbvhNodeReadBuffers[lbvhNodeSlot][nodeIdx];
                const uint lIdx = asuint(node.minLeft.w);
                const uint rIdx = asuint(node.maxRight.w);
                const NwbPhysicsGpuLbvhNode lNode = g_NwbPhysicsLbvhNodeReadBuffers[lbvhNodeSlot][lIdx];
                const NwbPhysicsGpuLbvhNode rNode = g_NwbPhysicsLbvhNodeReadBuffers[lbvhNodeSlot][rIdx];
                bool hitL = false;
                bool hitR = false;
                float tL = 0.0f;
                float tR = 0.0f;
                if(isRayQuery){
                    hitL = nwbPhysicsQueryRayAabbFast(rayOrig, rayInvDir, lNode.minLeft.xyz, lNode.maxRight.xyz, bestMetric, tL);
                    hitR = nwbPhysicsQueryRayAabbFast(rayOrig, rayInvDir, rNode.minLeft.xyz, rNode.maxRight.xyz, bestMetric, tR);
                }
                else{
                    hitL = nwbPhysicsQueryAabbOverlaps(qBoxMin, qBoxMax, lNode.minLeft.xyz, lNode.maxRight.xyz);
                    hitR = nwbPhysicsQueryAabbOverlaps(qBoxMin, qBoxMax, rNode.minLeft.xyz, rNode.maxRight.xyz);
                }
                const bool swapOrder = isRayQuery && hitL && hitR && (tR < tL);
                const bool hitFirst = swapOrder ? hitR : hitL;
                const bool hitSecond = swapOrder ? hitL : hitR;
                const uint idxFirst = swapOrder ? rIdx : lIdx;
                const uint idxSecond = swapOrder ? lIdx : rIdx;
                const float tFirst = swapOrder ? tR : tL;
                const float tSecond = swapOrder ? tL : tR;
                const NwbPhysicsGpuLbvhNode nodeFirst = swapOrder ? rNode : lNode;
                const NwbPhysicsGpuLbvhNode nodeSecond = swapOrder ? lNode : rNode;
                const bool intFirst = hitFirst && (idxFirst < leafStart);
                const bool intSecond = hitSecond && (idxSecond < leafStart);
                const uint internalPushCount = (intFirst ? 1u : 0u) + (intSecond ? 1u : 0u);
                if(sp + internalPushCount > 64u){
                    needsCpuFallback = true;
                    break;
                }
                if(intSecond){
                    stack[sp] = idxSecond;
                    stackT[sp] = tSecond;
                    ++sp;
                }
                if(intFirst){
                    stack[sp] = idxFirst;
                    stackT[sp] = tFirst;
                    ++sp;
                }
                if(hitFirst && idxFirst >= leafStart){
                    leafShapes[leafCount] = asuint(nodeFirst.minLeft.w);
                    leafMin[leafCount] = nodeFirst.minLeft.xyz;
                    leafMax[leafCount] = nodeFirst.maxRight.xyz;
                    leafT[leafCount] = tFirst;
                    ++leafCount;
                }
                if(hitSecond && idxSecond >= leafStart){
                    leafShapes[leafCount] = asuint(nodeSecond.minLeft.w);
                    leafMin[leafCount] = nodeSecond.minLeft.xyz;
                    leafMax[leafCount] = nodeSecond.maxRight.xyz;
                    leafT[leafCount] = tSecond;
                    ++leafCount;
                }
            }
            else{
                const uint shapeIdx = bruteIdx++;
                const float3 aMin = g_NwbPhysicsFloat4Buffers[shapeAabbSlot][shapeIdx * 2u + 0u].xyz;
                const float3 aMax = g_NwbPhysicsFloat4Buffers[shapeAabbSlot][shapeIdx * 2u + 1u].xyz;
                if(isRayQuery){
                    float tAabb = 0.0f;
                    if(!nwbPhysicsQueryRayAabbFast(rayOrig, rayInvDir, aMin, aMax, bestMetric, tAabb))
                        continue;
                }
                else if(!nwbPhysicsQueryAabbOverlaps(qBoxMin, qBoxMax, aMin, aMax)){
                    continue;
                }
                leafShapes[0] = shapeIdx;
                leafMin[0] = aMin;
                leafMax[0] = aMax;
                leafT[0] = 0.0f;
                leafCount = 1u;
            }
            for(uint li = 0u; li < leafCount; ++li){
                if(needsCpuFallback || (queryType == NWB_PHYSICS_QUERY_TYPE_RAYCAST_ANY && hasHit))
                    break;
                if(isRayQuery && leafT[li] > bestMetric)
                    continue;
                nwbPhysicsTestSpatialQueryShape(
                    shapeSlot,
                    bodyPoseSlot,
                    hullVertexSlot,
                    meshNodeSlot,
                    meshTriSlot,
                    queryType,
                    qCat,
                    qMask,
                    qFlags,
                    rayOrig,
                    rayDir,
                    rayInvDir,
                    sphereCenter,
                    sphereRadius,
                    qCenter,
                    leafShapes[li],
                    leafMin[li],
                    leafMax[li],
                    bestMetric,
                    bestPoint,
                    bestNormal,
                    bestShapeId,
                    bestBodyIdx,
                    hitCount,
                    hasHit,
                    needsCpuFallback,
                    meshStack,
                    meshStackMetric
                );
            }
        }
    }

    NwbPhysicsGpuSpatialQueryResult outRes;
    const uint fbFlag = needsCpuFallback ? NWB_PHYSICS_QUERY_RESULT_FLAG_NEEDS_CPU_FALLBACK : 0u;
    if(hasHit){
        if(isRayQuery){
            outRes.hitPointFraction = float4(bestPoint, bestMetric);
            outRes.hitNormalDist = float4(bestNormal, length(bestPoint - rayOrig));
        }
        else{
            outRes.hitPointFraction = float4(bestPoint, 0.0f);
            outRes.hitNormalDist = float4(bestNormal, sqrt(max(bestMetric, 0.0f)));
        }
        outRes.hitIds = uint4(bestShapeId, bestBodyIdx, hitCount, NWB_PHYSICS_QUERY_RESULT_FLAG_HIT | fbFlag);
    }
    else{
        outRes.hitPointFraction = float4(0.0f, 0.0f, 0.0f, 0.0f);
        outRes.hitNormalDist = float4(0.0f, 1.0f, 0.0f, 0.0f);
        outRes.hitIds = uint4(NWB_PHYSICS_INVALID_INDEX, NWB_PHYSICS_INVALID_INDEX, 0u, fbFlag);
    }
    g_NwbPhysicsSpatialQueryResultBuffers[resultSlot][tid] = outRes;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

