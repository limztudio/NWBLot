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

    const uint vOffset = proxy.filterData.w & 0x00ffffffu;
    const uint vCount = min(proxy.filterData.w >> 24u, 128u);
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
    const uint hullVertexSlot = g_NwbPhysicsPushConstants.slots1.y;
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
    if(typeA == NWB_PHYSICS_SHAPE_TYPE_MESH || typeB == NWB_PHYSICS_SHAPE_TYPE_MESH){
        result.pairInfo.w = 1u;
        g_NwbPhysicsNarrowphaseManifoldBuffers[manifoldSlot][idx] = result;
        return;
    }

    const bool isBoxA = (typeA == NWB_PHYSICS_SHAPE_TYPE_HULL) && ((proxyA.bodyIndexTypeShape.w & NWB_PHYSICS_SHAPE_FLAG_IS_BOX) != 0u);
    const bool isBoxB = (typeB == NWB_PHYSICS_SHAPE_TYPE_HULL) && ((proxyB.bodyIndexTypeShape.w & NWB_PHYSICS_SHAPE_FLAG_IS_BOX) != 0u);

    if((typeA == NWB_PHYSICS_SHAPE_TYPE_HULL && !isBoxA) || (typeB == NWB_PHYSICS_SHAPE_TYPE_HULL && !isBoxB)){
        const float totalR = proxyA.localCenterRadius.w + proxyB.localCenterRadius.w;
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
        sIdxA[0] = 0u;
        sIdxB[0] = 0u;
        sWA[0] = nwbPhysicsSupportWorld(proxyA, originA, rotA, hullVertexSlot, float3(1.0f, 0.0f, 0.0f), sIdxA[0]);
        sWB[0] = nwbPhysicsSupportWorld(proxyB, originB, rotB, hullVertexSlot, float3(-1.0f, 0.0f, 0.0f), sIdxB[0]);
        sW[0] = sWB[0] - sWA[0];
        sU[0] = 1.0f;

        bool hitDeep = false;
        for(uint iter = 0u; iter < 24u; ++iter){
            if(sCount == 2u)
                nwbPhysicsSolveSimplex2(sW, sWA, sWB, sIdxA, sIdxB, sU, sCount);
            else if(sCount == 3u)
                nwbPhysicsSolveSimplex3(sW, sWA, sWB, sIdxA, sIdxB, sU, sCount);

            if(sCount == 4u){
                hitDeep = true;
                break;
            }

            float3 pClosest = float3(0.0f, 0.0f, 0.0f);
            for(uint k = 0u; k < sCount; ++k)
                pClosest += sW[k] * sU[k];

            const float pLenSq = dot(pClosest, pClosest);
            if(pLenSq <= 1.0e-10f){
                hitDeep = true;
                break;
            }

            const float3 d = -pClosest;
            uint idxA = 0u;
            uint idxB = 0u;
            const float3 wA = nwbPhysicsSupportWorld(proxyA, originA, rotA, hullVertexSlot, -d, idxA);
            const float3 wB = nwbPhysicsSupportWorld(proxyB, originB, rotB, hullVertexSlot, d, idxB);

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

            if(sCount == 3u){
                const float testD = dot(wNew - sW[0], cross(sW[1] - sW[0], sW[2] - sW[0]));
                const float oriD = dot(-sW[0], cross(sW[1] - sW[0], sW[2] - sW[0]));
                if(testD * oriD >= 0.0f && abs(testD) > 1.0e-10f){
                    hitDeep = true;
                    break;
                }
            }

            sIdxA[sCount] = idxA;
            sIdxB[sCount] = idxB;
            sWA[sCount] = wA;
            sWB[sCount] = wB;
            sW[sCount] = wNew;
            sU[sCount] = 0.0f;
            ++sCount;
        }

        if(hitDeep){
            result.pairInfo.w = 1u;
            g_NwbPhysicsNarrowphaseManifoldBuffers[manifoldSlot][idx] = result;
            return;
        }

        float3 pA = float3(0.0f, 0.0f, 0.0f);
        float3 pB = float3(0.0f, 0.0f, 0.0f);
        for(uint k = 0u; k < sCount; ++k){
            pA += sWA[k] * sU[k];
            pB += sWB[k] * sU[k];
        }
        const float3 delta = pB - pA;
        const float dist = sqrt(dot(delta, delta));
        if(dist <= 1.0e-5f){
            result.pairInfo.w = 1u;
        }
        else{
            const float sep = dist - totalR;
            if(sep <= speculativeDist){
                const float3 n = delta / dist;
                result.pairInfo.z = 1u;
                result.normalPad = float4(n, 0.0f);
                result.anchorA[0] = float4(pA + n * proxyA.localCenterRadius.w - bodyA.positionInvMass.xyz, 0.0f);
                result.anchorBSeparation[0] = float4(pB - n * proxyB.localCenterRadius.w - bodyB.positionInvMass.xyz, sep);
                result.featureIds.x = 20u;
            }
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

            float3 candWRef[8];
            float3 candWInc[8];
            float3 candPos[8];
            float candSep[8];
            uint candFeat[8];
            uint candCount = 0u;
            for(uint p = 0u; p < polyCount && candCount < 8u; ++p){
                const float sep = dot(clipRefN, poly0[p]) - refPlaneDist - totalR;
                if(sep <= speculativeDist){
                    candWRef[candCount] = poly0[p] - clipRefN * (sep + totalR - (refIsA ? rA : rB));
                    candWInc[candCount] = poly0[p] - clipRefN * (refIsA ? rB : rA);
                    candPos[candCount] = poly0[p];
                    candSep[candCount] = sep;
                    candFeat[candCount] = uint(200u + p);
                    ++candCount;
                }
            }

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
                result.anchorA[k] = float4(wA - bodyA.positionInvMass.xyz, 0.0f);
                result.anchorBSeparation[k] = float4(wB - bodyB.positionInvMass.xyz, candSep[ci]);
                if(k == 0u) result.featureIds.x = candFeat[ci];
                else if(k == 1u) result.featureIds.y = candFeat[ci];
                else if(k == 2u) result.featureIds.z = candFeat[ci];
                else result.featureIds.w = candFeat[ci];
            }
            result.pairInfo.z = outCount;
            result.normalPad = float4(bestNormal, 0.0f);
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

        float bestDistSq = 1.0e20f;
        float3 bestSegPt = l0;
        float3 bestBoxPt = clamp(l0, -hBox, hBox);

        for(uint si = 0u; si < 5u; ++si){
            const float t = float(si) * 0.25f;
            const float3 sp = l0 + dSeg * t;
            const float3 bp = clamp(sp, -hBox, hBox);
            const float3 diff = sp - bp;
            const float dSq = dot(diff, diff);
            if(dSq < bestDistSq){
                bestDistSq = dSq;
                bestSegPt = sp;
                bestBoxPt = bp;
            }
        }

        const float3 edgeStarts[12] = {
            float3(-hBox.x, -hBox.y, -hBox.z), float3(-hBox.x,  hBox.y, -hBox.z), float3(-hBox.x, -hBox.y,  hBox.z), float3(-hBox.x,  hBox.y,  hBox.z),
            float3(-hBox.x, -hBox.y, -hBox.z), float3( hBox.x, -hBox.y, -hBox.z), float3(-hBox.x, -hBox.y,  hBox.z), float3( hBox.x, -hBox.y,  hBox.z),
            float3(-hBox.x, -hBox.y, -hBox.z), float3( hBox.x, -hBox.y, -hBox.z), float3(-hBox.x,  hBox.y, -hBox.z), float3( hBox.x,  hBox.y, -hBox.z)
        };
        const float3 edgeEnds[12] = {
            float3( hBox.x, -hBox.y, -hBox.z), float3( hBox.x,  hBox.y, -hBox.z), float3( hBox.x, -hBox.y,  hBox.z), float3( hBox.x,  hBox.y,  hBox.z),
            float3(-hBox.x,  hBox.y, -hBox.z), float3( hBox.x,  hBox.y, -hBox.z), float3(-hBox.x,  hBox.y,  hBox.z), float3( hBox.x,  hBox.y,  hBox.z),
            float3(-hBox.x, -hBox.y,  hBox.z), float3( hBox.x, -hBox.y,  hBox.z), float3(-hBox.x,  hBox.y,  hBox.z), float3( hBox.x,  hBox.y,  hBox.z)
        };
        for(uint ei = 0u; ei < 12u; ++ei){
            float es = 0.0f;
            float et = 0.0f;
            float3 cSeg;
            float3 cEdge;
            nwbPhysicsClosestPointsSegments(l0, l1, edgeStarts[ei], edgeEnds[ei], es, et, cSeg, cEdge);
            const float3 bp = clamp(cSeg, -hBox, hBox);
            const float3 d1 = cSeg - bp;
            const float dSq = dot(d1, d1);
            if(dSq < bestDistSq){
                bestDistSq = dSq;
                bestSegPt = cSeg;
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

        if(singleSep <= speculativeDist){
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
                        if(ptSep <= speculativeDist){
                            result.anchorA[emitted] = float4((capIsA ? capSurf : boxSurf) - bodyA.positionInvMass.xyz, 0.0f);
                            result.anchorBSeparation[emitted] = float4((capIsA ? boxSurf : capSurf) - bodyB.positionInvMass.xyz, ptSep);
                            if(emitted == 0u) result.featureIds.x = 6u;
                            else result.featureIds.y = 7u;
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
                result.anchorA[0] = float4((capIsA ? capSurf : boxSurf) - bodyA.positionInvMass.xyz, 0.0f);
                result.anchorBSeparation[0] = float4((capIsA ? boxSurf : capSurf) - bodyB.positionInvMass.xyz, singleSep);
                result.featureIds.x = 6u;
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

