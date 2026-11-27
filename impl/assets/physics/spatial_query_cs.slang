// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "physics_common.slangi"

#ifndef NWB_PHYSICS_QUERY_PROFILE
#define NWB_PHYSICS_QUERY_PROFILE 1
#endif

#define NWB_PHYSICS_QUERY_HAS_MESH (NWB_PHYSICS_QUERY_PROFILE != 0)
#define NWB_PHYSICS_QUERY_IS_SHAPE_OP (NWB_PHYSICS_QUERY_PROFILE == 2)
#define NWB_PHYSICS_ENABLE_GJK NWB_PHYSICS_QUERY_IS_SHAPE_OP
#define NWB_PHYSICS_ENABLE_EPA NWB_PHYSICS_QUERY_IS_SHAPE_OP
#include "gjk_epa_helpers.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if !NWB_PHYSICS_QUERY_IS_SHAPE_OP
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
#endif

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

#if !NWB_PHYSICS_QUERY_IS_SHAPE_OP
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
            outNormal = nwbPhysicsSafeNormal(m, nwbPhysicsSafeNormal(-dir, float3(0.0f, 1.0f, 0.0f)));
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
    outNormal = nwbPhysicsSafeNormal((origin + dir * t) - center, nwbPhysicsSafeNormal(-dir, float3(0.0f, 1.0f, 0.0f)));
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
            outNormal = nwbPhysicsSafeNormal(radial, float3(0.0f, 1.0f, 0.0f));
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
                    bestN = nwbPhysicsSafeNormal(hitP - axisPt, float3(0.0f, 1.0f, 0.0f));
                }
            }
        }
    }

    outT = bestT;
    outNormal = bestN;
    return anyHit;
}
#endif

float nwbPhysicsQueryAabbDistSq(const float3 p, const float3 bMin, const float3 bMax){
    const float3 clamped = clamp(p, bMin, bMax);
    const float3 d = p - clamped;
    return dot(d, d);
}

bool nwbPhysicsQueryPassesFilter(const uint qCat, const uint qMask, const uint qFlags, const uint4 shapeFilter, const int auxGroupIndex){
    int qGroup = 0;
    if((qFlags & NWB_PHYSICS_QUERY_FLAG_HAS_AUX) != 0u && auxGroupIndex != 0)
        qGroup = auxGroupIndex;
    else if((qFlags & NWB_PHYSICS_QUERY_FLAG_USE_GROUP_INDEX) != 0u)
        qGroup = int(qFlags) >> 16;
    if(qGroup != 0){
        const int sGroup = asint(shapeFilter.z);
        if(sGroup == qGroup)
            return qGroup > 0;
    }
    return ((qCat & shapeFilter.y) != 0u) && ((shapeFilter.x & qMask) != 0u);
}

#if !NWB_PHYSICS_QUERY_IS_SHAPE_OP
bool nwbPhysicsQueryRayTriangleT(
    const float3 origin,
    const float3 dir,
    const float3 v0,
    const float3 v1,
    const float3 v2,
    const bool cullBackfaces,
    const float maxT,
    out float outT,
    out float3 outFaceCross
){
    outT = 0.0f;
    const float3 e1 = v1 - v0;
    const float3 e2 = v2 - v0;
    outFaceCross = cross(e1, e2);
    if(cullBackfaces && dot(outFaceCross, dir) > 0.0f)
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

    outT = t;
    return true;
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
    float3 faceCross;
    if(!nwbPhysicsQueryRayTriangleT(origin, dir, v0, v1, v2, cullBackfaces, maxT, outT, faceCross)){
        outNormal = float3(0.0f, 1.0f, 0.0f);
        return false;
    }
    float3 n = nwbPhysicsSafeNormal(faceCross, float3(0.0f, 1.0f, 0.0f));
    if(dot(n, dir) > 0.0f)
        n = -n;
    outNormal = n;
    return true;
}

void nwbPhysicsQueryRayOpenCylinder(
    const float3 origin,
    const float3 dir,
    const float3 ea,
    const float3 eb,
    const float radius,
    inout float bestT,
    inout bool anyHit,
    inout float3 bestN
){
    const float3 d = eb - ea;
    const float dLenSq = dot(d, d);
    if(dLenSq <= 1.0e-12f)
        return;

    const float dLen = sqrt(dLenSq);
    const float3 axis = d / dLen;
    const float3 m = origin - ea;
    const float mDotA = dot(m, axis);
    const float dirDotA = dot(dir, axis);
    const float3 mPerp = m - axis * mDotA;
    const float3 dirPerp = dir - axis * dirDotA;
    const float a = dot(dirPerp, dirPerp);
    if(a <= 1.0e-12f)
        return;

    const float b = dot(mPerp, dirPerp);
    const float c = dot(mPerp, mPerp) - radius * radius;
    const float disc = b * b - a * c;
    if(disc < 0.0f)
        return;

    const float t = (-b - sqrt(disc)) / a;
    if(t < 0.0f || t > bestT)
        return;

    const float s = mDotA + t * dirDotA;
    if(s < 0.0f || s > dLen)
        return;

    const float3 hitP = origin + dir * t;
    const float3 axisPt = ea + axis * s;
    anyHit = true;
    bestT = t;
    bestN = nwbPhysicsSafeNormal(hitP - axisPt, float3(0.0f, 1.0f, 0.0f));
}

bool nwbPhysicsQuerySweptSphereTriangle(
    const float3 origin,
    const float3 dir,
    const float radius,
    const float3 v0,
    const float3 v1,
    const float3 v2,
    const bool cullBackfaces,
    const float maxT,
    out float outT,
    out float3 outNormal,
    out float outRawDist
){
    outT = 0.0f;
    outNormal = float3(0.0f, 1.0f, 0.0f);
    outRawDist = 0.0f;
    if(radius <= 1.0e-6f)
        return nwbPhysicsQueryRayTriangle(origin, dir, v0, v1, v2, cullBackfaces, maxT, outT, outNormal);

    const float3 e1 = v1 - v0;
    const float3 e2 = v2 - v0;
    const float3 faceCross = cross(e1, e2);
    const float faceCrossLenSq = dot(faceCross, faceCross);
    if(cullBackfaces && dot(faceCross, dir) > 0.0f)
        return false;

    const float3 cp0 = nwbPhysicsClosestPointOnTriangle(origin, v0, v1, v2);
    const float3 d0 = origin - cp0;
    const float d0Sq = dot(d0, d0);
    const float dirLenSq = dot(dir, dir);
    const float3 triN = (faceCrossLenSq > 1.0e-12f) ? (faceCross * rsqrt(faceCrossLenSq)) : float3(0.0f, 1.0f, 0.0f);
    if(d0Sq <= radius * radius){
        outT = 0.0f;
        outRawDist = sqrt(max(d0Sq, 0.0f));
        if(d0Sq > 1.0e-12f)
            outNormal = d0 * rsqrt(d0Sq);
        else if(dirLenSq > 1.0e-12f)
            outNormal = nwbPhysicsSafeNormal(-dir, triN);
        else
            outNormal = triN;
        return true;
    }

    if(dirLenSq <= 1.0e-12f)
        return false;

    if(faceCrossLenSq > 1.0e-12f){
        const float signed0 = dot(triN, origin - v0);
        const float nDotD = dot(triN, dir);
        float tPlane = -1.0f;
        float3 faceHitN = triN;
        if(signed0 > radius && nDotD < -1.0e-12f){
            tPlane = (radius - signed0) / nDotD;
            faceHitN = triN;
        }
        else if(!cullBackfaces && signed0 < -radius && nDotD > 1.0e-12f){
            tPlane = (-radius - signed0) / nDotD;
            faceHitN = -triN;
        }
        if(tPlane >= 0.0f && tPlane <= maxT){
            const float3 planePt = (origin + dir * tPlane) - faceHitN * radius;
            const float edgeTol = -1.0e-5f * faceCrossLenSq;
            if(
                dot(cross(v1 - v0, planePt - v0), faceCross) >= edgeTol
                && dot(cross(v2 - v1, planePt - v1), faceCross) >= edgeTol
                && dot(cross(v0 - v2, planePt - v2), faceCross) >= edgeTol
            ){
                outT = tPlane;
                outNormal = faceHitN;
                outRawDist = radius;
                return true;
            }
        }
    }

    bool anyHit = false;
    float bestT = maxT;
    float3 bestN = float3(0.0f, 1.0f, 0.0f);
    float tCand = 0.0f;
    float3 nCand = float3(0.0f, 1.0f, 0.0f);
    if(nwbPhysicsQueryRaySphere(origin, dir, bestT, v0, radius, tCand, nCand)){
        anyHit = true;
        bestT = tCand;
        bestN = nCand;
    }
    if(nwbPhysicsQueryRaySphere(origin, dir, bestT, v1, radius, tCand, nCand)){
        anyHit = true;
        bestT = tCand;
        bestN = nCand;
    }
    if(nwbPhysicsQueryRaySphere(origin, dir, bestT, v2, radius, tCand, nCand)){
        anyHit = true;
        bestT = tCand;
        bestN = nCand;
    }
    nwbPhysicsQueryRayOpenCylinder(origin, dir, v0, v1, radius, bestT, anyHit, bestN);
    nwbPhysicsQueryRayOpenCylinder(origin, dir, v1, v2, radius, bestT, anyHit, bestN);
    nwbPhysicsQueryRayOpenCylinder(origin, dir, v2, v0, radius, bestT, anyHit, bestN);

    if(!anyHit)
        return false;
    outT = bestT;
    outNormal = bestN;
    outRawDist = radius;
    return true;
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
        outNormal = nwbPhysicsSafeNormal(diff, nwbPhysicsSafeNormal(-dir, float3(0.0f, 1.0f, 0.0f)));
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
        outNormal = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(localN, worldQ), float3(0.0f, 1.0f, 0.0f));
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
            enterN = nwbPhysicsSafeNormal(float3(hx, 0.0f, hz), float3(1.0f, 0.0f, 0.0f));
            hasEnter = true;
        }
        if(tr1 < tMax)
            tMax = tr1;
        if(tMin > tMax)
            return false;
    }

    if(hasEnter && tMin >= 0.0f && tMin <= maxFrac){
        outT = tMin;
        outNormal = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(enterN, worldQ), float3(0.0f, 1.0f, 0.0f));
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
        outNormal = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(shallowFaceN, bodyRot), float3(0.0f, 1.0f, 0.0f));
        return true;
    }
    if(hitFace != NWB_PHYSICS_INVALID_INDEX){
        outT = tLower;
        outNormal = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(hitLocalN, bodyRot), float3(0.0f, 1.0f, 0.0f));
        return true;
    }
    return false;
}
#endif

float3 nwbPhysicsQuerySafeInvDir(const float3 dir){
    return float3(
        abs(dir.x) > 1.0e-12f ? (1.0f / dir.x) : 1.0e30f,
        abs(dir.y) > 1.0e-12f ? (1.0f / dir.y) : 1.0e30f,
        abs(dir.z) > 1.0e-12f ? (1.0f / dir.z) : 1.0e30f
    );
}

#if !NWB_PHYSICS_QUERY_IS_SHAPE_OP
#if NWB_PHYSICS_QUERY_HAS_MESH
bool nwbPhysicsQueryPushMeshChildren(
    const uint meshNodeSlot,
    const uint nodeBase,
    const float4 n0,
    const uint meta,
    const float3 localOrig,
    const float3 localInvDir,
    const float3 padVec,
    const float bestT,
    inout uint stack[64],
    inout float stackMetric[64],
    inout uint sp
){
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
    if((hitL ? 1u : 0u) + (hitR ? 1u : 0u) + sp > 64u)
        return false;
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
    return true;
}

bool nwbPhysicsQueryMeshClosestOutwardNormal(
    const uint nodeBase,
    const uint triBase,
    const uint meshNodeSlot,
    const uint meshTriSlot,
    const float3 localPoint,
    out float3 outNormalLocal,
    out uint outTriIdx,
    out float3 outBary,
    out bool outOverflow,
    inout uint stack[64],
    inout float stackMetric[64]
){
    outNormalLocal = float3(0.0f, 1.0f, 0.0f);
    outTriIdx = NWB_PHYSICS_INVALID_INDEX;
    outBary = float3(0.0f, 0.0f, 0.0f);
    outOverflow = false;
    bool found = false;
    float bestDistSq = 1.0e30f;
    float3 bestCross = float3(0.0f, 1.0f, 0.0f);
    uint bestTriIdx = NWB_PHYSICS_INVALID_INDEX;
    float3 bestClosePt = float3(0.0f, 0.0f, 0.0f);
    float3 bestTv0 = float3(0.0f, 0.0f, 0.0f);
    float3 bestTv1 = float3(0.0f, 0.0f, 0.0f);
    float3 bestTv2 = float3(0.0f, 0.0f, 0.0f);
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
                const float3 closePt = nwbPhysicsClosestPointOnTriangle(localPoint, tv0, tv1, tv2);
                const float3 diff = localPoint - closePt;
                const float dSq = dot(diff, diff);
                if(dSq < bestDistSq){
                    bestDistSq = dSq;
                    found = true;
                    bestCross = cross(tv1 - tv0, tv2 - tv0);
                    bestTriIdx = firstTri + ti;
                    bestClosePt = closePt;
                    bestTv0 = tv0;
                    bestTv1 = tv1;
                    bestTv2 = tv2;
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
            if((keepL ? 1u : 0u) + (keepR ? 1u : 0u) + sp > 64u){
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
    if(found){
        outNormalLocal = nwbPhysicsSafeNormal(bestCross, float3(0.0f, 1.0f, 0.0f));
        outTriIdx = bestTriIdx;
        const float3 bw = nwbPhysicsClosestPointBaryTriangle(bestClosePt, bestTv0, bestTv1, bestTv2);
        outBary = float3(bw.y, bw.z, bw.x);
    }
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
    out uint outTriIdx,
    out float3 outBary,
    out bool outNeedsFallback,
    inout uint stack[64],
    inout float stackMetric[64]
){
    outT = maxFrac;
    outNormal = float3(0.0f, 1.0f, 0.0f);
    outTriIdx = NWB_PHYSICS_INVALID_INDEX;
    outBary = float3(0.0f, 0.0f, 0.0f);
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
        if(nwbPhysicsIsPointInsideClosedMesh(nodeBase, triBase, meshNodeSlot, meshTriSlot, localOrig, insideOverflow, stack)){
            if(meshRadius > 1.0e-5f){
                outNeedsFallback = true;
                return false;
            }
            float3 closeNLocal = float3(0.0f, 1.0f, 0.0f);
            bool closeOverflow = false;
            const bool foundClosest = nwbPhysicsQueryMeshClosestOutwardNormal(nodeBase, triBase, meshNodeSlot, meshTriSlot, localOrig, closeNLocal, outTriIdx, outBary, closeOverflow, stack, stackMetric);
            if(closeOverflow){
                outNeedsFallback = true;
                return false;
            }
            outT = 0.0f;
            if(foundClosest)
                outNormal = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(closeNLocal, bodyRot), float3(0.0f, 1.0f, 0.0f));
            else
                outNormal = nwbPhysicsSafeNormal(-nwbPhysicsQuatRotate(localDir, bodyRot), float3(0.0f, 1.0f, 0.0f));
            return true;
        }
        if(insideOverflow){
            outNeedsFallback = true;
            return false;
        }
    }

    const float3 localInvDir = nwbPhysicsQuerySafeInvDir(localDir);
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
    uint bestTriIdx = NWB_PHYSICS_INVALID_INDEX;
    float3 bestTv0 = float3(0.0f, 0.0f, 0.0f);
    float3 bestTv1 = float3(0.0f, 0.0f, 0.0f);
    float3 bestTv2 = float3(0.0f, 0.0f, 0.0f);

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
                        bestTriIdx = firstTri + ti;
                        bestTv0 = tv0;
                        bestTv1 = tv1;
                        bestTv2 = tv2;
                    }
                }
            }
        }
        else{
            if(!nwbPhysicsQueryPushMeshChildren(meshNodeSlot, nodeBase, n0, meta, localOrig, localInvDir, padVec, bestT, stack, stackMetric, sp)){
                outNeedsFallback = true;
                return false;
            }
        }
    }

    if(!anyHit)
        return false;
    outT = bestT;
    outNormal = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(bestLocalN, bodyRot), float3(0.0f, 1.0f, 0.0f));
    outTriIdx = bestTriIdx;
    const float3 bwRayMesh = nwbPhysicsClosestPointBaryTriangle(localOrig + localDir * bestT, bestTv0, bestTv1, bestTv2);
    outBary = float3(bwRayMesh.y, bwRayMesh.z, bwRayMesh.x);
    return true;
}

