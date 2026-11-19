// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "physics_common.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
    const float3 localCenter = float3(body.invInertiaWorldRow0.w, body.invInertiaWorldRow1.w, body.invInertiaWorldRow2.w);
    outOrigin = body.positionInvMass.xyz - nwbPhysicsQuatRotate(localCenter, outRot);
}

void nwbPhysicsBoxVertices(const float3 center, const float4 rot, const float3 halfExtents, out float3 outVerts[8]){
    const float3 ax = nwbPhysicsQuatRotate(float3(halfExtents.x, 0.0f, 0.0f), rot);
    const float3 ay = nwbPhysicsQuatRotate(float3(0.0f, halfExtents.y, 0.0f), rot);
    const float3 az = nwbPhysicsQuatRotate(float3(0.0f, 0.0f, halfExtents.z), rot);
    outVerts[0] = center - ax - ay - az;
    outVerts[1] = center + ax - ay - az;
    outVerts[2] = center + ax + ay - az;
    outVerts[3] = center - ax + ay - az;
    outVerts[4] = center - ax - ay + az;
    outVerts[5] = center + ax - ay + az;
    outVerts[6] = center + ax + ay + az;
    outVerts[7] = center - ax + ay + az;
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_PHYSICS_WORKGROUP_SIZE, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint idx = dispatchThreadID.x;
    const uint pairCount = g_NwbPhysicsPushConstants.itemCount;
    if(idx >= pairCount)
        return;

    const uint bodySimSlot = g_NwbPhysicsPushConstants.slots0.x;
    const uint shapeSlot = g_NwbPhysicsPushConstants.slots0.z;
    const uint pairSlot = g_NwbPhysicsPushConstants.slots1.x;
    const uint manifoldSlot = g_NwbPhysicsPushConstants.slots1.z;
    const float speculativeDist = g_NwbPhysicsPushConstants.gravitySpeculative.w;

    const NwbPhysicsGpuBroadphasePair pair = g_NwbPhysicsBroadphasePairBuffers[pairSlot][idx];
    const NwbPhysicsGpuShapeProxy proxyA = g_NwbPhysicsShapeProxyBuffers[shapeSlot][pair.indices.x];
    const NwbPhysicsGpuShapeProxy proxyB = g_NwbPhysicsShapeProxyBuffers[shapeSlot][pair.indices.y];
    const NwbPhysicsGpuBodySim bodyA = g_NwbPhysicsBodySimBuffers[bodySimSlot][pair.indices.z];
    const NwbPhysicsGpuBodySim bodyB = g_NwbPhysicsBodySimBuffers[bodySimSlot][pair.indices.w];

    float3 originA;
    float4 rotA;
    nwbPhysicsWorldTransform(bodyA, originA, rotA);
    float3 originB;
    float4 rotB;
    nwbPhysicsWorldTransform(bodyB, originB, rotB);

    NwbPhysicsGpuNarrowphaseManifold result;
    result.pairInfo = uint4(pair.indices.x, pair.indices.y, 0u, 0u);
    result.normalPad = float4(0.0f, 1.0f, 0.0f, 0.0f);
    result.featureIds = uint4(0u, 0u, 0u, 0u);
    for(uint k = 0u; k < NWB_PHYSICS_MAX_MANIFOLD_POINTS; ++k){
        result.anchorA[k] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        result.anchorBSeparation[k] = float4(0.0f, 0.0f, 0.0f, 0.0f);
    }

    const uint typeA = proxyA.bodyIndexTypeShape.y;
    const uint typeB = proxyB.bodyIndexTypeShape.y;
    const bool isBoxA = (typeA == NWB_PHYSICS_SHAPE_TYPE_HULL) && ((proxyA.bodyIndexTypeShape.w & NWB_PHYSICS_SHAPE_FLAG_IS_BOX) != 0u);
    const bool isBoxB = (typeB == NWB_PHYSICS_SHAPE_TYPE_HULL) && ((proxyB.bodyIndexTypeShape.w & NWB_PHYSICS_SHAPE_FLAG_IS_BOX) != 0u);

    if((typeA == NWB_PHYSICS_SHAPE_TYPE_HULL && !isBoxA) || (typeB == NWB_PHYSICS_SHAPE_TYPE_HULL && !isBoxB)){
        result.pairInfo.w = 1u;
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
        const float maxD = totalR + speculativeDist;
        if(distSq <= maxD * maxD){
            const float dist = sqrt(distSq);
            const float3 n = dist > 1.0e-6f ? (d / dist) : float3(0.0f, 1.0f, 0.0f);
            result.pairInfo.z = 1u;
            result.normalPad = float4(n, 0.0f);
            result.anchorA[0] = float4(cA + n * rA - bodyA.positionInvMass.xyz, 0.0f);
            result.anchorBSeparation[0] = float4(cB - n * rB - bodyB.positionInvMass.xyz, dist - totalR);
            result.featureIds.x = 1u;
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
        const float maxD = totalR + speculativeDist;
        if(distSq <= maxD * maxD){
            const float dist = sqrt(distSq);
            const float3 n = dist > 1.0e-6f ? (d / dist) : float3(0.0f, 1.0f, 0.0f);
            result.pairInfo.z = 1u;
            result.normalPad = float4(n, 0.0f);
            result.anchorA[0] = float4(cA + n * rA - bodyA.positionInvMass.xyz, 0.0f);
            result.anchorBSeparation[0] = float4(q - n * rB - bodyB.positionInvMass.xyz, dist - totalR);
            result.featureIds.x = 2u;
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
        const float maxD = totalR + speculativeDist;
        if(distSq <= maxD * maxD){
            const float dist = sqrt(distSq);
            const float3 n = dist > 1.0e-6f ? (d / dist) : float3(0.0f, 1.0f, 0.0f);
            result.pairInfo.z = 1u;
            result.normalPad = float4(n, 0.0f);
            result.anchorA[0] = float4(q + n * rA - bodyA.positionInvMass.xyz, 0.0f);
            result.anchorBSeparation[0] = float4(cB - n * rB - bodyB.positionInvMass.xyz, dist - totalR);
            result.featureIds.x = 3u;
        }
    }
    else if(typeA == NWB_PHYSICS_SHAPE_TYPE_CAPSULE && typeB == NWB_PHYSICS_SHAPE_TYPE_CAPSULE){
        const float3 a0 = originA + nwbPhysicsQuatRotate(proxyA.localCenterRadius.xyz, rotA);
        const float3 a1 = originA + nwbPhysicsQuatRotate(proxyA.localExtentOrP1.xyz, rotA);
        const float3 b0 = originB + nwbPhysicsQuatRotate(proxyB.localCenterRadius.xyz, rotB);
        const float3 b1 = originB + nwbPhysicsQuatRotate(proxyB.localExtentOrP1.xyz, rotB);
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
        const float maxD = totalR + speculativeDist;
        if(distSq <= maxD * maxD){
            const float dist = sqrt(distSq);
            const float3 n = dist > 1.0e-6f ? (d / dist) : nwbPhysicsSafeNormal(cross(a1 - a0, float3(1.0f, 0.0f, 0.0f)), float3(0.0f, 1.0f, 0.0f));
            result.pairInfo.z = 1u;
            result.normalPad = float4(n, 0.0f);
            result.anchorA[0] = float4(cA + n * rA - bodyA.positionInvMass.xyz, 0.0f);
            result.anchorBSeparation[0] = float4(cB - n * rB - bodyB.positionInvMass.xyz, dist - totalR);
            result.featureIds.x = 4u;
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
        const float maxD = totalR + speculativeDist;
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
            result.anchorA[0] = float4((sphereIsA ? worldSpherePt : finalBoxPt) - bodyA.positionInvMass.xyz, 0.0f);
            result.anchorBSeparation[0] = float4((sphereIsA ? finalBoxPt : worldSpherePt) - bodyB.positionInvMass.xyz, separation);
            result.featureIds.x = 5u;
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

        int bestAxis = -1;
        float bestSep = -1.0e20f;
        float3 bestNormal = float3(0.0f, 1.0f, 0.0f);

        for(int i = 0; i < 3; ++i){
            float3 n = axesA[i];
            if(dot(n, dCenter) < 0.0f)
                n = -n;
            const float proj = abs(dot(n, dCenter));
            const float sep = proj - (halfA[i] + nwbPhysicsProjectBoxHalfExtent(axB, ayB, azB, hB, n)) - totalR;
            if(sep > speculativeDist){
                g_NwbPhysicsNarrowphaseManifoldBuffers[manifoldSlot][idx] = result;
                return;
            }
            if(sep > bestSep){
                bestSep = sep;
                bestAxis = i;
                bestNormal = n;
            }
        }

        for(int i = 0; i < 3; ++i){
            float3 n = axesB[i];
            if(dot(n, dCenter) < 0.0f)
                n = -n;
            const float proj = abs(dot(n, dCenter));
            const float sep = proj - (nwbPhysicsProjectBoxHalfExtent(axA, ayA, azA, hA, n) + halfB[i]) - totalR;
            if(sep > speculativeDist){
                g_NwbPhysicsNarrowphaseManifoldBuffers[manifoldSlot][idx] = result;
                return;
            }
            if(sep > bestSep + 1.0e-4f){
                bestSep = sep;
                bestAxis = 3 + i;
                bestNormal = n;
            }
        }

        for(int i = 0; i < 3; ++i){
            for(int j = 0; j < 3; ++j){
                const float3 rawCross = cross(axesA[i], axesB[j]);
                const float lenSq = dot(rawCross, rawCross);
                if(lenSq < 1.0e-5f)
                    continue;
                float3 n = rawCross * rsqrt(lenSq);
                if(dot(n, dCenter) < 0.0f)
                    n = -n;
                const float proj = abs(dot(n, dCenter));
                const float sep = proj - (nwbPhysicsProjectBoxHalfExtent(axA, ayA, azA, hA, n) + nwbPhysicsProjectBoxHalfExtent(axB, ayB, azB, hB, n)) - totalR;
                if(sep > speculativeDist){
                    g_NwbPhysicsNarrowphaseManifoldBuffers[manifoldSlot][idx] = result;
                    return;
                }
                if(sep > bestSep + 5.0e-4f){
                    bestSep = sep;
                    bestAxis = 6 + i * 3 + j;
                    bestNormal = n;
                }
            }
        }

        if(bestAxis >= 6){
            // Edge-edge single representative midpoint.
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
            result.anchorA[0] = float4(cEdgeA + bestNormal * rA - bodyA.positionInvMass.xyz, 0.0f);
            result.anchorBSeparation[0] = float4(cEdgeB - bestNormal * rB - bodyB.positionInvMass.xyz, bestSep);
            result.featureIds.x = uint(100 + bestAxis);
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

            uint keptCount = 0u;
            for(uint p = 0u; p < polyCount && keptCount < NWB_PHYSICS_MAX_MANIFOLD_POINTS; ++p){
                const float sep = dot(clipRefN, poly0[p]) - refPlaneDist - totalR;
                if(sep <= speculativeDist){
                    const float3 wRef = poly0[p] - clipRefN * (sep + totalR - (refIsA ? rA : rB));
                    const float3 wInc = poly0[p] - clipRefN * (refIsA ? rB : rA);
                    const float3 wA = refIsA ? wRef : wInc;
                    const float3 wB = refIsA ? wInc : wRef;
                    result.anchorA[keptCount] = float4(wA - bodyA.positionInvMass.xyz, 0.0f);
                    result.anchorBSeparation[keptCount] = float4(wB - bodyB.positionInvMass.xyz, sep);
                    if(keptCount == 0u) result.featureIds.x = uint(200u + p);
                    else if(keptCount == 1u) result.featureIds.y = uint(200u + p);
                    else if(keptCount == 2u) result.featureIds.z = uint(200u + p);
                    else result.featureIds.w = uint(200u + p);
                    ++keptCount;
                }
            }
            result.pairInfo.z = keptCount;
            result.normalPad = float4(bestNormal, 0.0f);
        }
    }
    else{
        result.pairInfo.w = 1u;
    }

    g_NwbPhysicsNarrowphaseManifoldBuffers[manifoldSlot][idx] = result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