bool nwbPhysicsQuerySphereCastMesh(
    const uint meshNodeSlot,
    const uint meshTriSlot,
    const NwbPhysicsGpuShapeProxy spProxy,
    const float3 shapeOrig,
    const float4 shapeRot,
    const uint qFlags,
    const float3 localOrig,
    const float3 localDir,
    const float maxFrac,
    const float castRadius,
    out float outT,
    out float3 outPoint,
    out float3 outNormal,
    out uint outTriIdx,
    out float3 outBary,
    out bool outNeedsFallback,
    inout uint stack[64],
    inout float stackMetric[64]
){
    outT = maxFrac;
    outPoint = float3(0.0f, 0.0f, 0.0f);
    outNormal = float3(0.0f, 1.0f, 0.0f);
    outTriIdx = NWB_PHYSICS_INVALID_INDEX;
    outBary = float3(0.0f, 0.0f, 0.0f);
    outNeedsFallback = false;

    const uint nodeBase = spProxy.filterData.w;
    const uint triBase = asuint(spProxy.localRotationOrMeta.x);
    if(meshNodeSlot == NWB_PHYSICS_INVALID_INDEX || meshTriSlot == NWB_PHYSICS_INVALID_INDEX || nodeBase == NWB_PHYSICS_INVALID_INDEX || triBase == NWB_PHYSICS_INVALID_INDEX){
        outNeedsFallback = true;
        return false;
    }

    const float skinR = max(spProxy.localCenterRadius.w, 0.0f);
    const float totalR = max(castRadius + skinR, 0.0f);
    const float3 padVec = float3(totalR, totalR, totalR);
    const float4 firstTri1 = g_NwbPhysicsFloat4Buffers[meshTriSlot][triBase * 3u + 1u];
    const uint meshFlags = asuint(firstTri1.w);
    const bool closedMesh = (meshFlags & NWB_PHYSICS_MESH_FLAG_CLOSED) != 0u;
    const bool cullBackfaces = ((meshFlags & NWB_PHYSICS_MESH_FLAG_CULL_BACKFACES) != 0u) || ((qFlags & NWB_PHYSICS_QUERY_FLAG_CULL_BACKFACES) != 0u);
    if(closedMesh){
        bool insideOverflow = false;
        if(nwbPhysicsIsPointInsideClosedMesh(nodeBase, triBase, meshNodeSlot, meshTriSlot, localOrig, insideOverflow, stack)){
            float3 closeNLocal = float3(0.0f, 1.0f, 0.0f);
            bool closeOverflow = false;
            const bool foundClosest = nwbPhysicsQueryMeshClosestOutwardNormal(nodeBase, triBase, meshNodeSlot, meshTriSlot, localOrig, closeNLocal, outTriIdx, outBary, closeOverflow, stack, stackMetric);
            if(closeOverflow){
                outNeedsFallback = true;
                return false;
            }
            outT = 0.0f;
            outPoint = shapeOrig + nwbPhysicsQuatRotate(localOrig, shapeRot);
            const float3 worldDir = nwbPhysicsQuatRotate(localDir, shapeRot);
            const float3 closeNW = foundClosest
                ? nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(closeNLocal, shapeRot), float3(0.0f, 1.0f, 0.0f))
                : nwbPhysicsSafeNormal(-worldDir, float3(0.0f, 1.0f, 0.0f));
            outNormal = (castRadius > 0.0f && dot(localDir, localDir) > 1.0e-12f)
                ? nwbPhysicsSafeNormal(-worldDir, closeNW)
                : closeNW;
            return true;
        }
        if(insideOverflow){
            outNeedsFallback = true;
            return false;
        }
    }

    const float3 localInvDir = nwbPhysicsQuerySafeInvDir(localDir);
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
    float bestRawDist = totalR;
    uint bestTriIdx = NWB_PHYSICS_INVALID_INDEX;
    float3 bestTv0 = float3(0.0f, 0.0f, 0.0f);
    float3 bestTv1 = float3(0.0f, 0.0f, 0.0f);
    float3 bestTv2 = float3(0.0f, 0.0f, 0.0f);

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
                const float3 triMin = min(tv0, min(tv1, tv2)) - padVec;
                const float3 triMax = max(tv0, max(tv1, tv2)) + padVec;
                float triBoxT = 0.0f;
                if(!nwbPhysicsQueryRayAabbFast(localOrig, localInvDir, triMin, triMax, bestT, triBoxT))
                    continue;
                float tTri = 0.0f;
                float3 nTri = float3(0.0f, 1.0f, 0.0f);
                float rawDistTri = 0.0f;
                if(nwbPhysicsQuerySweptSphereTriangle(localOrig, localDir, totalR, tv0, tv1, tv2, cullBackfaces, bestT, tTri, nTri, rawDistTri)){
                    if(!anyHit || tTri < bestT || (tTri == 0.0f && bestT == 0.0f && rawDistTri < bestRawDist)){
                        anyHit = true;
                        bestT = tTri;
                        bestLocalN = nTri;
                        bestRawDist = rawDistTri;
                        bestTriIdx = firstTri + ti;
                        bestTv0 = tv0;
                        bestTv1 = tv1;
                        bestTv2 = tv2;
                    }
                }
            }
        }
        else{
            if(!nwbPhysicsQueryPushMeshChildren(meshNodeSlot, nodeBase, n0, meta, localOrig, localInvDir, padVec, bestT, stack, stackMetric, sp)){
                outNeedsFallback = true;
                return false;
            }
        }
    }

    if(!anyHit)
        return false;
    outT = bestT;
    const float surfDist = max(bestRawDist - skinR, 0.0f);
    const float3 sampleLocal = localOrig + localDir * bestT;
    const float3 hitPtLocal = sampleLocal - bestLocalN * min(surfDist, castRadius);
    outPoint = shapeOrig + nwbPhysicsQuatRotate(hitPtLocal, shapeRot);
    outNormal = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(bestLocalN, shapeRot), float3(0.0f, 1.0f, 0.0f));
    outTriIdx = bestTriIdx;
    const float3 cpMesh = nwbPhysicsClosestPointOnTriangle(sampleLocal, bestTv0, bestTv1, bestTv2);
    const float3 bwScMesh = nwbPhysicsClosestPointBaryTriangle(cpMesh, bestTv0, bestTv1, bestTv2);
    outBary = float3(bwScMesh.y, bwScMesh.z, bwScMesh.x);
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
    out uint outTriIdx,
    out float3 outBary,
    out bool outOverflow,
    inout uint stack[64],
    inout float stackMetric[64]
){
    outNormal = float3(0.0f, 1.0f, 0.0f);
    outTriIdx = NWB_PHYSICS_INVALID_INDEX;
    outBary = float3(0.0f, 0.0f, 0.0f);
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
    uint bestTriIdx = NWB_PHYSICS_INVALID_INDEX;
    float3 bestClosePt = float3(0.0f, 0.0f, 0.0f);
    float3 bestTv0 = float3(0.0f, 0.0f, 0.0f);
    float3 bestTv1 = float3(0.0f, 0.0f, 0.0f);
    float3 bestTv2 = float3(0.0f, 0.0f, 0.0f);

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
                const float3 closePt = nwbPhysicsClosestPointOnTriangle(localPoint, tv0, tv1, tv2);
                const float3 diff = localPoint - closePt;
                const float dSq = dot(diff, diff);
                if(dSq <= minDistSq){
                    minDistSq = dSq;
                    foundTri = true;
                    bestDiff = diff;
                    bestTriCross = cross(tv1 - tv0, tv2 - tv0);
                    bestTriIdx = firstTri + ti;
                    bestClosePt = closePt;
                    bestTv0 = tv0;
                    bestTv1 = tv1;
                    bestTv2 = tv2;
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
            if((keepL ? 1u : 0u) + (keepR ? 1u : 0u) + sp > 64u){
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
        const float3 fallbackN = nwbPhysicsSafeNormal(bestTriCross, float3(0.0f, 1.0f, 0.0f));
        bestLocalN = nwbPhysicsSafeNormal(bestDiff, fallbackN);
        outTriIdx = bestTriIdx;
        const float3 bwPtMesh = nwbPhysicsClosestPointBaryTriangle(bestClosePt, bestTv0, bestTv1, bestTv2);
        outBary = float3(bwPtMesh.y, bwPtMesh.z, bwPtMesh.x);
    }
    outNormal = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(bestLocalN, bodyRot), float3(0.0f, 1.0f, 0.0f));
    if(foundTri && minDistSq <= skinR * skinR)
        return 0.0f;
    if(closedMesh){
        bool insideOverflow = false;
        const bool insideClosed = nwbPhysicsIsPointInsideClosedMesh(nodeBase, triBase, meshNodeSlot, meshTriSlot, localPoint, insideOverflow, stack);
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
#endif

bool nwbPhysicsQueryRayTaperedCapsule(
    const float3 origin,
    const float3 dir,
    const float maxFrac,
    const float3 p0,
    const float3 p1,
    const float r0,
    const float r1,
    out float outT,
    out float3 outNormal
){
    const float3 seg = p1 - p0;
    const float segLenSq = dot(seg, seg);
    if(segLenSq <= 1.0e-12f)
        return nwbPhysicsQueryRaySphere(origin, dir, maxFrac, r0 >= r1 ? p0 : p1, max(r0, r1), outT, outNormal);
    const float L = sqrt(segLenSq);
    if(abs(r0 - r1) >= L - 1.0e-6f){
        const float3 domC = (r0 >= r1) ? p0 : p1;
        const float domR = max(r0, r1);
        return nwbPhysicsQueryRaySphere(origin, dir, maxFrac, domC, domR, outT, outNormal);
    }

    const float3 axis = seg / L;
    const float sinA = (r0 - r1) / L;
    const float cosA = sqrt(max(1.0f - sinA * sinA, 0.0f));
    const float3 relStart = origin - p0;
    const float mDotA = dot(relStart, axis);
    const float3 startPerp = relStart - axis * mDotA;
    const float startX = length(startPerp);
    const float sStart = -startX * sinA + mDotA * cosA;
    if(dot(relStart, relStart) <= r0 * r0 || dot(origin - p1, origin - p1) <= r1 * r1 || (sStart >= 0.0f && sStart <= L * cosA && (startX * cosA + mDotA * sinA) <= r0)){
        outT = 0.0f;
        outNormal = nwbPhysicsSafeNormal(-dir, axis);
        return true;
    }

    bool anyHit = false;
    float bestT = maxFrac;
    float3 bestN = float3(0.0f, 1.0f, 0.0f);
    float tCap = 0.0f;
    float3 nCap = float3(0.0f, 1.0f, 0.0f);
    if(nwbPhysicsQueryRaySphere(origin, dir, bestT, p0, r0, tCap, nCap)){
        anyHit = true;
        bestT = tCap;
        bestN = nCap;
    }
    if(nwbPhysicsQueryRaySphere(origin, dir, bestT, p1, r1, tCap, nCap)){
        anyHit = true;
        bestT = tCap;
        bestN = nCap;
    }

    const float dirDotA = dot(dir, axis);
    const float3 dirPerp = dir - axis * dirDotA;
    const float c2 = cosA * cosA;
    const float k0 = r0 - mDotA * sinA;
    const float k1 = -dirDotA * sinA;
    const float qa = dot(dirPerp, dirPerp) * c2 - k1 * k1;
    const float qb = 2.0f * (dot(startPerp, dirPerp) * c2 - k0 * k1);
    const float qc = dot(startPerp, startPerp) * c2 - k0 * k0;
    const float yMinMantle = r0 * sinA;
    const float yMaxMantle = L + r1 * sinA;
    if(abs(qa) > 1.0e-10f){
        const float disc = qb * qb - 4.0f * qa * qc;
        if(disc >= 0.0f){
            const float sqrtDisc = sqrt(disc);
            const float inv2A = 0.5f / qa;
            const float roots[2] = { (-qb - sqrtDisc) * inv2A, (-qb + sqrtDisc) * inv2A };
            for(uint ri = 0u; ri < 2u; ++ri){
                const float t = roots[ri];
                if(t >= 0.0f && t <= bestT){
                    const float yHit = mDotA + t * dirDotA;
                    if(yHit >= yMinMantle - 1.0e-5f && yHit <= yMaxMantle + 1.0e-5f){
                        const float3 hitPerp = startPerp + dirPerp * t;
                        const float3 candN = nwbPhysicsSafeNormal(nwbPhysicsSafeNormal(hitPerp, axis) * cosA + axis * sinA, axis);
                        if(dot(candN, dir) < 0.0f){
                            anyHit = true;
                            bestT = t;
                            bestN = candN;
                        }
                    }
                }
            }
        }
    }
    else if(abs(qb) > 1.0e-10f){
        const float t = -qc / qb;
        if(t >= 0.0f && t <= bestT){
            const float yHit = mDotA + t * dirDotA;
            if(yHit >= yMinMantle - 1.0e-5f && yHit <= yMaxMantle + 1.0e-5f){
                const float3 hitPerp = startPerp + dirPerp * t;
                const float3 candN = nwbPhysicsSafeNormal(nwbPhysicsSafeNormal(hitPerp, axis) * cosA + axis * sinA, axis);
                if(dot(candN, dir) < 0.0f){
                    anyHit = true;
                    bestT = t;
                    bestN = candN;
                }
            }
        }
    }

    outT = bestT;
    outNormal = bestN;
    return anyHit;
}

bool nwbPhysicsQueryRayConeSharp(
    const float3 origin,
    const float3 dir,
    const float maxFrac,
    const float3 center,
    const float4 worldQ,
    const float rCone,
    const float hhCone,
    out float outT,
    out float3 outNormal
){
    outT = maxFrac;
    outNormal = float3(0.0f, 1.0f, 0.0f);
    const float3 ro = nwbPhysicsQuatInvRotate(origin - center, worldQ);
    const float3 rd = nwbPhysicsQuatInvRotate(dir, worldQ);
    const float r = max(rCone, 0.0f);
    const float hh = max(hhCone, 0.0f);
    const float rAtStart = (hh > 1.0e-6f) ? (r * (hh - ro.y) / (2.0f * hh)) : r;
    if(ro.y >= -hh && ro.y <= hh && (ro.x * ro.x + ro.z * ro.z) <= rAtStart * rAtStart){
        outT = 0.0f;
        outNormal = nwbPhysicsSafeNormal(-dir, float3(0.0f, 1.0f, 0.0f));
        return true;
    }

    bool anyHit = false;
    float bestT = maxFrac;
    float3 bestLocalN = float3(0.0f, 1.0f, 0.0f);

    if(abs(rd.y) > 1.0e-8f){
        const float tBase = (-hh - ro.y) / rd.y;
        if(tBase >= 0.0f && tBase <= bestT && rd.y > 0.0f){
            const float hx = ro.x + tBase * rd.x;
            const float hz = ro.z + tBase * rd.z;
            if((hx * hx + hz * hz) <= r * r + 1.0e-6f){
                anyHit = true;
                bestT = tBase;
                bestLocalN = float3(0.0f, -1.0f, 0.0f);
            }
        }
    }

    if(hh > 1.0e-6f && r > 1.0e-6f){
        const float slope = r / (2.0f * hh);
        const float slopeSq = slope * slope;
        const float dy0 = hh - ro.y;
        const float s = -rd.y;
        const float qa = (rd.x * rd.x + rd.z * rd.z) - slopeSq * s * s;
        const float qb = 2.0f * ((ro.x * rd.x + ro.z * rd.z) - slopeSq * dy0 * s);
        const float qc = (ro.x * ro.x + ro.z * ro.z) - slopeSq * dy0 * dy0;
        if(abs(qa) > 1.0e-10f){
            const float disc = qb * qb - 4.0f * qa * qc;
            if(disc >= 0.0f){
                const float sqrtDisc = sqrt(disc);
                const float inv2A = 0.5f / qa;
                const float roots[2] = { (-qb - sqrtDisc) * inv2A, (-qb + sqrtDisc) * inv2A };
                for(uint ri = 0u; ri < 2u; ++ri){
                    const float t = roots[ri];
                    if(t >= 0.0f && t <= bestT){
                        const float hy = ro.y + t * rd.y;
                        if(hy >= -hh - 1.0e-5f && hy <= hh + 1.0e-5f){
                            const float hx = ro.x + t * rd.x;
                            const float hz = ro.z + t * rd.z;
                            const float3 nLoc = nwbPhysicsSafeNormal(float3(hx, slopeSq * (hh - hy), hz), float3(0.0f, 1.0f, 0.0f));
                            if(dot(nLoc, rd) < 0.0f){
                                anyHit = true;
                                bestT = t;
                                bestLocalN = nLoc;
                            }
                        }
                    }
                }
            }
        }
        else if(abs(qb) > 1.0e-10f){
            const float t = -qc / qb;
            if(t >= 0.0f && t <= bestT){
                const float hy = ro.y + t * rd.y;
                if(hy >= -hh - 1.0e-5f && hy <= hh + 1.0e-5f){
                    const float hx = ro.x + t * rd.x;
                    const float hz = ro.z + t * rd.z;
                    const float3 nLoc = nwbPhysicsSafeNormal(float3(hx, slopeSq * (hh - hy), hz), float3(0.0f, 1.0f, 0.0f));
                    if(dot(nLoc, rd) < 0.0f){
                        anyHit = true;
                        bestT = t;
                        bestLocalN = nLoc;
                    }
                }
            }
        }
    }

    if(!anyHit)
        return false;
    outT = bestT;
    outNormal = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(bestLocalN, worldQ), float3(0.0f, 1.0f, 0.0f));
    return true;
}

void nwbPhysicsQueryStepSimplex(inout float3 sW[4], inout uint sIdx[4], inout float sU[4], inout uint count){
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
    const uint pIdx[4] = sIdx;
    count = nextCount;
    for(uint k = 0u; k < nextCount; ++k){
        const uint src = slots[k];
        sW[k] = pW[src];
        sIdx[k] = pIdx[src];
        sU[k] = weights[k];
    }
}

void nwbPhysicsQueryHullPointGjk(
    const uint hullVertexSlot,
    const uint vOffset,
    const uint vCount,
    const float3 localP,
    out float3 outClosestDiff,
    out bool outInside
){
    outClosestDiff = float3(0.0f, 0.0f, 0.0f);
    outInside = false;
    if(vCount == 0u)
        return;

    float3 sW[4];
    uint sIdx[4];
    float sU[4];
    for(uint k = 0u; k < 4u; ++k){
        sW[k] = float3(0.0f, 0.0f, 0.0f);
        sIdx[k] = 0u;
        sU[k] = 0.0f;
    }

    sIdx[0] = 0u;
    sW[0] = g_NwbPhysicsFloat4Buffers[hullVertexSlot][vOffset].xyz - localP;
    sU[0] = 1.0f;
    uint sCount = 1u;

    for(uint iter = 0u; iter < 20u; ++iter){
        if(sCount >= 2u)
            nwbPhysicsQueryStepSimplex(sW, sIdx, sU, sCount);

        if(sCount == 4u){
            outInside = true;
            return;
        }

        float3 pClosest = float3(0.0f, 0.0f, 0.0f);
        for(uint k = 0u; k < sCount; ++k)
            pClosest += sW[k] * sU[k];

        const float pLenSq = dot(pClosest, pClosest);
        if(pLenSq <= 1.0e-12f){
            outInside = true;
            return;
        }
        if(iter == 19u)
            break;

        const float3 dir = -pClosest;
        uint bestIdx = 0u;
        float3 bestV = g_NwbPhysicsFloat4Buffers[hullVertexSlot][vOffset].xyz;
        float bestDot = dot(bestV, dir);
        for(uint i = 1u; i < vCount; ++i){
            const float3 v = g_NwbPhysicsFloat4Buffers[hullVertexSlot][vOffset + i].xyz;
            const float dv = dot(v, dir);
            if(dv > bestDot){
                bestDot = dv;
                bestIdx = i;
                bestV = v;
            }
        }

        bool dup = false;
        for(uint k = 0u; k < sCount; ++k){
            if(sIdx[k] == bestIdx){
                dup = true;
                break;
            }
        }
        if(dup)
            break;

        const float3 wNew = bestV - localP;
        if(dot(pClosest - wNew, pClosest) <= 1.0e-5f * pLenSq)
            break;

        sW[sCount] = wNew;
        sIdx[sCount] = bestIdx;
        sU[sCount] = 0.0f;
        ++sCount;
    }

    float3 finalP = float3(0.0f, 0.0f, 0.0f);
    for(uint k = 0u; k < sCount; ++k)
        finalP += sW[k] * sU[k];
    outClosestDiff = -finalP;
}

void nwbPhysicsHfInitDda(
    const float3 localOrig,
    const float3 localDir,
    const float3 hfOrigin,
    const float sx,
    const float sz,
    const int cx,
    const int cz,
    out int outStepX,
    out int outStepZ,
    out float outTDeltaX,
    out float outTDeltaZ,
    out float outTMaxX,
    out float outTMaxZ
){
    outStepX = (localDir.x > 1.0e-12f) ? 1 : ((localDir.x < -1.0e-12f) ? -1 : 0);
    outStepZ = (localDir.z > 1.0e-12f) ? 1 : ((localDir.z < -1.0e-12f) ? -1 : 0);
    outTDeltaX = (outStepX != 0) ? (sx / abs(localDir.x)) : 1.0e30f;
    outTDeltaZ = (outStepZ != 0) ? (sz / abs(localDir.z)) : 1.0e30f;
    outTMaxX = 1.0e30f;
    if(outStepX != 0){
        const float nextX = hfOrigin.x + float(cx + (outStepX > 0 ? 1 : 0)) * sx;
        outTMaxX = (nextX - localOrig.x) / localDir.x;
    }
    outTMaxZ = 1.0e30f;
    if(outStepZ != 0){
        const float nextZ = hfOrigin.z + float(cz + (outStepZ > 0 ? 1 : 0)) * sz;
        outTMaxZ = (nextZ - localOrig.z) / localDir.z;
    }
}

bool nwbPhysicsHfStepDda(
    const int stepX,
    const int stepZ,
    const float tDeltaX,
    const float tDeltaZ,
    const float tLimit,
    inout int cx,
    inout int cz,
    inout float tMaxX,
    inout float tMaxZ,
    inout float curT
){
    if(tMaxX <= tMaxZ){
        if(stepX == 0 || tMaxX > tLimit)
            return false;
        curT = tMaxX;
        tMaxX += tDeltaX;
        cx += stepX;
        return true;
    }
    if(stepZ == 0 || tMaxZ > tLimit)
        return false;
    curT = tMaxZ;
    tMaxZ += tDeltaZ;
    cz += stepZ;
    return true;
}

bool nwbPhysicsQueryRayHeightfield(
    const uint hullVertexSlot,
    const NwbPhysicsGpuShapeProxy spProxy,
    const float4 shapeRot,
    const uint qFlags,
    const float3 localOrig,
    const float3 localDir,
    const float maxFrac,
    out float outT,
    out float3 outNormal,
    out uint outTriIdx,
    out float3 outBary,
    out bool outNeedsFallback
){
    outT = maxFrac;
    outNormal = float3(0.0f, 1.0f, 0.0f);
    outTriIdx = NWB_PHYSICS_INVALID_INDEX;
    outBary = float3(0.0f, 0.0f, 0.0f);
    outNeedsFallback = false;

    const uint hfBase = spProxy.filterData.w;
    NwbPhysicsHfHeader hf;
    if(hullVertexSlot == NWB_PHYSICS_INVALID_INDEX || hfBase == NWB_PHYSICS_INVALID_INDEX || !nwbPhysicsHfLoadHeader(hullVertexSlot, hfBase, hf)){
        outNeedsFallback = true;
        return false;
    }
    const float3 hfOrigin = hf.origin;
    const float hfMaxY = hf.maxY;
    const float sx = hf.sx;
    const float sy = hf.sy;
    const float sz = hf.sz;
    const float hfMinY = hf.minY;
    const uint cols = hf.cols;
    const uint rows = hf.rows;
    const uint hfFlags = hf.flags;
    const float hfThickness = hf.thickness;

    const float3 gridMin = float3(hfOrigin.x, hfMinY - hfThickness, hfOrigin.z);
    const float3 gridMax = float3(hfOrigin.x + float(cols - 1u) * sx, hfMaxY + hfThickness, hfOrigin.z + float(rows - 1u) * sz);
    const float3 localInvDir = nwbPhysicsQuerySafeInvDir(localDir);
    const float3 t0Vec = (gridMin - localOrig) * localInvDir;
    const float3 t1Vec = (gridMax - localOrig) * localInvDir;
    const float3 tSmall = min(t0Vec, t1Vec);
    const float3 tLarge = max(t0Vec, t1Vec);
    const float tEnter = max(0.0f, max(tSmall.x, max(tSmall.y, tSmall.z)));
    const float tExit = min(maxFrac, min(tLarge.x, min(tLarge.y, tLarge.z)));
    if(tExit < tEnter)
        return false;

    const int cellCols = int(cols - 1u);
    const int cellRows = int(rows - 1u);
    const float3 startPt = localOrig + localDir * tEnter;
    int cx = clamp(int(floor((startPt.x - hfOrigin.x) / sx)), 0, cellCols - 1);
    int cz = clamp(int(floor((startPt.z - hfOrigin.z) / sz)), 0, cellRows - 1);

    int stepX;
    int stepZ;
    float tDeltaX;
    float tDeltaZ;
    float tMaxX;
    float tMaxZ;
    nwbPhysicsHfInitDda(localOrig, localDir, hfOrigin, sx, sz, cx, cz, stepX, stepZ, tDeltaX, tDeltaZ, tMaxX, tMaxZ);

    const bool flipDiag = (hfFlags & NWB_PHYSICS_HF_FLAG_FLIP_DIAGONAL) != 0u;
    const bool cullBackfaces = (qFlags & NWB_PHYSICS_QUERY_FLAG_CULL_BACKFACES) != 0u;
    float curT = tEnter;
    float bestT = maxFrac;
    float3 bestLocalN = float3(0.0f, 1.0f, 0.0f);
    uint bestTriIdx = NWB_PHYSICS_INVALID_INDEX;
    float3 bestTv0 = float3(0.0f, 0.0f, 0.0f);
    float3 bestTv1 = float3(0.0f, 0.0f, 0.0f);
    float3 bestTv2 = float3(0.0f, 0.0f, 0.0f);
    bool anyHit = false;
    bool finishedTraversal = false;
    const float3 zeroVec = float3(0.0f, 0.0f, 0.0f);

    [loop]
    for(uint step = 0u; step < 512u; ++step){
        if(curT > bestT || curT > tExit){
            finishedTraversal = true;
            break;
        }
        float3 v00 = zeroVec;
        float3 v10 = zeroVec;
        float3 v01 = zeroVec;
        float3 v11 = zeroVec;
        if(nwbPhysicsHfLoadCellVertices(hullVertexSlot, hfBase, cols, rows, hfOrigin, sx, sz, sy, hfFlags, uint(cx), uint(cz), v00, v10, v01, v11)){
            [loop]
            for(uint ti = 0u; ti < 2u; ++ti){
                float3 tv0 = zeroVec;
                float3 tv1 = zeroVec;
                float3 tv2 = zeroVec;
                nwbPhysicsHfExtractTriangle(flipDiag, ti, v00, v10, v01, v11, tv0, tv1, tv2);
                float triT = 0.0f;
                float3 triN = float3(0.0f, 1.0f, 0.0f);
                if(nwbPhysicsQueryRayTriangle(localOrig, localDir, tv0, tv1, tv2, cullBackfaces, bestT, triT, triN)){
                    if(!anyHit || triT < bestT){
                        anyHit = true;
                        bestT = triT;
                        bestLocalN = triN;
                        bestTriIdx = (uint(cz) * (cols - 1u) + uint(cx)) * 2u + ti;
                        bestTv0 = tv0;
                        bestTv1 = tv1;
                        bestTv2 = tv2;
                    }
                }
            }
        }
        if(!nwbPhysicsHfStepDda(stepX, stepZ, tDeltaX, tDeltaZ, min(bestT, tExit), cx, cz, tMaxX, tMaxZ, curT)){
            finishedTraversal = true;
            break;
        }
        if(cx < 0 || cx >= cellCols || cz < 0 || cz >= cellRows){
            finishedTraversal = true;
            break;
        }
    }

    if(!finishedTraversal && !anyHit){
        outNeedsFallback = true;
        return false;
    }
    if(!anyHit)
        return false;

    outT = bestT;
    outNormal = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(bestLocalN, shapeRot), float3(0.0f, 1.0f, 0.0f));
    outTriIdx = bestTriIdx;
    const float3 bwRayHf = nwbPhysicsClosestPointBaryTriangle(localOrig + localDir * bestT, bestTv0, bestTv1, bestTv2);
    outBary = float3(bwRayHf.y, bwRayHf.z, bwRayHf.x);
    return true;
}

void nwbPhysicsQuerySphereCastHfCell(
    const uint hullVertexSlot,
    const uint hfBase,
    const uint cols,
    const uint rows,
    const float3 hfOrigin,
    const float sx,
    const float sz,
    const float sy,
    const uint hfFlags,
    const bool flipDiag,
    const bool cullBackfaces,
    const float hfMinY,
    const float hfMaxY,
    const int cx,
    const int cz,
    const float3 localOrig,
    const float3 localDir,
    const float3 localInvDir,
    const float totalR,
    inout bool anyHit,
    inout float bestT,
    inout float3 bestLocalN,
    inout float bestRawDist,
    inout uint bestTriIdx,
    inout float3 bestTv0,
    inout float3 bestTv1,
    inout float3 bestTv2
){
    if(cx < 0 || cx >= int(cols - 1u) || cz < 0 || cz >= int(rows - 1u))
        return;

    const float cellX0 = hfOrigin.x + float(cx) * sx;
    const float cellZ0 = hfOrigin.z + float(cz) * sz;
    const float3 padVec = float3(totalR, totalR, totalR);
    float boxT = 0.0f;
    if(!nwbPhysicsQueryRayAabbFast(localOrig, localInvDir, float3(cellX0, hfMinY, cellZ0) - padVec, float3(cellX0 + sx, hfMaxY, cellZ0 + sz) + padVec, bestT, boxT))
        return;

    const float3 zeroVec = float3(0.0f, 0.0f, 0.0f);
    float3 v00 = zeroVec;
    float3 v10 = zeroVec;
    float3 v01 = zeroVec;
    float3 v11 = zeroVec;
    if(!nwbPhysicsHfLoadCellVertices(hullVertexSlot, hfBase, cols, rows, hfOrigin, sx, sz, sy, hfFlags, uint(cx), uint(cz), v00, v10, v01, v11))
        return;

    const float cellMinY = min(min(v00.y, v10.y), min(v01.y, v11.y));
    const float cellMaxY = max(max(v00.y, v10.y), max(v01.y, v11.y));
    if(!nwbPhysicsQueryRayAabbFast(localOrig, localInvDir, float3(cellX0, cellMinY, cellZ0) - padVec, float3(cellX0 + sx, cellMaxY, cellZ0 + sz) + padVec, bestT, boxT))
        return;

    [loop]
    for(uint ti = 0u; ti < 2u; ++ti){
        float3 tv0 = zeroVec;
        float3 tv1 = zeroVec;
        float3 tv2 = zeroVec;
        nwbPhysicsHfExtractTriangle(flipDiag, ti, v00, v10, v01, v11, tv0, tv1, tv2);
        float triT = 0.0f;
        float3 triN = float3(0.0f, 1.0f, 0.0f);
        float rawDistTri = 0.0f;
        if(nwbPhysicsQuerySweptSphereTriangle(localOrig, localDir, totalR, tv0, tv1, tv2, cullBackfaces, bestT, triT, triN, rawDistTri)){
            if(!anyHit || triT < bestT || (triT == 0.0f && bestT == 0.0f && rawDistTri < bestRawDist)){
                anyHit = true;
                bestT = triT;
                bestLocalN = triN;
                bestRawDist = rawDistTri;
                bestTriIdx = (uint(cz) * (cols - 1u) + uint(cx)) * 2u + ti;
                bestTv0 = tv0;
                bestTv1 = tv1;
                bestTv2 = tv2;
            }
        }
    }
}

bool nwbPhysicsQuerySphereCastHeightfield(
    const uint hullVertexSlot,
    const NwbPhysicsGpuShapeProxy spProxy,
    const float3 shapeOrig,
    const float4 shapeRot,
    const uint qFlags,
    const float3 localOrig,
    const float3 localDir,
    const float maxFrac,
    const float castRadius,
    out float outT,
    out float3 outPoint,
    out float3 outNormal,
    out uint outTriIdx,
    out float3 outBary,
    out bool outNeedsFallback
){
    outT = maxFrac;
    outPoint = float3(0.0f, 0.0f, 0.0f);
    outNormal = float3(0.0f, 1.0f, 0.0f);
    outTriIdx = NWB_PHYSICS_INVALID_INDEX;
    outBary = float3(0.0f, 0.0f, 0.0f);
    outNeedsFallback = false;

    const uint hfBase = spProxy.filterData.w;
    NwbPhysicsHfHeader hf;
    if(hullVertexSlot == NWB_PHYSICS_INVALID_INDEX || hfBase == NWB_PHYSICS_INVALID_INDEX || !nwbPhysicsHfLoadHeader(hullVertexSlot, hfBase, hf)){
        outNeedsFallback = true;
        return false;
    }
    const float3 hfOrigin = hf.origin;
    const float hfMaxY = hf.maxY;
    const float sx = hf.sx;
    const float sy = hf.sy;
    const float sz = hf.sz;
    const float hfMinY = hf.minY;
    const uint cols = hf.cols;
    const uint rows = hf.rows;
    const uint hfFlags = hf.flags;
    const float hfThickness = hf.thickness;

    const float totalR = max(castRadius + hfThickness, 0.0f);
    const float3 gridMin = float3(hfOrigin.x - totalR, hfMinY - totalR, hfOrigin.z - totalR);
    const float3 gridMax = float3(hfOrigin.x + float(cols - 1u) * sx + totalR, hfMaxY + totalR, hfOrigin.z + float(rows - 1u) * sz + totalR);
    const float3 localInvDir = float3(
        abs(localDir.x) > 1.0e-12f ? (1.0f / localDir.x) : 1.0e30f,
        abs(localDir.y) > 1.0e-12f ? (1.0f / localDir.y) : 1.0e30f,
        abs(localDir.z) > 1.0e-12f ? (1.0f / localDir.z) : 1.0e30f
    );
    const float3 t0Vec = (gridMin - localOrig) * localInvDir;
    const float3 t1Vec = (gridMax - localOrig) * localInvDir;
    const float3 tSmall = min(t0Vec, t1Vec);
    const float3 tLarge = max(t0Vec, t1Vec);
    const float tEnter = max(0.0f, max(tSmall.x, max(tSmall.y, tSmall.z)));
    const float tExit = min(maxFrac, min(tLarge.x, min(tLarge.y, tLarge.z)));
    if(tExit < tEnter)
        return false;

    const int maxCellX = int(cols - 2u);
    const int maxCellZ = int(rows - 2u);
    const float3 startPt = localOrig + localDir * tEnter;
    const float3 endPt = localOrig + localDir * tExit;
    const float2 sweepMinXZ = min(startPt.xz, endPt.xz) - float2(totalR, totalR);
    const float2 sweepMaxXZ = max(startPt.xz, endPt.xz) + float2(totalR, totalR);
    const int rawCx0 = int(floor((sweepMinXZ.x - hfOrigin.x) / sx));
    const int rawCx1 = int(floor((sweepMaxXZ.x - hfOrigin.x) / sx));
    const int rawCz0 = int(floor((sweepMinXZ.y - hfOrigin.z) / sz));
    const int rawCz1 = int(floor((sweepMaxXZ.y - hfOrigin.z) / sz));
    if(rawCx1 < 0 || rawCx0 > maxCellX || rawCz1 < 0 || rawCz0 > maxCellZ)
        return false;

    const int cx0 = max(rawCx0, 0);
    const int cx1 = min(rawCx1, maxCellX);
    const int cz0 = max(rawCz0, 0);
    const int cz1 = min(rawCz1, maxCellZ);
    const bool flipDiag = (hfFlags & NWB_PHYSICS_HF_FLAG_FLIP_DIAGONAL) != 0u;
    const bool cullBackfaces = (qFlags & NWB_PHYSICS_QUERY_FLAG_CULL_BACKFACES) != 0u;
    float bestT = maxFrac;
    float3 bestLocalN = float3(0.0f, 1.0f, 0.0f);
    float bestRawDist = totalR;
    uint bestTriIdx = NWB_PHYSICS_INVALID_INDEX;
    float3 bestTv0 = float3(0.0f, 0.0f, 0.0f);
    float3 bestTv1 = float3(0.0f, 0.0f, 0.0f);
    float3 bestTv2 = float3(0.0f, 0.0f, 0.0f);
    bool anyHit = false;

    const uint cellSpan = uint(cx1 - cx0 + 1) * uint(cz1 - cz0 + 1);
    if(cellSpan <= 256u){
        [loop]
        for(int cz = cz0; cz <= cz1; ++cz){
            [loop]
            for(int cx = cx0; cx <= cx1; ++cx)
                nwbPhysicsQuerySphereCastHfCell(hullVertexSlot, hfBase, cols, rows, hfOrigin, sx, sz, sy, hfFlags, flipDiag, cullBackfaces, hfMinY, hfMaxY, cx, cz, localOrig, localDir, localInvDir, totalR, anyHit, bestT, bestLocalN, bestRawDist, bestTriIdx, bestTv0, bestTv1, bestTv2);
        }
    }
    else{
        const int haloX = int(ceil(totalR / sx));
        const int haloZ = int(ceil(totalR / sz));
        if(haloX > 2 || haloZ > 2){
            outNeedsFallback = true;
            return false;
        }

        int cx = int(floor((startPt.x - hfOrigin.x) / sx));
        int cz = int(floor((startPt.z - hfOrigin.z) / sz));
        if(haloX == 0)
            cx = clamp(cx, 0, maxCellX);
        if(haloZ == 0)
            cz = clamp(cz, 0, maxCellZ);

        int stepX;
        int stepZ;
        float tDeltaX;
        float tDeltaZ;
        float tMaxX;
        float tMaxZ;
        nwbPhysicsHfInitDda(localOrig, localDir, hfOrigin, sx, sz, cx, cz, stepX, stepZ, tDeltaX, tDeltaZ, tMaxX, tMaxZ);

        float curT = tEnter;
        bool finishedTraversal = false;
        [loop]
        for(uint step = 0u; step < 256u; ++step){
            if(curT > bestT || curT > tExit){
                finishedTraversal = true;
                break;
            }
            if(
                (stepX > 0 && cx - haloX > maxCellX)
                || (stepX < 0 && cx + haloX < 0)
                || (stepZ > 0 && cz - haloZ > maxCellZ)
                || (stepZ < 0 && cz + haloZ < 0)
            ){
                finishedTraversal = true;
                break;
            }

            [loop]
            for(int dz = -haloZ; dz <= haloZ; ++dz){
                const int testZ = cz + dz;
                if(testZ < 0 || testZ > maxCellZ)
                    continue;
                [loop]
                for(int dx = -haloX; dx <= haloX; ++dx){
                    const int testX = cx + dx;
                    if(testX < 0 || testX > maxCellX)
                        continue;
                    nwbPhysicsQuerySphereCastHfCell(hullVertexSlot, hfBase, cols, rows, hfOrigin, sx, sz, sy, hfFlags, flipDiag, cullBackfaces, hfMinY, hfMaxY, testX, testZ, localOrig, localDir, localInvDir, totalR, anyHit, bestT, bestLocalN, bestRawDist, bestTriIdx, bestTv0, bestTv1, bestTv2);
                }
            }

            if(!nwbPhysicsHfStepDda(stepX, stepZ, tDeltaX, tDeltaZ, min(bestT, tExit), cx, cz, tMaxX, tMaxZ, curT)){
                finishedTraversal = true;
                break;
            }
        }
        if(!finishedTraversal && (!anyHit || bestT >= curT)){
            outNeedsFallback = true;
            return false;
        }
    }

    if(!anyHit)
        return false;
    outT = bestT;
    const float surfDist = max(bestRawDist - hfThickness, 0.0f);
    const float3 sampleHfLocal = localOrig + localDir * bestT;
    const float3 hitPtLocal = sampleHfLocal - bestLocalN * min(surfDist, castRadius);
    outPoint = shapeOrig + nwbPhysicsQuatRotate(hitPtLocal, shapeRot);
    outNormal = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(bestLocalN, shapeRot), float3(0.0f, 1.0f, 0.0f));
    outTriIdx = bestTriIdx;
    const float3 cpHf = nwbPhysicsClosestPointOnTriangle(sampleHfLocal, bestTv0, bestTv1, bestTv2);
    const float3 bwScHf = nwbPhysicsClosestPointBaryTriangle(cpHf, bestTv0, bestTv1, bestTv2);
    outBary = float3(bwScHf.y, bwScHf.z, bwScHf.x);
    return true;
}

float nwbPhysicsQueryPointHeightfieldDistSq(
    const uint hullVertexSlot,
    const NwbPhysicsGpuShapeProxy spProxy,
    const float4 shapeRot,
    const float3 localP,
    const float maxQueryDist,
    out float3 outNormal,
    out uint outTriIdx,
    out float3 outBary,
    out bool outNeedsFallback
){
    outNormal = float3(0.0f, 1.0f, 0.0f);
    outTriIdx = NWB_PHYSICS_INVALID_INDEX;
    outBary = float3(0.0f, 0.0f, 0.0f);
    outNeedsFallback = false;

    const uint hfBase = spProxy.filterData.w;
    NwbPhysicsHfHeader hf;
    if(hullVertexSlot == NWB_PHYSICS_INVALID_INDEX || hfBase == NWB_PHYSICS_INVALID_INDEX || !nwbPhysicsHfLoadHeader(hullVertexSlot, hfBase, hf)){
        outNeedsFallback = true;
        return 1.0e30f;
    }
    const float3 hfOrigin = hf.origin;
    const float hfMaxY = hf.maxY;
    const float sx = hf.sx;
    const float sy = hf.sy;
    const float sz = hf.sz;
    const float hfMinY = hf.minY;
    const uint cols = hf.cols;
    const uint rows = hf.rows;
    const uint hfFlags = hf.flags;
    const float hfThickness = hf.thickness;

    if(maxQueryDist >= 1.0e10f){
        outNeedsFallback = true;
        return 1.0e30f;
    }

    const float pad = max(maxQueryDist, 1.0e-3f) + hfThickness;
    if(localP.y + pad < hfMinY || localP.y - pad > hfMaxY)
        return 1.0e30f;

    const int maxCellX = int(cols - 2u);
    const int maxCellZ = int(rows - 2u);
    const int rawCx0 = int(floor(((localP.x - pad) - hfOrigin.x) / sx));
    const int rawCx1 = int(floor(((localP.x + pad) - hfOrigin.x) / sx));
    const int rawCz0 = int(floor(((localP.z - pad) - hfOrigin.z) / sz));
    const int rawCz1 = int(floor(((localP.z + pad) - hfOrigin.z) / sz));
    if(rawCx1 < 0 || rawCx0 > maxCellX || rawCz1 < 0 || rawCz0 > maxCellZ)
        return 1.0e30f;
    const uint cx0 = uint(max(rawCx0, 0));
    const uint cx1 = uint(min(rawCx1, maxCellX));
    const uint cz0 = uint(max(rawCz0, 0));
    const uint cz1 = uint(min(rawCz1, maxCellZ));
    if(cx0 > cx1 || cz0 > cz1)
        return 1.0e30f;
    if((cx1 - cx0 + 1u) * (cz1 - cz0 + 1u) > 256u){
        outNeedsFallback = true;
        return 1.0e30f;
    }

    const bool flipDiag = (hfFlags & NWB_PHYSICS_HF_FLAG_FLIP_DIAGONAL) != 0u;
    const float3 zeroVec = float3(0.0f, 0.0f, 0.0f);
    bool found = false;
    float bestDistSq = 1.0e30f;
    float3 bestLocalN = float3(0.0f, 1.0f, 0.0f);
    uint bestTriIdx = NWB_PHYSICS_INVALID_INDEX;
    float3 bestClosePt = float3(0.0f, 0.0f, 0.0f);
    float3 bestTv0 = float3(0.0f, 0.0f, 0.0f);
    float3 bestTv1 = float3(0.0f, 0.0f, 0.0f);
    float3 bestTv2 = float3(0.0f, 0.0f, 0.0f);

    [loop]
    for(uint cz = cz0; cz <= cz1; ++cz){
        [loop]
        for(uint cx = cx0; cx <= cx1; ++cx){
            float3 v00 = zeroVec;
            float3 v10 = zeroVec;
            float3 v01 = zeroVec;
            float3 v11 = zeroVec;
            if(!nwbPhysicsHfLoadCellVertices(hullVertexSlot, hfBase, cols, rows, hfOrigin, sx, sz, sy, hfFlags, cx, cz, v00, v10, v01, v11))
                continue;
            const float cellMinY = min(min(v00.y, v10.y), min(v01.y, v11.y));
            const float cellMaxY = max(max(v00.y, v10.y), max(v01.y, v11.y));
            if(localP.y + pad < cellMinY || localP.y - pad > cellMaxY)
                continue;

            [loop]
            for(uint ti = 0u; ti < 2u; ++ti){
                float3 tv0 = zeroVec;
                float3 tv1 = zeroVec;
                float3 tv2 = zeroVec;
                nwbPhysicsHfExtractTriangle(flipDiag, ti, v00, v10, v01, v11, tv0, tv1, tv2);
                const float3 qPt = nwbPhysicsClosestPointOnTriangle(localP, tv0, tv1, tv2);
                const float3 d = localP - qPt;
                const float dSq = dot(d, d);
                if(!found || dSq < bestDistSq){
                    found = true;
                    bestDistSq = dSq;
                    const float3 triN = nwbPhysicsHfTriNormal(tv0, tv1, tv2);
                    bestLocalN = nwbPhysicsSafeNormal(d, triN);
                    bestTriIdx = (cz * (cols - 1u) + cx) * 2u + ti;
                    bestClosePt = qPt;
                    bestTv0 = tv0;
                    bestTv1 = tv1;
                    bestTv2 = tv2;
                }
            }
        }
    }

    if(!found)
        return 1.0e30f;

    outNormal = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(bestLocalN, shapeRot), float3(0.0f, 1.0f, 0.0f));
    const float rawDist = sqrt(max(bestDistSq, 0.0f));
    if(rawDist <= hfThickness)
        return 0.0f;
    const float effDist = rawDist - hfThickness;
    return effDist * effDist;
}

float nwbPhysicsPrimitivePointDistSq(
    const float3 p,
    const NwbPhysicsGpuBodyPose pose,
    const NwbPhysicsGpuShapeProxy sp,
    const uint hullVertexSlot,
    const float maxQueryDist,
    out float3 outNormal,
    inout bool outNeedsFallback
){
    const uint shapeType = sp.bodyIndexTypeShape.y;
    const uint shapeFlags = sp.bodyIndexTypeShape.w;
    float3 origin = pose.originPad.xyz;
    float4 q = pose.rotation;
    nwbPhysicsResolveShapeWorldPose(sp, pose.originPad.xyz, pose.rotation, origin, q);

    if(shapeType == NWB_PHYSICS_SHAPE_TYPE_SPHERE){
        const float3 c = origin + nwbPhysicsQuatRotate(sp.localCenterRadius.xyz, q);
        const float r = sp.localCenterRadius.w;
        const float3 d = p - c;
        const float lenSq = dot(d, d);
        if(lenSq <= r * r){
            outNormal = nwbPhysicsSafeNormal(d, float3(0.0f, 1.0f, 0.0f));
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
            outNormal = nwbPhysicsSafeNormal(d, float3(0.0f, 1.0f, 0.0f));
            return 0.0f;
        }
        const float dist = sqrt(lenSq) - r;
        outNormal = d * rsqrt(max(lenSq, 1.0e-12f));
        return dist * dist;
    }

    if(shapeType == NWB_PHYSICS_SHAPE_TYPE_TAPERED_CAPSULE){
        const float3 p0 = origin + nwbPhysicsQuatRotate(sp.localCenterRadius.xyz, q);
        const float3 p1 = origin + nwbPhysicsQuatRotate(sp.localExtentOrP1.xyz, q);
        const float r0 = max(sp.localCenterRadius.w + sp.localRotationOrMeta.x, 0.0f);
        const float r1 = max(sp.localCenterRadius.w + sp.localRotationOrMeta.y, 0.0f);
        const float3 seg = p1 - p0;
        const float segLenSq = dot(seg, seg);
        if(segLenSq <= 1.0e-12f || abs(r0 - r1) >= sqrt(segLenSq) - 1.0e-6f){
            const float3 domC = (r0 >= r1) ? p0 : p1;
            const float domR = max(r0, r1);
            const float3 dDom = p - domC;
            const float lenSqDom = dot(dDom, dDom);
            outNormal = nwbPhysicsSafeNormal(dDom, float3(0.0f, 1.0f, 0.0f));
            if(lenSqDom <= domR * domR)
                return 0.0f;
            const float distDom = sqrt(lenSqDom) - domR;
            return distDom * distDom;
        }
        const float L = sqrt(segLenSq);
        const float3 u = seg / L;
        const float sinA = (r0 - r1) / L;
        const float cosA = sqrt(max(1.0f - sinA * sinA, 0.0f));
        const float3 rel = p - p0;
        const float y = dot(rel, u);
        const float3 perp = rel - u * y;
        const float x = length(perp);
        const float s = -x * sinA + y * cosA;
        if(s <= 0.0f){
            const float3 d0 = p - p0;
            const float lenSq0 = dot(d0, d0);
            outNormal = nwbPhysicsSafeNormal(d0, -u);
            if(lenSq0 <= r0 * r0)
                return 0.0f;
            const float dist0 = sqrt(lenSq0) - r0;
            return dist0 * dist0;
        }
        if(s >= L * cosA){
            const float3 d1 = p - p1;
            const float lenSq1 = dot(d1, d1);
            outNormal = nwbPhysicsSafeNormal(d1, u);
            if(lenSq1 <= r1 * r1)
                return 0.0f;
            const float dist1 = sqrt(lenSq1) - r1;
            return dist1 * dist1;
        }
        float3 uFallback = cross(u, float3(0.0f, 1.0f, 0.0f));
        if(dot(uFallback, uFallback) < 1.0e-6f)
            uFallback = cross(u, float3(1.0f, 0.0f, 0.0f));
        const float3 rHat = nwbPhysicsSafeNormal(perp, nwbPhysicsSafeNormal(uFallback, float3(1.0f, 0.0f, 0.0f)));
        outNormal = nwbPhysicsSafeNormal(rHat * cosA + u * sinA, u);
        const float sd = x * cosA + y * sinA - r0;
        if(sd <= 0.0f)
            return 0.0f;
        return sd * sd;
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
            outNormal = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(nLocal, worldQ), float3(0.0f, 1.0f, 0.0f));
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
        outNormal = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(localN, worldQ), float3(0.0f, 1.0f, 0.0f));
        return 0.0f;
    }

    if(shapeType == NWB_PHYSICS_SHAPE_TYPE_CONE){
        const float4 worldQ = (sp.localRotationOrMeta.w < 0.999999f)
            ? nwbPhysicsQuatNormalize(nwbPhysicsQuatMul(q, sp.localRotationOrMeta))
            : q;
        const float3 c = origin + nwbPhysicsQuatRotate(sp.localCenterRadius.xyz, q);
        const float cr = max(sp.localCenterRadius.w, 0.0f);
        const float rCore = max(sp.localExtentOrP1.x, 0.0f);
        const float hhCore = max(sp.localExtentOrP1.y, 0.0f);
        const float3 localP = nwbPhysicsQuatInvRotate(p - c, worldQ);
        const float radial = sqrt(localP.x * localP.x + localP.z * localP.z);
        const float2 radDir = (radial > 1.0e-6f) ? float2(localP.x / radial, localP.z / radial) : float2(1.0f, 0.0f);
        const float2 pt2 = float2(radial, localP.y);

        const float2 apex2 = float2(0.0f, hhCore);
        const float2 corner2 = float2(rCore, -hhCore);
        const float2 edgeSlope = corner2 - apex2;
        const float edgeSlopeSq = dot(edgeSlope, edgeSlope);
        const float tSlope = (edgeSlopeSq > 1.0e-12f) ? clamp(dot(pt2 - apex2, edgeSlope) / edgeSlopeSq, 0.0f, 1.0f) : 0.0f;
        const float2 diffSlope = pt2 - (apex2 + edgeSlope * tSlope);
        const float dSlopeSq = dot(diffSlope, diffSlope);

        const float2 diffBase = pt2 - float2(min(radial, rCore), -hhCore);
        const float dBaseSq = dot(diffBase, diffBase);

        const float rAtY = (hhCore > 1.0e-6f) ? (rCore * (hhCore - localP.y) / (2.0f * hhCore)) : rCore;
        if(localP.y >= -hhCore && localP.y <= hhCore && radial <= rAtY){
            float3 insideN = float3(0.0f, -1.0f, 0.0f);
            if(dSlopeSq < dBaseSq)
                insideN = nwbPhysicsSafeNormal(float3(radDir.x * (2.0f * hhCore), rCore, radDir.y * (2.0f * hhCore)), float3(0.0f, 1.0f, 0.0f));
            outNormal = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(insideN, worldQ), float3(0.0f, 1.0f, 0.0f));
            return 0.0f;
        }

        const bool useSlope = dSlopeSq <= dBaseSq;
        const float rawDistSq = useSlope ? dSlopeSq : dBaseSq;
        const float2 diff2 = useSlope ? diffSlope : diffBase;
        const float3 diff3 = float3(radDir.x * diff2.x, diff2.y, radDir.y * diff2.x);
        const float3 fallbackConeN = useSlope
            ? nwbPhysicsSafeNormal(float3(radDir.x * (2.0f * hhCore), rCore, radDir.y * (2.0f * hhCore)), float3(0.0f, 1.0f, 0.0f))
            : float3(0.0f, -1.0f, 0.0f);
        outNormal = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(diff3, worldQ), nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(fallbackConeN, worldQ), float3(0.0f, 1.0f, 0.0f)));
        const float rawDist = sqrt(max(rawDistSq, 0.0f));
        if(rawDist <= cr)
            return 0.0f;
        const float effDist = rawDist - cr;
        return effDist * effDist;
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
        if(rawDistSq <= 1.0e-12f){
            const float3 faceDist = h - abs(localP);
            float3 localFaceN = float3(0.0f, 1.0f, 0.0f);
            if(faceDist.x <= faceDist.y && faceDist.x <= faceDist.z)
                localFaceN = float3(localP.x >= 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f);
            else if(faceDist.y <= faceDist.z)
                localFaceN = float3(0.0f, localP.y >= 0.0f ? 1.0f : -1.0f, 0.0f);
            else
                localFaceN = float3(0.0f, 0.0f, localP.z >= 0.0f ? 1.0f : -1.0f);
            outNormal = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(localFaceN, worldQ), float3(0.0f, 1.0f, 0.0f));
            return 0.0f;
        }
        outNormal = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(dLocal, worldQ), float3(0.0f, 1.0f, 0.0f));
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
        if(hullVertexSlot != NWB_PHYSICS_INVALID_INDEX && vOffset != NWB_PHYSICS_INVALID_INDEX && vCount > 0u && fCount > 0u){
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
                outNormal = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(bestFaceN, q), float3(0.0f, 1.0f, 0.0f));
                return 0.0f;
            }
            if(maxSd > skinR + maxQueryDist){
                outNormal = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(bestFaceN, q), float3(0.0f, 1.0f, 0.0f));
                return 1.0e30f;
            }
            float3 gjkDiff = float3(0.0f, 0.0f, 0.0f);
            bool gjkInside = false;
            nwbPhysicsQueryHullPointGjk(hullVertexSlot, vOffset, vCount, localP, gjkDiff, gjkInside);
            if(gjkInside){
                outNormal = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(bestFaceN, q), float3(0.0f, 1.0f, 0.0f));
                return 0.0f;
            }
            const float rawDist = length(gjkDiff);
            const float dist = max(rawDist - skinR, 0.0f);
            const float3 locN = (rawDist > 1.0e-5f) ? gjkDiff : bestFaceN;
            outNormal = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(locN, q), float3(0.0f, 1.0f, 0.0f));
            return dist * dist;
        }
    }

    outNeedsFallback = true;
    outNormal = float3(0.0f, 1.0f, 0.0f);
    return 1.0e30f;
}

bool nwbPhysicsQuerySphereMarchShape(
    const NwbPhysicsGpuShapeProxy spProxy,
    const NwbPhysicsGpuBodyPose pose,
    const uint hullVertexSlot,
    const float3 rayOrig,
    const float3 rayDir,
    const float3 rayInvDir,
    const float maxFrac,
    const float3 aMin,
    const float3 aMax,
    const float castRadius,
    out float outT,
    out float3 outPoint,
    out float3 outNormal,
    out bool outNeedsFallback
){
    outT = maxFrac;
    outPoint = float3(0.0f, 0.0f, 0.0f);
    outNormal = float3(0.0f, 1.0f, 0.0f);
    outNeedsFallback = false;

    const float3 padVec = float3(castRadius, castRadius, castRadius);
    const float3 t0 = ((aMin - padVec) - rayOrig) * rayInvDir;
    const float3 t1 = ((aMax + padVec) - rayOrig) * rayInvDir;
    const float3 tMin = min(t0, t1);
    const float3 tMax = max(t0, t1);
    const float tEnter = max(max(tMin.x, tMin.y), max(tMin.z, 0.0f));
    const float tExit = min(min(tMax.x, tMax.y), min(tMax.z, maxFrac));
    if(tExit < tEnter)
        return false;

    const float dirLen = length(rayDir);
    const bool zeroDir = dirLen <= 1.0e-12f;
    float t = zeroDir ? 0.0f : tEnter;
    const float stepExit = zeroDir ? 0.0f : tExit;

    [loop]
    for(uint step = 0u; step < 64u; ++step){
        if(t > stepExit)
            return false;
        const float3 sampleP = rayOrig + rayDir * t;
        const float remainDist = zeroDir ? (castRadius + 0.01f) : max((tExit - t) * dirLen + castRadius + 0.01f, 0.01f);
        float3 nOut = float3(0.0f, 1.0f, 0.0f);
        bool ptFallback = false;
        const float dSq = nwbPhysicsPrimitivePointDistSq(sampleP, pose, spProxy, hullVertexSlot, remainDist, nOut, ptFallback);
        if(ptFallback){
            outNeedsFallback = true;
            return false;
        }
        const float rawDist = sqrt(max(dSq, 0.0f));
        const float signedDist = rawDist - castRadius;
        if(signedDist <= 1.0e-4f){
            outT = t;
            outNormal = zeroDir ? nOut : ((t <= 1.0e-6f && rawDist <= 1.0e-6f) ? nwbPhysicsSafeNormal(-rayDir, nOut) : nOut);
            outPoint = sampleP - outNormal * min(rawDist, castRadius);
            return true;
        }
        if(zeroDir)
            return false;
        t += signedDist / dirLen;
    }

    if(!zeroDir && t <= tExit)
        outNeedsFallback = true;
    return false;
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if NWB_PHYSICS_QUERY_IS_SHAPE_OP
float nwbPhysicsProxySkinRadius(const NwbPhysicsGpuShapeProxy sp){
    if(sp.bodyIndexTypeShape.y == NWB_PHYSICS_SHAPE_TYPE_PLANE)
        return 0.0f;
    return max(sp.localCenterRadius.w, 0.0f);
}

void nwbPhysicsComputeSupportWorldRelAabb(
    const NwbPhysicsGpuSupportCache cache,
    const uint hullVertexSlot,
    const float skinR,
    const float3 refOrigin,
    out float3 outRelMin,
    out float3 outRelMax
){
    float minX = 0.0f;
    float maxX = 0.0f;
    float minY = 0.0f;
    float maxY = 0.0f;
    float minZ = 0.0f;
    float maxZ = 0.0f;
    nwbPhysicsSupportMinMaxCached(cache, hullVertexSlot, float3(1.0f, 0.0f, 0.0f), minX, maxX);
    nwbPhysicsSupportMinMaxCached(cache, hullVertexSlot, float3(0.0f, 1.0f, 0.0f), minY, maxY);
    nwbPhysicsSupportMinMaxCached(cache, hullVertexSlot, float3(0.0f, 0.0f, 1.0f), minZ, maxZ);
    const float3 rVec = float3(skinR, skinR, skinR);
    outRelMin = (float3(minX, minY, minZ) - refOrigin) - rVec;
    outRelMax = (float3(maxX, maxY, maxZ) - refOrigin) + rVec;
}

NwbPhysicsGpuSupportCache nwbPhysicsTranslateSupportCache(
    const NwbPhysicsGpuSupportCache cache,
    const float3 delta
){
    NwbPhysicsGpuSupportCache shifted = cache;
    shifted.p0OrCenter += delta;
    if(
        shifted.shapeType == NWB_PHYSICS_SHAPE_TYPE_CAPSULE
        || shifted.shapeType == NWB_PHYSICS_SHAPE_TYPE_SEGMENT
        || shifted.shapeType == NWB_PHYSICS_SHAPE_TYPE_TAPERED_CAPSULE
    )
        shifted.p1OrExtent += delta;
    return shifted;
}

[noinline]
bool nwbPhysicsQueryConvexOrTriShapeOp(
    const NwbPhysicsGpuSupportCache qCache0,
    const float3 qOrig0,
    const float qSkinR,
    const float3 qRelMin,
    const float3 qRelMax,
    const float3 rayDir,
    const float3 rayInvDir,
    const float maxFrac,
    const bool isCast,
    const NwbPhysicsGpuSupportCache targetCache,
    const bool triB,
    const float3 tv0,
    const float3 tv1,
    const float3 tv2,
    const float bSkinR,
    const float3 bMin,
    const float3 bMax,
    const float3 hintN,
    const uint hullVertexSlot,
    out float outT,
    out float3 outPoint,
    out float3 outNormal,
    out bool outNeedsFallback
){
    outT = isCast ? maxFrac : 0.0f;
    outPoint = float3(0.0f, 0.0f, 0.0f);
    outNormal = float3(0.0f, 1.0f, 0.0f);
    outNeedsFallback = false;

    const bool zeroDir = !isCast || (dot(rayDir, rayDir) <= 1.0e-12f);
    float tEnter = 0.0f;
    float tExit = 0.0f;
    if(isCast){
        if(zeroDir){
            if(!nwbPhysicsAabbOverlaps(qOrig0 + qRelMin, qOrig0 + qRelMax, bMin, bMax))
                return false;
        }
        else{
            const float3 eMin = bMin - qRelMax - float3(1.0e-4f, 1.0e-4f, 1.0e-4f);
            const float3 eMax = bMax - qRelMin + float3(1.0e-4f, 1.0e-4f, 1.0e-4f);
            const float3 t0 = (eMin - qOrig0) * rayInvDir;
            const float3 t1 = (eMax - qOrig0) * rayInvDir;
            const float3 tS = min(t0, t1);
            const float3 tL = max(t0, t1);
            tEnter = max(0.0f, max(tS.x, max(tS.y, tS.z)));
            tExit = min(maxFrac, min(tL.x, min(tL.y, tL.z)));
            if(tExit < tEnter)
                return false;
        }
    }

    const float totalR = qSkinR + bSkinR;
    const float3 zeroVec = float3(0.0f, 0.0f, 0.0f);
    float t = tEnter;
    float lastSep = 1.0e30f;

    [loop]
    for(uint iter = 0u; iter < 48u; ++iter){
        const NwbPhysicsGpuSupportCache qCacheT = isCast ? nwbPhysicsTranslateSupportCache(qCache0, rayDir * t) : qCache0;
        float3 pA = zeroVec;
        float3 pB = zeroVec;
        float gjkDist = 0.0f;
        bool hitDeep = false;
        nwbPhysicsRunGjkDistanceCached(
            qCacheT, false, zeroVec, zeroVec, zeroVec,
            targetCache, triB, tv0, tv1, tv2,
            hullVertexSlot, pA, pB, gjkDist, hitDeep
        );

        if(!hitDeep && gjkDist > 1.0e-5f){
            const float sep = gjkDist - totalR;
            const float3 nBtoA = (pA - pB) / gjkDist;
            if(sep <= (isCast ? 1.0e-4f : 0.0f)){
                outT = t;
                outPoint = pB + nBtoA * bSkinR;
                outNormal = nBtoA;
                return true;
            }
            if(zeroDir)
                return false;
            const float closing = -dot(rayDir, nBtoA);
            if(closing <= 1.0e-7f)
                return false;
            lastSep = sep;
            t += sep / closing;
            if(t > tExit)
                return false;
        }
        else{
            float3 epaN = float3(0.0f, 1.0f, 0.0f);
            float3 epaPtA = zeroVec;
            float3 epaPtB = zeroVec;
            float epaDepth = 0.0f;
            const float3 safeHintAtoB = nwbPhysicsSafeNormal(zeroDir ? hintN : rayDir, float3(0.0f, -1.0f, 0.0f));
            if(!nwbPhysicsRunEpaCached(
                qCacheT, false, zeroVec, zeroVec, zeroVec,
                targetCache, triB, tv0, tv1, tv2,
                hullVertexSlot, safeHintAtoB, epaN, epaPtA, epaPtB, epaDepth
            )){
                outNeedsFallback = true;
                return false;
            }
            outT = t;
            outNormal = nwbPhysicsSafeNormal(-epaN, -safeHintAtoB);
            outPoint = epaPtB + outNormal * bSkinR;
            return true;
        }
    }

    if(!zeroDir && t <= tExit && lastSep <= 1.0e-2f)
        outNeedsFallback = true;
    return false;
}

bool nwbPhysicsQueryPlaneVsQueryShape(
    const NwbPhysicsGpuShapeProxy spPlane,
    const NwbPhysicsGpuBodyPose posePlane,
    const NwbPhysicsGpuSupportCache qCache0,
    const float qSkinR,
    const float3 rayDir,
    const float maxFrac,
    const bool isCast,
    const uint hullVertexSlot,
    out float outT,
    out float3 outPoint,
    out float3 outNormal
){
    outT = maxFrac;
    outPoint = float3(0.0f, 0.0f, 0.0f);
    outNormal = float3(0.0f, 1.0f, 0.0f);

    float3 plOrig = posePlane.originPad.xyz;
    float4 plRot = posePlane.rotation;
    nwbPhysicsResolveShapeWorldPose(spPlane, posePlane.originPad.xyz, posePlane.rotation, plOrig, plRot);
    const float4 worldQ = nwbPhysicsQuatNormalize(nwbPhysicsQuatMul(plRot, spPlane.localRotationOrMeta));
    const float3 pPlane = plOrig + nwbPhysicsQuatRotate(spPlane.localCenterRadius.xyz, plRot);
    float3 t1W;
    float3 t2W;
    float3 nW;
    nwbPhysicsQuatToAxes(worldQ, t1W, t2W, nW);

    uint supIdx = 0u;
    const float3 qSup = nwbPhysicsSupportCached(qCache0, hullVertexSlot, -nW, supIdx) - nW * qSkinR;
    const float sd0 = dot(qSup - pPlane, nW);
    const bool isFinite = (spPlane.localExtentOrP1.x > 0.0f) && (spPlane.localExtentOrP1.z > 0.0f);

    float hitT = 0.0f;
    float3 hitSup = qSup;
    if(sd0 <= 0.0f){
        hitT = 0.0f;
        hitSup = qSup;
    }
    else if(isCast){
        const float vn = dot(rayDir, nW);
        if(vn >= -1.0e-7f)
            return false;
        hitT = sd0 / (-vn);
        if(hitT > maxFrac)
            return false;
        hitSup = qSup + rayDir * hitT;
    }
    else{
        return false;
    }

    const float3 rel = hitSup - pPlane;
    if(isFinite){
        const float u1 = dot(rel, t1W);
        const float u2 = dot(rel, t2W);
        if(abs(u1) > spPlane.localExtentOrP1.x || abs(u2) > spPlane.localExtentOrP1.z)
            return false;
        outPoint = pPlane + t1W * u1 + t2W * u2;
    }
    else{
        outPoint = hitSup - nW * dot(rel, nW);
    }
    outT = hitT;
    outNormal = nW;
    return true;
}

#if NWB_PHYSICS_QUERY_HAS_MESH
[noinline]
bool nwbPhysicsQueryMeshVsQueryShape(
    const uint meshNodeSlot,
    const uint meshTriSlot,
    const uint hullVertexSlot,
    const NwbPhysicsGpuShapeProxy spProxy,
    const float3 shapeOrig,
    const float4 shapeRot,
    const NwbPhysicsGpuShapeProxy qProxy,
    const float3 qStartOrig,
    const float4 qRot,
    const float qSkinR,
    const float3 rayDir,
    const float maxFrac,
    const bool isCast,
    out float outT,
    out float3 outPoint,
    out float3 outNormal,
    out uint outTriIdx,
    out float3 outBary,
    out bool outNeedsFallback,
    inout uint stack[64],
    inout float stackMetric[64]
){
    outT = maxFrac;
    outPoint = float3(0.0f, 0.0f, 0.0f);
    outNormal = float3(0.0f, 1.0f, 0.0f);
    outTriIdx = NWB_PHYSICS_INVALID_INDEX;
    outBary = float3(0.0f, 0.0f, 0.0f);
    outNeedsFallback = false;

    const uint nodeBase = spProxy.filterData.w;
    const uint triBase = asuint(spProxy.localRotationOrMeta.x);
    if(meshNodeSlot == NWB_PHYSICS_INVALID_INDEX || meshTriSlot == NWB_PHYSICS_INVALID_INDEX || nodeBase == NWB_PHYSICS_INVALID_INDEX || triBase == NWB_PHYSICS_INVALID_INDEX){
        outNeedsFallback = true;
        return false;
    }

    const float4 shapeRotInv = float4(-shapeRot.xyz, shapeRot.w);
    const float3 localQOrig = nwbPhysicsQuatInvRotate(qStartOrig - shapeOrig, shapeRot);
    const float4 localQRot = nwbPhysicsQuatNormalize(nwbPhysicsQuatMul(shapeRotInv, qRot));
    const float3 localDir = nwbPhysicsQuatInvRotate(rayDir, shapeRot);
    const float3 localInvDir = nwbPhysicsQuerySafeInvDir(localDir);
    const NwbPhysicsGpuSupportCache localQCache0 = nwbPhysicsBuildSupportCache(qProxy, localQOrig, localQRot);

    float3 localQRelMin;
    float3 localQRelMax;
    nwbPhysicsComputeSupportWorldRelAabb(localQCache0, hullVertexSlot, qSkinR, localQOrig, localQRelMin, localQRelMax);

    const float meshSkinR = max(spProxy.localCenterRadius.w, 0.0f);
    const float3 meshSkinVec = float3(meshSkinR, meshSkinR, meshSkinR);
    const float3 qBoxMin = localQOrig + localQRelMin;
    const float3 qBoxMax = localQOrig + localQRelMax;

    const float4 rootN0 = g_NwbPhysicsFloat4Buffers[meshNodeSlot][nodeBase * 2u + 0u];
    const float4 rootN1 = g_NwbPhysicsFloat4Buffers[meshNodeSlot][nodeBase * 2u + 1u];
    uint sp = 0u;
    float rootT = 0.0f;
    bool rootHit = false;
    if(isCast)
        rootHit = nwbPhysicsQueryRayAabbFast(localQOrig, localInvDir, rootN0.xyz - meshSkinVec - localQRelMax, rootN1.xyz + meshSkinVec - localQRelMin, maxFrac, rootT);
    else
        rootHit = nwbPhysicsAabbOverlaps(qBoxMin, qBoxMax, rootN0.xyz - meshSkinVec, rootN1.xyz + meshSkinVec);
    if(rootHit){
        stack[0] = 0u;
        stackMetric[0] = rootT;
        sp = 1u;
    }

    bool anyHit = false;
    float bestMetric = isCast ? maxFrac : 1.0e30f;
    float3 bestPtLocal = float3(0.0f, 0.0f, 0.0f);
    float3 bestNLocal = float3(0.0f, 1.0f, 0.0f);
    uint bestTriIdx = NWB_PHYSICS_INVALID_INDEX;
    float3 bestTv0 = float3(0.0f, 0.0f, 0.0f);
    float3 bestTv1 = float3(0.0f, 0.0f, 0.0f);
    float3 bestTv2 = float3(0.0f, 0.0f, 0.0f);
    NwbPhysicsGpuSupportCache dummyTarget;
    dummyTarget.shapeType = 0u;
    dummyTarget.vCount = 0u;
    dummyTarget.vOffset = 0u;
    dummyTarget.isBox = false;
    dummyTarget.p0OrCenter = float3(0.0f, 0.0f, 0.0f);
    dummyTarget.p1OrExtent = float3(0.0f, 0.0f, 0.0f);
    dummyTarget.axX = float3(1.0f, 0.0f, 0.0f);
    dummyTarget.axY = float3(0.0f, 1.0f, 0.0f);
    dummyTarget.axZ = float3(0.0f, 0.0f, 1.0f);
    dummyTarget.extraParams = float2(0.0f, 0.0f);

    while(sp > 0u){
        --sp;
        if(isCast && stackMetric[sp] > bestMetric)
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
                const float3 triMin = min(tv0, min(tv1, tv2)) - meshSkinVec;
                const float3 triMax = max(tv0, max(tv1, tv2)) + meshSkinVec;
                const float3 triN = nwbPhysicsSafeNormal(cross(tv1 - tv0, tv2 - tv0), float3(0.0f, 1.0f, 0.0f));
                float candT = bestMetric;
                float3 candPtL = float3(0.0f, 0.0f, 0.0f);
                float3 candNL = triN;
                bool triFallback = false;
                bool triHit = false;
                if(isCast || nwbPhysicsAabbOverlaps(qBoxMin, qBoxMax, triMin, triMax)){
                    triHit = nwbPhysicsQueryConvexOrTriShapeOp(
                        localQCache0, localQOrig, qSkinR, localQRelMin, localQRelMax,
                        localDir, localInvDir, bestMetric, isCast,
                        dummyTarget, true, tv0, tv1, tv2, meshSkinR, triMin, triMax, -triN,
                        hullVertexSlot, candT, candPtL, candNL, triFallback
                    );
                    if(triHit && !isCast){
                        const float3 diffQ = candPtL - localQOrig;
                        candT = dot(diffQ, diffQ);
                    }
                }
                if(triFallback){
                    outNeedsFallback = true;
                    return false;
                }
                if(triHit && (!anyHit || candT < bestMetric)){
                    anyHit = true;
                    bestMetric = candT;
                    bestPtLocal = candPtL;
                    bestNLocal = candNL;
                    bestTriIdx = firstTri + ti;
                    bestTv0 = tv0;
                    bestTv1 = tv1;
                    bestTv2 = tv2;
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
            bool hitL = false;
            bool hitR = false;
            if(isCast){
                hitL = nwbPhysicsQueryRayAabbFast(localQOrig, localInvDir, ln0.xyz - meshSkinVec - localQRelMax, ln1.xyz + meshSkinVec - localQRelMin, bestMetric, tL);
                hitR = nwbPhysicsQueryRayAabbFast(localQOrig, localInvDir, rn0.xyz - meshSkinVec - localQRelMax, rn1.xyz + meshSkinVec - localQRelMin, bestMetric, tR);
            }
            else{
                hitL = nwbPhysicsAabbOverlaps(qBoxMin, qBoxMax, ln0.xyz - meshSkinVec, ln1.xyz + meshSkinVec);
                hitR = nwbPhysicsAabbOverlaps(qBoxMin, qBoxMax, rn0.xyz - meshSkinVec, rn1.xyz + meshSkinVec);
            }
            if((hitL ? 1u : 0u) + (hitR ? 1u : 0u) + sp > 64u){
                outNeedsFallback = true;
                return false;
            }
            if(hitL && hitR){
                if(isCast && tR < tL){
                    stack[sp] = lIdx; stackMetric[sp] = tL; ++sp;
                    stack[sp] = rIdx; stackMetric[sp] = tR; ++sp;
                }
                else{
                    stack[sp] = rIdx; stackMetric[sp] = tR; ++sp;
                    stack[sp] = lIdx; stackMetric[sp] = tL; ++sp;
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
    outT = bestMetric;
    outPoint = shapeOrig + nwbPhysicsQuatRotate(bestPtLocal, shapeRot);
    outNormal = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(bestNLocal, shapeRot), float3(0.0f, 1.0f, 0.0f));
    outTriIdx = bestTriIdx;
    const float3 bwMeshShape = nwbPhysicsClosestPointBaryTriangle(bestPtLocal, bestTv0, bestTv1, bestTv2);
    outBary = float3(bwMeshShape.y, bwMeshShape.z, bwMeshShape.x);
    return true;
}
#endif

[noinline]
bool nwbPhysicsQueryHeightfieldVsQueryShape(
    const uint hullVertexSlot,
    const NwbPhysicsGpuShapeProxy spProxy,
    const float3 shapeOrig,
    const float4 shapeRot,
    const NwbPhysicsGpuShapeProxy qProxy,
    const float3 qStartOrig,
    const float4 qRot,
    const float qSkinR,
    const float3 rayDir,
    const float maxFrac,
    const bool isCast,
    out float outT,
    out float3 outPoint,
    out float3 outNormal,
    out uint outTriIdx,
    out float3 outBary,
    out bool outNeedsFallback
){
    outT = maxFrac;
    outPoint = float3(0.0f, 0.0f, 0.0f);
    outNormal = float3(0.0f, 1.0f, 0.0f);
    outTriIdx = NWB_PHYSICS_INVALID_INDEX;
    outBary = float3(0.0f, 0.0f, 0.0f);
    outNeedsFallback = false;

    const uint hfBase = spProxy.filterData.w;
    NwbPhysicsHfHeader hf;
    if(hullVertexSlot == NWB_PHYSICS_INVALID_INDEX || hfBase == NWB_PHYSICS_INVALID_INDEX || !nwbPhysicsHfLoadHeader(hullVertexSlot, hfBase, hf)){
        outNeedsFallback = true;
        return false;
    }

    const float4 shapeRotInv = float4(-shapeRot.xyz, shapeRot.w);
    const float3 localQOrig = nwbPhysicsQuatInvRotate(qStartOrig - shapeOrig, shapeRot);
    const float4 localQRot = nwbPhysicsQuatNormalize(nwbPhysicsQuatMul(shapeRotInv, qRot));
    const float3 localDir = nwbPhysicsQuatInvRotate(rayDir, shapeRot);
    const float3 localInvDir = nwbPhysicsQuerySafeInvDir(localDir);
    const NwbPhysicsGpuSupportCache localQCache0 = nwbPhysicsBuildSupportCache(qProxy, localQOrig, localQRot);

    float3 localQRelMin;
    float3 localQRelMax;
    nwbPhysicsComputeSupportWorldRelAabb(localQCache0, hullVertexSlot, qSkinR, localQOrig, localQRelMin, localQRelMax);

    const float hfThick = hf.thickness;
    const float3 hfThickVec = float3(hfThick, hfThick, hfThick);
    float3 sweepMin = localQOrig + localQRelMin;
    float3 sweepMax = localQOrig + localQRelMax;
    if(isCast){
        const float3 qEnd = localQOrig + localDir * maxFrac;
        sweepMin = min(sweepMin, qEnd + localQRelMin);
        sweepMax = max(sweepMax, qEnd + localQRelMax);
    }
    sweepMin -= hfThickVec;
    sweepMax += hfThickVec;

    const int maxCellX = int(hf.cols - 2u);
    const int maxCellZ = int(hf.rows - 2u);
    if(sweepMax.y < hf.minY || sweepMin.y > hf.maxY)
        return false;
    const int rawCx0 = int(floor((sweepMin.x - hf.origin.x) / hf.sx));
    const int rawCx1 = int(floor((sweepMax.x - hf.origin.x) / hf.sx));
    const int rawCz0 = int(floor((sweepMin.z - hf.origin.z) / hf.sz));
    const int rawCz1 = int(floor((sweepMax.z - hf.origin.z) / hf.sz));
    if(rawCx1 < 0 || rawCx0 > maxCellX || rawCz1 < 0 || rawCz0 > maxCellZ)
        return false;

    const int cx0 = max(rawCx0, 0);
    const int cx1 = min(rawCx1, maxCellX);
    const int cz0 = max(rawCz0, 0);
    const int cz1 = min(rawCz1, maxCellZ);
    if(uint(cx1 - cx0 + 1) * uint(cz1 - cz0 + 1) > 256u){
        outNeedsFallback = true;
        return false;
    }

    const bool flipDiag = (hf.flags & NWB_PHYSICS_HF_FLAG_FLIP_DIAGONAL) != 0u;
    const float3 qBoxMin = localQOrig + localQRelMin;
    const float3 qBoxMax = localQOrig + localQRelMax;
    const float3 zeroVec = float3(0.0f, 0.0f, 0.0f);
    NwbPhysicsGpuSupportCache dummyTarget;
    dummyTarget.shapeType = 0u;
    dummyTarget.vCount = 0u;
    dummyTarget.vOffset = 0u;
    dummyTarget.isBox = false;
    dummyTarget.p0OrCenter = zeroVec;
    dummyTarget.p1OrExtent = zeroVec;
    dummyTarget.axX = float3(1.0f, 0.0f, 0.0f);
    dummyTarget.axY = float3(0.0f, 1.0f, 0.0f);
    dummyTarget.axZ = float3(0.0f, 0.0f, 1.0f);
    dummyTarget.extraParams = float2(0.0f, 0.0f);

    bool anyHit = false;
    float bestMetric = isCast ? maxFrac : 1.0e30f;
    float3 bestPtLocal = zeroVec;
    float3 bestNLocal = float3(0.0f, 1.0f, 0.0f);
    uint bestTriIdx = NWB_PHYSICS_INVALID_INDEX;
    float3 bestTv0 = zeroVec;
    float3 bestTv1 = zeroVec;
    float3 bestTv2 = zeroVec;

    [loop]
    for(int cz = cz0; cz <= cz1; ++cz){
        [loop]
        for(int cx = cx0; cx <= cx1; ++cx){
            float3 v00 = zeroVec;
            float3 v10 = zeroVec;
            float3 v01 = zeroVec;
            float3 v11 = zeroVec;
            if(!nwbPhysicsHfLoadCellVertices(hullVertexSlot, hfBase, hf.cols, hf.rows, hf.origin, hf.sx, hf.sz, hf.sy, hf.flags, uint(cx), uint(cz), v00, v10, v01, v11))
                continue;
            const float cellMinY = min(min(v00.y, v10.y), min(v01.y, v11.y));
            const float cellMaxY = max(max(v00.y, v10.y), max(v01.y, v11.y));
            if(sweepMax.y < cellMinY || sweepMin.y > cellMaxY)
                continue;

            [loop]
            for(uint ti = 0u; ti < 2u; ++ti){
                float3 tv0 = zeroVec;
                float3 tv1 = zeroVec;
                float3 tv2 = zeroVec;
                nwbPhysicsHfExtractTriangle(flipDiag, ti, v00, v10, v01, v11, tv0, tv1, tv2);
                const float3 triMin = min(tv0, min(tv1, tv2)) - hfThickVec;
                const float3 triMax = max(tv0, max(tv1, tv2)) + hfThickVec;
                const float3 triN = nwbPhysicsHfTriNormal(tv0, tv1, tv2);
                float candT = bestMetric;
                float3 candPtL = zeroVec;
                float3 candNL = triN;
                bool triFallback = false;
                bool triHit = false;
                if(isCast || nwbPhysicsAabbOverlaps(qBoxMin, qBoxMax, triMin, triMax)){
                    triHit = nwbPhysicsQueryConvexOrTriShapeOp(
                        localQCache0, localQOrig, qSkinR, localQRelMin, localQRelMax,
                        localDir, localInvDir, bestMetric, isCast,
                        dummyTarget, true, tv0, tv1, tv2, hfThick, triMin, triMax, -triN,
                        hullVertexSlot, candT, candPtL, candNL, triFallback
                    );
                    if(triHit && !isCast){
                        const float3 diffQ = candPtL - localQOrig;
                        candT = dot(diffQ, diffQ);
                    }
                }
                if(triFallback){
                    outNeedsFallback = true;
                    return false;
                }
                if(triHit && (!anyHit || candT < bestMetric)){
                    anyHit = true;
                    bestMetric = candT;
                    bestPtLocal = candPtL;
                    bestNLocal = candNL;
                    bestTriIdx = (uint(cz) * (hf.cols - 1u) + uint(cx)) * 2u + ti;
                    bestTv0 = tv0;
                    bestTv1 = tv1;
                    bestTv2 = tv2;
                }
            }
        }
    }

    if(!anyHit)
        return false;
    outT = bestMetric;
    outPoint = shapeOrig + nwbPhysicsQuatRotate(bestPtLocal, shapeRot);
    outNormal = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(bestNLocal, shapeRot), float3(0.0f, 1.0f, 0.0f));
    outTriIdx = bestTriIdx;
    const float3 bwHfShape = nwbPhysicsClosestPointBaryTriangle(bestPtLocal, bestTv0, bestTv1, bestTv2);
    outBary = float3(bwHfShape.y, bwHfShape.z, bwHfShape.x);
    return true;
}
#endif

void nwbPhysicsTestSpatialQueryShape(
    const uint queryIdx,
    const uint shapeSlot,
    const uint bodyPoseSlot,
    const uint hullVertexSlot,
#if NWB_PHYSICS_QUERY_HAS_MESH
    const uint meshNodeSlot,
    const uint meshTriSlot,
#endif
    const uint queryType,
    const uint qCat,
    const uint qMask,
    const uint qFlags,
    const uint ignoreBodyIdx,
    const uint ignoreShapeId,
    const int auxGroupIndex,
    const uint multiHitSlot,
    const uint maxHitsPerQuery,
    const float3 rayOrig,
    const float3 rayDir,
    const float3 rayInvDir,
    const float maxQueryFrac,
#if !NWB_PHYSICS_QUERY_IS_SHAPE_OP
    const float3 sphereCenter,
    const float sphereRadius,
    const float castRadius,
#endif
    const float3 qCenter,
#if NWB_PHYSICS_QUERY_IS_SHAPE_OP
    const NwbPhysicsGpuShapeProxy qShapeProxy,
    const NwbPhysicsGpuSupportCache qCache0,
    const float3 qStartOrig,
    const float4 qStartRot,
    const float qSkinR,
    const float3 qRelMin,
    const float3 qRelMax,
#endif
    const uint shapeIdx,
    const float3 aMin,
    const float3 aMax,
    inout float bestMetric,
    inout float3 bestPoint,
    inout float3 bestNormal,
    inout uint bestTriIdx,
    inout float3 bestBary,
    inout uint bestShapeId,
    inout uint bestBodyIdx,
    inout uint hitCount,
    inout uint writtenHits,
    inout bool hasHit,
    inout bool needsCpuFallback
#if NWB_PHYSICS_QUERY_HAS_MESH
    ,
    inout uint meshStack[64],
    inout float meshStackMetric[64]
#endif
){
    const uint4 meta = g_NwbPhysicsShapeProxyBuffers[shapeSlot][shapeIdx].bodyIndexTypeShape;
    if((qFlags & NWB_PHYSICS_QUERY_FLAG_HAS_AUX) != 0u){
        if(ignoreBodyIdx != NWB_PHYSICS_INVALID_INDEX && meta.x == ignoreBodyIdx)
            return;
        if(ignoreShapeId != NWB_PHYSICS_INVALID_INDEX && meta.z == ignoreShapeId)
            return;
    }
    if((meta.w & NWB_PHYSICS_SHAPE_FLAG_SENSOR) != 0u && (qFlags & NWB_PHYSICS_QUERY_FLAG_INCLUDE_SENSORS) == 0u)
        return;
    const uint4 filter = g_NwbPhysicsShapeProxyBuffers[shapeSlot][shapeIdx].filterData;
    if(!nwbPhysicsQueryPassesFilter(qCat, qMask, qFlags, filter, auxGroupIndex))
        return;

    const bool isMultiHit = (qFlags & NWB_PHYSICS_QUERY_FLAG_MULTI_HIT) != 0u;
    const bool isSphereCast = (queryType == NWB_PHYSICS_QUERY_TYPE_SPHERE_CAST);
    const bool isShapeCast = (queryType == NWB_PHYSICS_QUERY_TYPE_SHAPE_CAST);
#if !NWB_PHYSICS_QUERY_IS_SHAPE_OP
    const bool isRay = (queryType == NWB_PHYSICS_QUERY_TYPE_RAYCAST_CLOSEST) || (queryType == NWB_PHYSICS_QUERY_TYPE_RAYCAST_ANY);
    const bool isRayLike = isRay || isSphereCast;
#else
    const bool isRayLike = isShapeCast;
#endif
    const uint shapeType = meta.y;

    bool candHit = false;
    float candMetric = 0.0f;
    float3 candPoint = float3(0.0f, 0.0f, 0.0f);
    float3 candNormal = float3(0.0f, 1.0f, 0.0f);
    uint candTriIdx = NWB_PHYSICS_INVALID_INDEX;
    float3 candBary = float3(0.0f, 0.0f, 0.0f);

#if !NWB_PHYSICS_QUERY_IS_SHAPE_OP
    if(queryType == NWB_PHYSICS_QUERY_TYPE_AABB_OVERLAP){
        if(
            (shapeType == NWB_PHYSICS_SHAPE_TYPE_HEIGHTFIELD && (hullVertexSlot == NWB_PHYSICS_INVALID_INDEX || filter.w == NWB_PHYSICS_INVALID_INDEX))
            || (shapeType == NWB_PHYSICS_SHAPE_TYPE_HULL && (meta.w & NWB_PHYSICS_SHAPE_FLAG_IS_BOX) == 0u && filter.w == NWB_PHYSICS_INVALID_INDEX)
            || (shapeType == NWB_PHYSICS_SHAPE_TYPE_MESH && (filter.w == NWB_PHYSICS_INVALID_INDEX || asuint(g_NwbPhysicsShapeProxyBuffers[shapeSlot][shapeIdx].localRotationOrMeta.x) == NWB_PHYSICS_INVALID_INDEX))
        ){
            needsCpuFallback = true;
            return;
        }
        candHit = true;
        candPoint = (aMin + aMax) * 0.5f;
        const float3 diff = candPoint - qCenter;
        candMetric = dot(diff, diff);
        candNormal = nwbPhysicsSafeNormal(diff, float3(0.0f, 1.0f, 0.0f));
    }
    else{
#endif
        if(shapeType > NWB_PHYSICS_SHAPE_TYPE_SEGMENT){
            needsCpuFallback = true;
            return;
        }
        if(shapeType == NWB_PHYSICS_SHAPE_TYPE_HULL && (meta.w & NWB_PHYSICS_SHAPE_FLAG_IS_BOX) == 0u){
            const uint metaPackPre = asuint(g_NwbPhysicsShapeProxyBuffers[shapeSlot][shapeIdx].localRotationOrMeta.x);
            if(hullVertexSlot == NWB_PHYSICS_INVALID_INDEX || filter.w == NWB_PHYSICS_INVALID_INDEX || (metaPackPre & 0xffu) == 0u || ((metaPackPre >> 8u) & 0xffu) == 0u){
                needsCpuFallback = true;
                return;
            }
        }
        else if(shapeType == NWB_PHYSICS_SHAPE_TYPE_MESH){
#if NWB_PHYSICS_QUERY_HAS_MESH
            if(
                meshNodeSlot == NWB_PHYSICS_INVALID_INDEX
                || meshTriSlot == NWB_PHYSICS_INVALID_INDEX
                || filter.w == NWB_PHYSICS_INVALID_INDEX
                || asuint(g_NwbPhysicsShapeProxyBuffers[shapeSlot][shapeIdx].localRotationOrMeta.x) == NWB_PHYSICS_INVALID_INDEX
            ){
                needsCpuFallback = true;
                return;
            }
#else
            needsCpuFallback = true;
            return;
#endif
        }
        else if(shapeType == NWB_PHYSICS_SHAPE_TYPE_HEIGHTFIELD){
            if(hullVertexSlot == NWB_PHYSICS_INVALID_INDEX || filter.w == NWB_PHYSICS_INVALID_INDEX){
                needsCpuFallback = true;
                return;
            }
        }

        const NwbPhysicsGpuShapeProxy spProxy = g_NwbPhysicsShapeProxyBuffers[shapeSlot][shapeIdx];
        const NwbPhysicsGpuBodyPose pose = g_NwbPhysicsBodyPoseBuffers[bodyPoseSlot][meta.x];
#if NWB_PHYSICS_QUERY_IS_SHAPE_OP
        {
            if(qShapeProxy.bodyIndexTypeShape.y == NWB_PHYSICS_INVALID_INDEX){
                needsCpuFallback = true;
                return;
            }
            const bool isCastOp = isShapeCast;
            const float evalMaxFrac = isMultiHit ? maxQueryFrac : bestMetric;
            bool shapeHit = false;
            float hitMetric = 0.0f;
            float3 hitPt = float3(0.0f, 0.0f, 0.0f);
            float3 hitN = float3(0.0f, 1.0f, 0.0f);
            bool opFallback = false;

            if(shapeType == NWB_PHYSICS_SHAPE_TYPE_PLANE){
                shapeHit = nwbPhysicsQueryPlaneVsQueryShape(spProxy, pose, qCache0, qSkinR, rayDir, evalMaxFrac, isCastOp, hullVertexSlot, hitMetric, hitPt, hitN);
            }
            else if(shapeType == NWB_PHYSICS_SHAPE_TYPE_HEIGHTFIELD){
                float3 shapeOrig = pose.originPad.xyz;
                float4 shapeRot = pose.rotation;
                nwbPhysicsResolveShapeWorldPose(spProxy, pose.originPad.xyz, pose.rotation, shapeOrig, shapeRot);
                shapeHit = nwbPhysicsQueryHeightfieldVsQueryShape(hullVertexSlot, spProxy, shapeOrig, shapeRot, qShapeProxy, qStartOrig, qStartRot, qSkinR, rayDir, evalMaxFrac, isCastOp, hitMetric, hitPt, hitN, candTriIdx, candBary, opFallback);
            }
            else if(shapeType == NWB_PHYSICS_SHAPE_TYPE_MESH){
#if NWB_PHYSICS_QUERY_HAS_MESH
                float3 shapeOrig = pose.originPad.xyz;
                float4 shapeRot = pose.rotation;
                nwbPhysicsResolveShapeWorldPose(spProxy, pose.originPad.xyz, pose.rotation, shapeOrig, shapeRot);
                shapeHit = nwbPhysicsQueryMeshVsQueryShape(meshNodeSlot, meshTriSlot, hullVertexSlot, spProxy, shapeOrig, shapeRot, qShapeProxy, qStartOrig, qStartRot, qSkinR, rayDir, evalMaxFrac, isCastOp, hitMetric, hitPt, hitN, candTriIdx, candBary, opFallback, meshStack, meshStackMetric);
#else
                opFallback = true;
#endif
            }
            else{
                float3 shapeOrig = pose.originPad.xyz;
                float4 shapeRot = pose.rotation;
                nwbPhysicsResolveShapeWorldPose(spProxy, pose.originPad.xyz, pose.rotation, shapeOrig, shapeRot);
                const NwbPhysicsGpuSupportCache targetCache = nwbPhysicsBuildSupportCache(spProxy, shapeOrig, shapeRot);
                const float bSkinR = nwbPhysicsProxySkinRadius(spProxy);
                const float3 hintAtoB = shapeOrig - qStartOrig;
                shapeHit = nwbPhysicsQueryConvexOrTriShapeOp(
                    qCache0, qStartOrig, qSkinR, qRelMin, qRelMax,
                    rayDir, rayInvDir, evalMaxFrac, isCastOp,
                    targetCache, false, float3(0.0f, 0.0f, 0.0f), float3(0.0f, 0.0f, 0.0f), float3(0.0f, 0.0f, 0.0f), bSkinR, aMin, aMax, hintAtoB,
                    hullVertexSlot, hitMetric, hitPt, hitN, opFallback
                );
            }

            if(opFallback){
                needsCpuFallback = true;
                return;
            }
            if(shapeHit){
                candHit = true;
                candPoint = hitPt;
                candNormal = hitN;
                if(isCastOp){
                    candMetric = hitMetric;
                }
                else{
                    const float3 diffQ = hitPt - qCenter;
                    candMetric = dot(diffQ, diffQ);
                }
            }
        }
#else
        if(isRayLike){
            const float evalMaxFrac = isMultiHit ? maxQueryFrac : bestMetric;
            float tHit = 0.0f;
            float3 nHit = float3(0.0f, 1.0f, 0.0f);
            float3 hitPtOverride = float3(0.0f, 0.0f, 0.0f);
            bool hasPtOverride = false;
            bool hitShape = false;
            bool useSphereMarch = isSphereCast;
            const float marchRadius = isSphereCast ? castRadius : 0.0f;
            const float skinR = max(spProxy.localCenterRadius.w, 0.0f);
            if(isRay){
                if(shapeType == NWB_PHYSICS_SHAPE_TYPE_SPHERE){
                    const float3 c = pose.originPad.xyz + nwbPhysicsQuatRotate(spProxy.localCenterRadius.xyz, pose.rotation);
                    hitShape = nwbPhysicsQueryRaySphere(rayOrig, rayDir, evalMaxFrac, c, spProxy.localCenterRadius.w, tHit, nHit);
                }
                else if(shapeType == NWB_PHYSICS_SHAPE_TYPE_CAPSULE){
                    const float3 p0 = pose.originPad.xyz + nwbPhysicsQuatRotate(spProxy.localCenterRadius.xyz, pose.rotation);
                    const float3 p1 = pose.originPad.xyz + nwbPhysicsQuatRotate(spProxy.localExtentOrP1.xyz, pose.rotation);
                    hitShape = nwbPhysicsQueryRayCapsule(rayOrig, rayDir, evalMaxFrac, p0, p1, spProxy.localCenterRadius.w, tHit, nHit);
                }
                else if(shapeType == NWB_PHYSICS_SHAPE_TYPE_TAPERED_CAPSULE){
                    const float3 p0 = pose.originPad.xyz + nwbPhysicsQuatRotate(spProxy.localCenterRadius.xyz, pose.rotation);
                    const float3 p1 = pose.originPad.xyz + nwbPhysicsQuatRotate(spProxy.localExtentOrP1.xyz, pose.rotation);
                    const float r0 = max(spProxy.localCenterRadius.w + spProxy.localRotationOrMeta.x, 0.0f);
                    const float r1 = max(spProxy.localCenterRadius.w + spProxy.localRotationOrMeta.y, 0.0f);
                    hitShape = nwbPhysicsQueryRayTaperedCapsule(rayOrig, rayDir, evalMaxFrac, p0, p1, r0, r1, tHit, nHit);
                }
                else if(shapeType == NWB_PHYSICS_SHAPE_TYPE_SEGMENT){
                    const float3 p0 = pose.originPad.xyz + nwbPhysicsQuatRotate(spProxy.localCenterRadius.xyz, pose.rotation);
                    const float3 p1 = pose.originPad.xyz + nwbPhysicsQuatRotate(spProxy.localExtentOrP1.xyz, pose.rotation);
                    if(skinR > 0.0f)
                        hitShape = nwbPhysicsQueryRayCapsule(rayOrig, rayDir, evalMaxFrac, p0, p1, skinR, tHit, nHit);
                    else
                        hitShape = nwbPhysicsQueryRaySegmentZero(rayOrig, rayDir, evalMaxFrac, p0, p1, tHit, nHit);
                }
                else if(shapeType == NWB_PHYSICS_SHAPE_TYPE_PLANE){
                    const float4 worldQ = nwbPhysicsQuatNormalize(nwbPhysicsQuatMul(pose.rotation, spProxy.localRotationOrMeta));
                    const float3 pPlane = pose.originPad.xyz + nwbPhysicsQuatRotate(spProxy.localCenterRadius.xyz, pose.rotation);
                    float3 t1W;
                    float3 t2W;
                    float3 nW;
                    nwbPhysicsQuatToAxes(worldQ, t1W, t2W, nW);
                    const bool isFin = (spProxy.localExtentOrP1.x > 0.0f) && (spProxy.localExtentOrP1.z > 0.0f);
                    hitShape = nwbPhysicsQueryRayPlane(rayOrig, rayDir, evalMaxFrac, pPlane, t1W, t2W, nW, isFin, spProxy.localExtentOrP1.x, spProxy.localExtentOrP1.z, tHit, nHit);
                }
                else if(shapeType == NWB_PHYSICS_SHAPE_TYPE_CYLINDER){
                    if(skinR > 1.0e-5f){
                        useSphereMarch = true;
                    }
                    else{
                        const float4 worldQ = (spProxy.localRotationOrMeta.w < 0.999999f)
                            ? nwbPhysicsQuatNormalize(nwbPhysicsQuatMul(pose.rotation, spProxy.localRotationOrMeta))
                            : pose.rotation;
                        const float3 c = pose.originPad.xyz + nwbPhysicsQuatRotate(spProxy.localCenterRadius.xyz, pose.rotation);
                        hitShape = nwbPhysicsQueryRayCylinderSharp(rayOrig, rayDir, evalMaxFrac, c, worldQ, spProxy.localExtentOrP1.x, spProxy.localExtentOrP1.y, tHit, nHit);
                    }
                }
                else if(shapeType == NWB_PHYSICS_SHAPE_TYPE_CONE){
                    if(skinR > 1.0e-5f){
                        useSphereMarch = true;
                    }
                    else{
                        const float4 worldQ = (spProxy.localRotationOrMeta.w < 0.999999f)
                            ? nwbPhysicsQuatNormalize(nwbPhysicsQuatMul(pose.rotation, spProxy.localRotationOrMeta))
                            : pose.rotation;
                        const float3 c = pose.originPad.xyz + nwbPhysicsQuatRotate(spProxy.localCenterRadius.xyz, pose.rotation);
                        hitShape = nwbPhysicsQueryRayConeSharp(rayOrig, rayDir, evalMaxFrac, c, worldQ, spProxy.localExtentOrP1.x, spProxy.localExtentOrP1.y, tHit, nHit);
                    }
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
                    const float3 h = spProxy.localExtentOrP1.xyz + float3(skinR, skinR, skinR);
                    float3 localN = float3(0.0f, 1.0f, 0.0f);
                    hitShape = nwbPhysicsQueryRayAabb(localOrig, localInvDir, -h, h, evalMaxFrac, tHit, localN);
                    if(hitShape){
                        if(skinR > 1.0e-5f){
                            useSphereMarch = true;
                            hitShape = false;
                        }
                        else{
                            nHit = nwbPhysicsSafeNormal(nwbPhysicsQuatRotate(localN, worldQ), float3(0.0f, 1.0f, 0.0f));
                        }
                    }
                }
                else if(shapeType == NWB_PHYSICS_SHAPE_TYPE_HULL){
                    if(skinR > 1.0e-5f){
                        useSphereMarch = true;
                    }
                    else{
                        float3 shapeOrig = pose.originPad.xyz;
                        float4 shapeRot = pose.rotation;
                        nwbPhysicsResolveShapeWorldPose(spProxy, pose.originPad.xyz, pose.rotation, shapeOrig, shapeRot);
                        const float3 localOrig = nwbPhysicsQuatInvRotate(rayOrig - shapeOrig, shapeRot);
                        const float3 localDir = nwbPhysicsQuatInvRotate(rayDir, shapeRot);
                        bool hullFallback = false;
                        hitShape = nwbPhysicsQueryRayHull(hullVertexSlot, spProxy, shapeRot, localOrig, localDir, evalMaxFrac, tHit, nHit, hullFallback);
                        if(hullFallback)
                            needsCpuFallback = true;
                    }
                }
                else if(shapeType == NWB_PHYSICS_SHAPE_TYPE_HEIGHTFIELD){
                    const uint hfBase = spProxy.filterData.w;
                    const float hfThickness = g_NwbPhysicsFloat4Buffers[hullVertexSlot][hfBase + 2u].w;
                    float3 shapeOrig = pose.originPad.xyz;
                    float4 shapeRot = pose.rotation;
                    nwbPhysicsResolveShapeWorldPose(spProxy, pose.originPad.xyz, pose.rotation, shapeOrig, shapeRot);
                    const float3 localOrig = nwbPhysicsQuatInvRotate(rayOrig - shapeOrig, shapeRot);
                    const float3 localDir = nwbPhysicsQuatInvRotate(rayDir, shapeRot);
                    bool hfFallback = false;
                    if(hfThickness > 1.0e-5f){
                        hitShape = nwbPhysicsQuerySphereCastHeightfield(hullVertexSlot, spProxy, shapeOrig, shapeRot, qFlags, localOrig, localDir, evalMaxFrac, 0.0f, tHit, hitPtOverride, nHit, candTriIdx, candBary, hfFallback);
                        hasPtOverride = true;
                    }
                    else{
                        hitShape = nwbPhysicsQueryRayHeightfield(hullVertexSlot, spProxy, shapeRot, qFlags, localOrig, localDir, evalMaxFrac, tHit, nHit, candTriIdx, candBary, hfFallback);
                    }
                    if(hfFallback)
                        needsCpuFallback = true;
                }
                else{
#if NWB_PHYSICS_QUERY_HAS_MESH
                    float3 shapeOrig = pose.originPad.xyz;
                    float4 shapeRot = pose.rotation;
                    nwbPhysicsResolveShapeWorldPose(spProxy, pose.originPad.xyz, pose.rotation, shapeOrig, shapeRot);
                    const float3 localOrig = nwbPhysicsQuatInvRotate(rayOrig - shapeOrig, shapeRot);
                    const float3 localDir = nwbPhysicsQuatInvRotate(rayDir, shapeRot);
                    bool meshFallback = false;
                    if(skinR > 1.0e-5f){
                        hitShape = nwbPhysicsQuerySphereCastMesh(meshNodeSlot, meshTriSlot, spProxy, shapeOrig, shapeRot, qFlags, localOrig, localDir, evalMaxFrac, 0.0f, tHit, hitPtOverride, nHit, candTriIdx, candBary, meshFallback, meshStack, meshStackMetric);
                        hasPtOverride = true;
                    }
                    else{
                        hitShape = nwbPhysicsQueryRayMesh(meshNodeSlot, meshTriSlot, spProxy, shapeRot, qFlags, localOrig, localDir, evalMaxFrac, tHit, nHit, candTriIdx, candBary, meshFallback, meshStack, meshStackMetric);
                    }
                    if(meshFallback)
                        needsCpuFallback = true;
#else
                    needsCpuFallback = true;
#endif
                }
            }
            else if(shapeType == NWB_PHYSICS_SHAPE_TYPE_HEIGHTFIELD){
                useSphereMarch = false;
                float3 shapeOrig = pose.originPad.xyz;
                float4 shapeRot = pose.rotation;
                nwbPhysicsResolveShapeWorldPose(spProxy, pose.originPad.xyz, pose.rotation, shapeOrig, shapeRot);
                const float3 localOrig = nwbPhysicsQuatInvRotate(rayOrig - shapeOrig, shapeRot);
                const float3 localDir = nwbPhysicsQuatInvRotate(rayDir, shapeRot);
                bool hfFallback = false;
                hitShape = nwbPhysicsQuerySphereCastHeightfield(hullVertexSlot, spProxy, shapeOrig, shapeRot, qFlags, localOrig, localDir, evalMaxFrac, castRadius, tHit, hitPtOverride, nHit, candTriIdx, candBary, hfFallback);
                hasPtOverride = true;
                if(hfFallback){
                    needsCpuFallback = true;
                    return;
                }
            }
            else if(shapeType == NWB_PHYSICS_SHAPE_TYPE_MESH){
                useSphereMarch = false;
#if NWB_PHYSICS_QUERY_HAS_MESH
                float3 shapeOrig = pose.originPad.xyz;
                float4 shapeRot = pose.rotation;
                nwbPhysicsResolveShapeWorldPose(spProxy, pose.originPad.xyz, pose.rotation, shapeOrig, shapeRot);
                const float3 localOrig = nwbPhysicsQuatInvRotate(rayOrig - shapeOrig, shapeRot);
                const float3 localDir = nwbPhysicsQuatInvRotate(rayDir, shapeRot);
                bool meshFallback = false;
                hitShape = nwbPhysicsQuerySphereCastMesh(meshNodeSlot, meshTriSlot, spProxy, shapeOrig, shapeRot, qFlags, localOrig, localDir, evalMaxFrac, castRadius, tHit, hitPtOverride, nHit, candTriIdx, candBary, meshFallback, meshStack, meshStackMetric);
                hasPtOverride = true;
                if(meshFallback){
                    needsCpuFallback = true;
                    return;
                }
#else
                needsCpuFallback = true;
                return;
#endif
            }
            if(useSphereMarch){
                bool marchFallback = false;
                hitShape = nwbPhysicsQuerySphereMarchShape(spProxy, pose, hullVertexSlot, rayOrig, rayDir, rayInvDir, evalMaxFrac, aMin, aMax, marchRadius, tHit, hitPtOverride, nHit, marchFallback);
                hasPtOverride = true;
                if(marchFallback){
                    needsCpuFallback = true;
                    if(isSphereCast)
                        return;
                }
            }
            if(hitShape){
                candHit = true;
                candMetric = tHit;
                candPoint = hasPtOverride ? hitPtOverride : (rayOrig + rayDir * tHit);
                candNormal = nHit;
            }
        }
        else{
            float evalDist = sphereRadius;
            float acceptDistSq = sphereRadius * sphereRadius;
            if(queryType == NWB_PHYSICS_QUERY_TYPE_CLOSEST_POINT){
                evalDist = isMultiHit ? sphereRadius : sqrt(max(bestMetric, 0.0f));
                acceptDistSq = isMultiHit ? (sphereRadius * sphereRadius) : bestMetric;
            }
            else if(queryType == NWB_PHYSICS_QUERY_TYPE_OVERLAP_POINT){
                evalDist = 0.0f;
                acceptDistSq = 1.0e-10f;
            }

            float3 nOut = float3(0.0f, 1.0f, 0.0f);
            bool overlapFallback = false;
            float dSq = 1.0e30f;
            if(shapeType == NWB_PHYSICS_SHAPE_TYPE_MESH){
#if NWB_PHYSICS_QUERY_HAS_MESH
                float3 shapeOrig = pose.originPad.xyz;
                float4 shapeRot = pose.rotation;
                nwbPhysicsResolveShapeWorldPose(spProxy, pose.originPad.xyz, pose.rotation, shapeOrig, shapeRot);
                const float3 localP = nwbPhysicsQuatInvRotate(sphereCenter - shapeOrig, shapeRot);
                dSq = nwbPhysicsQueryMeshPointDistSq(meshNodeSlot, meshTriSlot, spProxy, shapeRot, localP, evalDist, nOut, candTriIdx, candBary, overlapFallback, meshStack, meshStackMetric);
#else
                overlapFallback = true;
#endif
            }
            else if(shapeType == NWB_PHYSICS_SHAPE_TYPE_HEIGHTFIELD){
                float3 shapeOrig = pose.originPad.xyz;
                float4 shapeRot = pose.rotation;
                nwbPhysicsResolveShapeWorldPose(spProxy, pose.originPad.xyz, pose.rotation, shapeOrig, shapeRot);
                const float3 localP = nwbPhysicsQuatInvRotate(sphereCenter - shapeOrig, shapeRot);
                dSq = nwbPhysicsQueryPointHeightfieldDistSq(hullVertexSlot, spProxy, shapeRot, localP, evalDist, nOut, candTriIdx, candBary, overlapFallback);
            }
            else{
                dSq = nwbPhysicsPrimitivePointDistSq(sphereCenter, pose, spProxy, hullVertexSlot, evalDist, nOut, overlapFallback);
            }
            if(overlapFallback){
                needsCpuFallback = true;
                return;
            }
            if(dSq <= acceptDistSq){
                candHit = true;
                if(queryType == NWB_PHYSICS_QUERY_TYPE_OVERLAP_POINT){
                    candMetric = 0.0f;
                    candPoint = sphereCenter;
                }
                else{
                    candMetric = dSq;
                    const float d = sqrt(max(dSq, 0.0f));
                    candPoint = sphereCenter - nOut * min(d, evalDist);
                }
                candNormal = nOut;
            }
        }
#endif
#if !NWB_PHYSICS_QUERY_IS_SHAPE_OP
    }
#endif

    if(!candHit)
        return;

    ++hitCount;
    if(multiHitSlot != NWB_PHYSICS_INVALID_INDEX && isMultiHit && writtenHits < maxHitsPerQuery){
        NwbPhysicsGpuSpatialQueryResult mhRes;
        if(isRayLike){
            mhRes.hitPointFraction = float4(candPoint, candMetric);
            mhRes.hitNormalDist = float4(candNormal, (isSphereCast || isShapeCast) ? (candMetric * length(rayDir)) : length(candPoint - rayOrig));
        }
        else{
            mhRes.hitPointFraction = float4(candPoint, 0.0f);
            mhRes.hitNormalDist = float4(candNormal, sqrt(max(candMetric, 0.0f)));
        }
        mhRes.hitIds = uint4(meta.z, meta.x, 1u, NWB_PHYSICS_QUERY_RESULT_FLAG_HIT);
        mhRes.hitTriBary = float4(candBary, asfloat(candTriIdx));
        g_NwbPhysicsSpatialQueryResultBuffers[multiHitSlot][queryIdx * maxHitsPerQuery + writtenHits] = mhRes;
        ++writtenHits;
    }

    if(!hasHit || candMetric < bestMetric){
        hasHit = true;
        bestMetric = candMetric;
        bestPoint = candPoint;
        bestNormal = candNormal;
        bestTriIdx = candTriIdx;
        bestBary = candBary;
        bestShapeId = meta.z;
        bestBodyIdx = meta.x;
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
#if NWB_PHYSICS_QUERY_HAS_MESH
    const uint meshNodeSlot = g_NwbPhysicsPushConstants.slots1.w;
    const uint meshTriSlot = g_NwbPhysicsPushConstants.flags;
#endif
    const uint auxSlot = asuint(g_NwbPhysicsPushConstants.gravitySpeculative.x);
    const uint multiHitSlot = asuint(g_NwbPhysicsPushConstants.gravitySpeculative.y);
    const uint maxHitsPerQuery = asuint(g_NwbPhysicsPushConstants.gravitySpeculative.z);
#if NWB_PHYSICS_QUERY_IS_SHAPE_OP
    const uint queryShapeSlot = asuint(g_NwbPhysicsPushConstants.gravitySpeculative.w);
#endif

    const NwbPhysicsGpuSpatialQuery q = g_NwbPhysicsSpatialQueryBuffers[querySlot][tid];
    const uint queryType = q.params.x;
    const uint qCat = q.params.y;
    const uint qMask = q.params.z;
    const uint qFlags = q.params.w;

    const bool isSphereCast = (queryType == NWB_PHYSICS_QUERY_TYPE_SPHERE_CAST);
    const bool isShapeCast = (queryType == NWB_PHYSICS_QUERY_TYPE_SHAPE_CAST);
    const bool isOverlapShape = (queryType == NWB_PHYSICS_QUERY_TYPE_OVERLAP_SHAPE);
#if NWB_PHYSICS_QUERY_IS_SHAPE_OP
    if(!isShapeCast && !isOverlapShape)
        return;
    const bool isRayLike = isShapeCast;
#else
    if(isShapeCast || isOverlapShape)
        return;
    const bool isRayCast = (queryType == NWB_PHYSICS_QUERY_TYPE_RAYCAST_CLOSEST) || (queryType == NWB_PHYSICS_QUERY_TYPE_RAYCAST_ANY);
    const bool isRayLike = isRayCast || isSphereCast;
#endif
    const bool isMultiHit = (qFlags & NWB_PHYSICS_QUERY_FLAG_MULTI_HIT) != 0u;

    uint ignoreBodyIdx = NWB_PHYSICS_INVALID_INDEX;
    uint ignoreShapeId = NWB_PHYSICS_INVALID_INDEX;
    int auxGroupIndex = 0;
    if((qFlags & NWB_PHYSICS_QUERY_FLAG_HAS_AUX) != 0u && auxSlot != NWB_PHYSICS_INVALID_INDEX){
        const float4 aux = g_NwbPhysicsFloat4Buffers[auxSlot][tid];
        ignoreBodyIdx = asuint(aux.x);
        ignoreShapeId = asuint(aux.y);
        auxGroupIndex = asint(aux.z);
    }

    bool missingQueryShape = false;
#if NWB_PHYSICS_QUERY_IS_SHAPE_OP
    NwbPhysicsGpuShapeProxy qShapeProxy = (NwbPhysicsGpuShapeProxy)0;
    qShapeProxy.bodyIndexTypeShape = uint4(NWB_PHYSICS_INVALID_INDEX, NWB_PHYSICS_INVALID_INDEX, NWB_PHYSICS_INVALID_INDEX, 0u);
    qShapeProxy.localRotationOrMeta = float4(0.0f, 0.0f, 0.0f, 1.0f);
    qShapeProxy.localTransformRot = float4(0.0f, 0.0f, 0.0f, 1.0f);
    float3 qStartOrig = q.originMin.xyz;
    float4 qStartRot = float4(0.0f, 0.0f, 0.0f, 1.0f);
    float qSkinR = 0.0f;
    float3 qRelMin = float3(0.0f, 0.0f, 0.0f);
    float3 qRelMax = float3(0.0f, 0.0f, 0.0f);
    NwbPhysicsGpuSupportCache qCache0 = (NwbPhysicsGpuSupportCache)0;
    if(queryShapeSlot == NWB_PHYSICS_INVALID_INDEX){
        missingQueryShape = true;
    }
    else{
        qShapeProxy = g_NwbPhysicsShapeProxyBuffers[queryShapeSlot][tid];
        if(qShapeProxy.bodyIndexTypeShape.y > NWB_PHYSICS_SHAPE_TYPE_SEGMENT){
            missingQueryShape = true;
        }
        else if(
            qShapeProxy.bodyIndexTypeShape.y == NWB_PHYSICS_SHAPE_TYPE_HULL
            && (qShapeProxy.bodyIndexTypeShape.w & NWB_PHYSICS_SHAPE_FLAG_IS_BOX) == 0u
            && (hullVertexSlot == NWB_PHYSICS_INVALID_INDEX || qShapeProxy.filterData.w == NWB_PHYSICS_INVALID_INDEX || (asuint(qShapeProxy.localRotationOrMeta.x) & 0xffu) == 0u)
        ){
            missingQueryShape = true;
        }
        else{
            nwbPhysicsResolveShapeWorldPose(qShapeProxy, q.originMin.xyz, float4(0.0f, 0.0f, 0.0f, 1.0f), qStartOrig, qStartRot);
            qSkinR = nwbPhysicsProxySkinRadius(qShapeProxy);
            qCache0 = nwbPhysicsBuildSupportCache(qShapeProxy, qStartOrig, qStartRot);
            nwbPhysicsComputeSupportWorldRelAabb(qCache0, hullVertexSlot, qSkinR, q.originMin.xyz, qRelMin, qRelMax);
        }
    }
#endif

    const float3 rayOrig = q.originMin.xyz;
    const float3 rayDir = q.dirMax.xyz;
    const float3 rayInvDir = isRayLike
        ? float3(
            abs(rayDir.x) > 1.0e-12f ? (1.0f / rayDir.x) : 1.0e30f,
            abs(rayDir.y) > 1.0e-12f ? (1.0f / rayDir.y) : 1.0e30f,
            abs(rayDir.z) > 1.0e-12f ? (1.0f / rayDir.z) : 1.0e30f
        )
        : float3(0.0f, 0.0f, 0.0f);
    const float maxFrac = q.dirMax.w;
#if NWB_PHYSICS_QUERY_IS_SHAPE_OP
    const float3 castPadMin = qRelMax;
    const float3 castPadMax = -qRelMin;
#else
    const float castRadius = isSphereCast ? max(q.originMin.w, 0.0f) : 0.0f;
    const float3 castPadMin = float3(castRadius, castRadius, castRadius);
    const float3 castPadMax = float3(castRadius, castRadius, castRadius);
    const float3 sphereCenter = q.originMin.xyz;
    const float sphereRadius = max(q.dirMax.w, 0.0f);
    const float maxDistSqLimit = sphereRadius * sphereRadius;
#endif

    float3 qBoxMin = q.originMin.xyz;
    float3 qBoxMax = q.dirMax.xyz;
#if NWB_PHYSICS_QUERY_IS_SHAPE_OP
    if(isShapeCast){
        const float3 rayEnd = rayOrig + rayDir * maxFrac;
        qBoxMin = min(rayOrig + qRelMin, rayEnd + qRelMin);
        qBoxMax = max(rayOrig + qRelMax, rayEnd + qRelMax);
    }
    else{
        qBoxMin = rayOrig + qRelMin;
        qBoxMax = rayOrig + qRelMax;
    }
#else
    if(isRayLike){
        const float3 rayEnd = rayOrig + rayDir * maxFrac;
        qBoxMin = min(rayOrig, rayEnd) - castPadMin;
        qBoxMax = max(rayOrig, rayEnd) + castPadMax;
    }
    else if(queryType == NWB_PHYSICS_QUERY_TYPE_SPHERE_OVERLAP || queryType == NWB_PHYSICS_QUERY_TYPE_CLOSEST_POINT){
        qBoxMin = sphereCenter - float3(sphereRadius, sphereRadius, sphereRadius);
        qBoxMax = sphereCenter + float3(sphereRadius, sphereRadius, sphereRadius);
    }
    else if(queryType == NWB_PHYSICS_QUERY_TYPE_OVERLAP_POINT){
        const float3 eps = float3(1.0e-4f, 1.0e-4f, 1.0e-4f);
        qBoxMin = sphereCenter - eps;
        qBoxMax = sphereCenter + eps;
    }
#endif
    const float3 qCenter = (qBoxMin + qBoxMax) * 0.5f;

#if NWB_PHYSICS_QUERY_IS_SHAPE_OP
    float bestMetric = isRayLike ? maxFrac : 1.0e30f;
#else
    float bestMetric = isRayLike ? maxFrac : ((queryType == NWB_PHYSICS_QUERY_TYPE_CLOSEST_POINT) ? maxDistSqLimit : 1.0e30f);
#endif
    float3 bestPoint = float3(0.0f, 0.0f, 0.0f);
    float3 bestNormal = float3(0.0f, 1.0f, 0.0f);
    uint bestTriIdx = NWB_PHYSICS_INVALID_INDEX;
    float3 bestBary = float3(0.0f, 0.0f, 0.0f);
    uint bestShapeId = NWB_PHYSICS_INVALID_INDEX;
    uint bestBodyIdx = NWB_PHYSICS_INVALID_INDEX;
    uint hitCount = 0u;
    uint writtenHits = 0u;
    bool hasHit = false;
    bool needsCpuFallback = missingQueryShape;

    const bool traverseLbvh = useLbvh && (lbvhNodeSlot != NWB_PHYSICS_INVALID_INDEX);
    const uint leafStart = traverseLbvh ? (shapeCount - 1u) : 0u;
#if NWB_PHYSICS_QUERY_IS_SHAPE_OP
    const bool sortNodes = isRayLike;
#else
    const bool sortNodes = isRayLike || (queryType == NWB_PHYSICS_QUERY_TYPE_CLOSEST_POINT);
#endif
    if(shapeCount > 0u && !needsCpuFallback){
        uint stack[64];
        float stackT[64];
#if NWB_PHYSICS_QUERY_HAS_MESH
        uint meshStack[64];
        float meshStackMetric[64];
#endif
        stack[0] = 0u;
        stackT[0] = 0.0f;
        uint sp = 0u;
        uint bruteIdx = 0u;
        if(traverseLbvh){
            const NwbPhysicsGpuLbvhNode root = g_NwbPhysicsLbvhNodeReadBuffers[lbvhNodeSlot][0u];
            bool rootHit = false;
            float rootT = 0.0f;
            if(isRayLike){
                const float evalFrac = isMultiHit ? maxFrac : bestMetric;
                rootHit = nwbPhysicsQueryRayAabbFast(rayOrig, rayInvDir, root.minLeft.xyz - castPadMin, root.maxRight.xyz + castPadMax, evalFrac, rootT);
            }
#if !NWB_PHYSICS_QUERY_IS_SHAPE_OP
            else if(queryType == NWB_PHYSICS_QUERY_TYPE_SPHERE_OVERLAP || queryType == NWB_PHYSICS_QUERY_TYPE_CLOSEST_POINT){
                const float evalDistSq = (queryType == NWB_PHYSICS_QUERY_TYPE_SPHERE_OVERLAP || isMultiHit) ? maxDistSqLimit : bestMetric;
                rootT = nwbPhysicsQueryAabbDistSq(sphereCenter, root.minLeft.xyz, root.maxRight.xyz);
                rootHit = rootT <= evalDistSq;
            }
#endif
            else{
                rootHit = nwbPhysicsAabbOverlaps(qBoxMin, qBoxMax, root.minLeft.xyz, root.maxRight.xyz);
            }
            if(rootHit){
                stack[0] = 0u;
                stackT[0] = rootT;
                sp = 1u;
            }
        }

        while(((traverseLbvh && sp > 0u) || (!traverseLbvh && bruteIdx < shapeCount)) && !(queryType == NWB_PHYSICS_QUERY_TYPE_RAYCAST_ANY && hasHit && !isMultiHit) && !needsCpuFallback){
            uint leafCount = 0u;
            uint leafShapes[2] = { 0u, 0u };
            float3 leafMin[2] = { float3(0.0f, 0.0f, 0.0f), float3(0.0f, 0.0f, 0.0f) };
            float3 leafMax[2] = { float3(0.0f, 0.0f, 0.0f), float3(0.0f, 0.0f, 0.0f) };
            float leafT[2] = { 0.0f, 0.0f };
            if(traverseLbvh){
                --sp;
                if(sortNodes && !isMultiHit && stackT[sp] > bestMetric)
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
                if(isRayLike){
                    const float evalFrac = isMultiHit ? maxFrac : bestMetric;
                    hitL = nwbPhysicsQueryRayAabbFast(rayOrig, rayInvDir, lNode.minLeft.xyz - castPadMin, lNode.maxRight.xyz + castPadMax, evalFrac, tL);
                    hitR = nwbPhysicsQueryRayAabbFast(rayOrig, rayInvDir, rNode.minLeft.xyz - castPadMin, rNode.maxRight.xyz + castPadMax, evalFrac, tR);
                }
#if !NWB_PHYSICS_QUERY_IS_SHAPE_OP
                else if(queryType == NWB_PHYSICS_QUERY_TYPE_SPHERE_OVERLAP || queryType == NWB_PHYSICS_QUERY_TYPE_CLOSEST_POINT){
                    const float evalDistSq = (queryType == NWB_PHYSICS_QUERY_TYPE_SPHERE_OVERLAP || isMultiHit) ? maxDistSqLimit : bestMetric;
                    tL = nwbPhysicsQueryAabbDistSq(sphereCenter, lNode.minLeft.xyz, lNode.maxRight.xyz);
                    tR = nwbPhysicsQueryAabbDistSq(sphereCenter, rNode.minLeft.xyz, rNode.maxRight.xyz);
                    hitL = tL <= evalDistSq;
                    hitR = tR <= evalDistSq;
                }
#endif
                else{
                    hitL = nwbPhysicsAabbOverlaps(qBoxMin, qBoxMax, lNode.minLeft.xyz, lNode.maxRight.xyz);
                    hitR = nwbPhysicsAabbOverlaps(qBoxMin, qBoxMax, rNode.minLeft.xyz, rNode.maxRight.xyz);
                }
                const bool swapOrder = sortNodes && hitL && hitR && (tR < tL);
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
                float candAabbT = 0.0f;
                if(isRayLike){
                    const float evalFrac = isMultiHit ? maxFrac : bestMetric;
                    if(!nwbPhysicsQueryRayAabbFast(rayOrig, rayInvDir, aMin - castPadMin, aMax + castPadMax, evalFrac, candAabbT))
                        continue;
                }
#if !NWB_PHYSICS_QUERY_IS_SHAPE_OP
                else if(queryType == NWB_PHYSICS_QUERY_TYPE_SPHERE_OVERLAP || queryType == NWB_PHYSICS_QUERY_TYPE_CLOSEST_POINT){
                    const float evalDistSq = (queryType == NWB_PHYSICS_QUERY_TYPE_SPHERE_OVERLAP || isMultiHit) ? maxDistSqLimit : bestMetric;
                    candAabbT = nwbPhysicsQueryAabbDistSq(sphereCenter, aMin, aMax);
                    if(candAabbT > evalDistSq)
                        continue;
                }
#endif
                else if(!nwbPhysicsAabbOverlaps(qBoxMin, qBoxMax, aMin, aMax)){
                    continue;
                }
                leafShapes[0] = shapeIdx;
                leafMin[0] = aMin;
                leafMax[0] = aMax;
                leafT[0] = candAabbT;
                leafCount = 1u;
            }
            [loop]
            for(uint li = 0u; li < leafCount; ++li){
                if(needsCpuFallback || (queryType == NWB_PHYSICS_QUERY_TYPE_RAYCAST_ANY && hasHit && !isMultiHit))
                    break;
                if(sortNodes && !isMultiHit && leafT[li] > bestMetric)
                    continue;
                nwbPhysicsTestSpatialQueryShape(
                    tid,
                    shapeSlot,
                    bodyPoseSlot,
                    hullVertexSlot,
#if NWB_PHYSICS_QUERY_HAS_MESH
                    meshNodeSlot,
                    meshTriSlot,
#endif
                    queryType,
                    qCat,
                    qMask,
                    qFlags,
                    ignoreBodyIdx,
                    ignoreShapeId,
                    auxGroupIndex,
                    multiHitSlot,
                    maxHitsPerQuery,
                    rayOrig,
                    rayDir,
                    rayInvDir,
                    maxFrac,
#if !NWB_PHYSICS_QUERY_IS_SHAPE_OP
                    sphereCenter,
                    sphereRadius,
                    castRadius,
#endif
                    qCenter,
#if NWB_PHYSICS_QUERY_IS_SHAPE_OP
                    qShapeProxy,
                    qCache0,
                    qStartOrig,
                    qStartRot,
                    qSkinR,
                    qRelMin,
                    qRelMax,
#endif
                    leafShapes[li],
                    leafMin[li],
                    leafMax[li],
                    bestMetric,
                    bestPoint,
                    bestNormal,
                    bestTriIdx,
                    bestBary,
                    bestShapeId,
                    bestBodyIdx,
                    hitCount,
                    writtenHits,
                    hasHit,
                    needsCpuFallback
#if NWB_PHYSICS_QUERY_HAS_MESH
                    ,
                    meshStack,
                    meshStackMetric
#endif
                );
            }
        }
    }

    NwbPhysicsGpuSpatialQueryResult outRes;
    const uint fbFlag = needsCpuFallback ? NWB_PHYSICS_QUERY_RESULT_FLAG_NEEDS_CPU_FALLBACK : 0u;
    if(hasHit){
        if(isRayLike){
            outRes.hitPointFraction = float4(bestPoint, bestMetric);
            outRes.hitNormalDist = float4(bestNormal, (isSphereCast || isShapeCast) ? (bestMetric * length(rayDir)) : length(bestPoint - rayOrig));
        }
        else{
            outRes.hitPointFraction = float4(bestPoint, 0.0f);
            outRes.hitNormalDist = float4(bestNormal, sqrt(max(bestMetric, 0.0f)));
        }
        outRes.hitIds = uint4(bestShapeId, bestBodyIdx, hitCount, NWB_PHYSICS_QUERY_RESULT_FLAG_HIT | fbFlag);
        outRes.hitTriBary = float4(bestBary, asfloat(bestTriIdx));
    }
    else{
        outRes.hitPointFraction = float4(0.0f, 0.0f, 0.0f, 0.0f);
        outRes.hitNormalDist = float4(0.0f, 1.0f, 0.0f, 0.0f);
        outRes.hitIds = uint4(NWB_PHYSICS_INVALID_INDEX, NWB_PHYSICS_INVALID_INDEX, 0u, fbFlag);
        outRes.hitTriBary = float4(0.0f, 0.0f, 0.0f, asfloat(NWB_PHYSICS_INVALID_INDEX));
    }
    g_NwbPhysicsSpatialQueryResultBuffers[resultSlot][tid] = outRes;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

