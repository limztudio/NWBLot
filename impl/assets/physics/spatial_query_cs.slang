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

float nwbPhysicsShapePointDistSq(
    const float3 p,
    const NwbPhysicsGpuBodyPose pose,
    const NwbPhysicsGpuShapeProxy sp,
    const float3 aabbMin,
    const float3 aabbMax,
    out float3 outNormal
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

    if(shapeType == NWB_PHYSICS_SHAPE_TYPE_CAPSULE){
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

    if(shapeType == NWB_PHYSICS_SHAPE_TYPE_HULL && (shapeFlags & NWB_PHYSICS_SHAPE_FLAG_IS_BOX) != 0u){
        const float4 worldQ = (sp.localRotationOrMeta.w < 0.999999f)
            ? nwbPhysicsQuatNormalize(nwbPhysicsQuatMul(q, sp.localRotationOrMeta))
            : q;
        const float3 c = origin + nwbPhysicsQuatRotate(sp.localCenterRadius.xyz, q);
        const float3 h = sp.localExtentOrP1.xyz + float3(sp.localCenterRadius.w, sp.localCenterRadius.w, sp.localCenterRadius.w);
        const float3 localP = nwbPhysicsQuatInvRotate(p - c, worldQ);
        const float3 clamped = clamp(localP, -h, h);
        const float3 dLocal = localP - clamped;
        const float dSq = dot(dLocal, dLocal);
        outNormal = nwbPhysicsQuerySafeNormal(nwbPhysicsQuatRotate(dLocal, worldQ), float3(0.0f, 1.0f, 0.0f));
        return dSq;
    }

    const float3 clamped = clamp(p, aabbMin, aabbMax);
    const float3 d = p - clamped;
    outNormal = nwbPhysicsQuerySafeNormal(d, float3(0.0f, 1.0f, 0.0f));
    return dot(d, d);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void nwbPhysicsTestSpatialQueryShape(
    const uint shapeSlot,
    const uint bodyPoseSlot,
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
    inout bool hasHit
){
    const uint4 meta = g_NwbPhysicsShapeProxyBuffers[shapeSlot][shapeIdx].bodyIndexTypeShape;
    if((meta.w & NWB_PHYSICS_SHAPE_FLAG_SENSOR) != 0u && (qFlags & NWB_PHYSICS_QUERY_FLAG_INCLUDE_SENSORS) == 0u)
        return;
    const uint4 filter = g_NwbPhysicsShapeProxyBuffers[shapeSlot][shapeIdx].filterData;
    if((qCat & filter.y) == 0u || (filter.x & qMask) == 0u)
        return;

    if(queryType == NWB_PHYSICS_QUERY_TYPE_AABB_OVERLAP){
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

    const NwbPhysicsGpuBodyPose pose = g_NwbPhysicsBodyPoseBuffers[bodyPoseSlot][meta.x];
    const NwbPhysicsGpuShapeProxy spProxy = g_NwbPhysicsShapeProxyBuffers[shapeSlot][shapeIdx];
    if(queryType == NWB_PHYSICS_QUERY_TYPE_RAYCAST_CLOSEST){
        float tHit = 0.0f;
        float3 nHit = float3(0.0f, 1.0f, 0.0f);
        bool hitShape = false;
        const uint shapeType = meta.y;
        if(shapeType == NWB_PHYSICS_SHAPE_TYPE_SPHERE){
            const float3 c = pose.originPad.xyz + nwbPhysicsQuatRotate(spProxy.localCenterRadius.xyz, pose.rotation);
            hitShape = nwbPhysicsQueryRaySphere(rayOrig, rayDir, bestMetric, c, spProxy.localCenterRadius.w, tHit, nHit);
        }
        else if(shapeType == NWB_PHYSICS_SHAPE_TYPE_CAPSULE){
            const float3 p0 = pose.originPad.xyz + nwbPhysicsQuatRotate(spProxy.localCenterRadius.xyz, pose.rotation);
            const float3 p1 = pose.originPad.xyz + nwbPhysicsQuatRotate(spProxy.localExtentOrP1.xyz, pose.rotation);
            hitShape = nwbPhysicsQueryRayCapsule(rayOrig, rayDir, bestMetric, p0, p1, spProxy.localCenterRadius.w, tHit, nHit);
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
            const float3 h = spProxy.localExtentOrP1.xyz + float3(spProxy.localCenterRadius.w, spProxy.localCenterRadius.w, spProxy.localCenterRadius.w);
            float3 localN = float3(0.0f, 1.0f, 0.0f);
            hitShape = nwbPhysicsQueryRayAabb(localOrig, localInvDir, -h, h, bestMetric, tHit, localN);
            if(hitShape)
                nHit = nwbPhysicsQuerySafeNormal(nwbPhysicsQuatRotate(localN, worldQ), float3(0.0f, 1.0f, 0.0f));
        }
        else{
            hitShape = nwbPhysicsQueryRayAabb(rayOrig, rayInvDir, aMin, aMax, bestMetric, tHit, nHit);
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
    const float dSq = nwbPhysicsShapePointDistSq(sphereCenter, pose, spProxy, aMin, aMax, nOut);
    if(dSq <= sphereRadius * sphereRadius){
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

    const NwbPhysicsGpuSpatialQuery q = g_NwbPhysicsSpatialQueryBuffers[querySlot][tid];
    const uint queryType = q.params.x;
    const uint qCat = q.params.y;
    const uint qMask = q.params.z;
    const uint qFlags = q.params.w;

    const float3 rayOrig = q.originMin.xyz;
    const float3 rayDir = q.dirMax.xyz;
    const float3 rayInvDir = (queryType == NWB_PHYSICS_QUERY_TYPE_RAYCAST_CLOSEST)
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
    if(queryType == NWB_PHYSICS_QUERY_TYPE_RAYCAST_CLOSEST){
        const float3 rayEnd = rayOrig + rayDir * maxFrac;
        qBoxMin = min(rayOrig, rayEnd);
        qBoxMax = max(rayOrig, rayEnd);
    }
    else if(queryType == NWB_PHYSICS_QUERY_TYPE_SPHERE_OVERLAP){
        qBoxMin = sphereCenter - float3(sphereRadius, sphereRadius, sphereRadius);
        qBoxMax = sphereCenter + float3(sphereRadius, sphereRadius, sphereRadius);
    }
    const float3 qCenter = (qBoxMin + qBoxMax) * 0.5f;

    float bestMetric = (queryType == NWB_PHYSICS_QUERY_TYPE_RAYCAST_CLOSEST) ? maxFrac : 1.0e30f;
    float3 bestPoint = float3(0.0f, 0.0f, 0.0f);
    float3 bestNormal = float3(0.0f, 1.0f, 0.0f);
    uint bestShapeId = NWB_PHYSICS_INVALID_INDEX;
    uint bestBodyIdx = NWB_PHYSICS_INVALID_INDEX;
    uint hitCount = 0u;
    bool hasHit = false;

    const bool traverseLbvh = useLbvh && (lbvhNodeSlot != NWB_PHYSICS_INVALID_INDEX);
    const uint leafStart = traverseLbvh ? (shapeCount - 1u) : 0u;
    if(shapeCount > 0u){
        if(traverseLbvh){
            uint stack[32];
            float stackT[32];
            stack[0] = 0u;
            stackT[0] = 0.0f;
            uint sp = 0u;
            const NwbPhysicsGpuLbvhNode root = g_NwbPhysicsLbvhNodeReadBuffers[lbvhNodeSlot][0u];
            bool rootHit = nwbPhysicsQueryAabbOverlaps(qBoxMin, qBoxMax, root.minLeft.xyz, root.maxRight.xyz);
            float rootT = 0.0f;
            if(rootHit && queryType == NWB_PHYSICS_QUERY_TYPE_RAYCAST_CLOSEST){
                float3 nNode = float3(0.0f, 1.0f, 0.0f);
                rootHit = nwbPhysicsQueryRayAabb(rayOrig, rayInvDir, root.minLeft.xyz, root.maxRight.xyz, bestMetric, rootT, nNode);
            }
            if(rootHit){
                stack[sp] = 0u;
                stackT[sp] = rootT;
                ++sp;
            }

            while(sp > 0u){
                --sp;
                const uint nodeIdx = stack[sp];
                if(queryType == NWB_PHYSICS_QUERY_TYPE_RAYCAST_CLOSEST && stackT[sp] > bestMetric)
                    continue;
                const NwbPhysicsGpuLbvhNode node = g_NwbPhysicsLbvhNodeReadBuffers[lbvhNodeSlot][nodeIdx];
                const uint lIdx = asuint(node.minLeft.w);
                const uint rIdx = asuint(node.maxRight.w);
                const NwbPhysicsGpuLbvhNode lNode = g_NwbPhysicsLbvhNodeReadBuffers[lbvhNodeSlot][lIdx];
                const NwbPhysicsGpuLbvhNode rNode = g_NwbPhysicsLbvhNodeReadBuffers[lbvhNodeSlot][rIdx];
                bool hitL = nwbPhysicsQueryAabbOverlaps(qBoxMin, qBoxMax, lNode.minLeft.xyz, lNode.maxRight.xyz);
                bool hitR = nwbPhysicsQueryAabbOverlaps(qBoxMin, qBoxMax, rNode.minLeft.xyz, rNode.maxRight.xyz);
                float tL = 0.0f;
                float tR = 0.0f;
                if(hitL && queryType == NWB_PHYSICS_QUERY_TYPE_RAYCAST_CLOSEST){
                    float3 nL = float3(0.0f, 1.0f, 0.0f);
                    hitL = nwbPhysicsQueryRayAabb(rayOrig, rayInvDir, lNode.minLeft.xyz, lNode.maxRight.xyz, bestMetric, tL, nL);
                }
                if(hitR && queryType == NWB_PHYSICS_QUERY_TYPE_RAYCAST_CLOSEST){
                    float3 nR = float3(0.0f, 1.0f, 0.0f);
                    hitR = nwbPhysicsQueryRayAabb(rayOrig, rayInvDir, rNode.minLeft.xyz, rNode.maxRight.xyz, bestMetric, tR, nR);
                }
                if(hitL && lIdx >= leafStart){
                    nwbPhysicsTestSpatialQueryShape(shapeSlot, bodyPoseSlot, queryType, qCat, qMask, qFlags, rayOrig, rayDir, rayInvDir, sphereCenter, sphereRadius, qCenter, asuint(lNode.minLeft.w), lNode.minLeft.xyz, lNode.maxRight.xyz, bestMetric, bestPoint, bestNormal, bestShapeId, bestBodyIdx, hitCount, hasHit);
                    hitL = false;
                }
                if(hitR && rIdx >= leafStart){
                    nwbPhysicsTestSpatialQueryShape(shapeSlot, bodyPoseSlot, queryType, qCat, qMask, qFlags, rayOrig, rayDir, rayInvDir, sphereCenter, sphereRadius, qCenter, asuint(rNode.minLeft.w), rNode.minLeft.xyz, rNode.maxRight.xyz, bestMetric, bestPoint, bestNormal, bestShapeId, bestBodyIdx, hitCount, hasHit);
                    hitR = false;
                }
                if(hitL && hitR){
                    if(queryType == NWB_PHYSICS_QUERY_TYPE_RAYCAST_CLOSEST && tL < tR){
                        if(sp < 32u){
                            stack[sp] = rIdx;
                            stackT[sp] = tR;
                            ++sp;
                        }
                        if(sp < 32u){
                            stack[sp] = lIdx;
                            stackT[sp] = tL;
                            ++sp;
                        }
                    }
                    else{
                        if(sp < 32u){
                            stack[sp] = lIdx;
                            stackT[sp] = tL;
                            ++sp;
                        }
                        if(sp < 32u){
                            stack[sp] = rIdx;
                            stackT[sp] = tR;
                            ++sp;
                        }
                    }
                }
                else if(hitL){
                    if(sp < 32u){
                        stack[sp] = lIdx;
                        stackT[sp] = tL;
                        ++sp;
                    }
                }
                else if(hitR){
                    if(sp < 32u){
                        stack[sp] = rIdx;
                        stackT[sp] = tR;
                        ++sp;
                    }
                }
            }
        }
        else{
            for(uint shapeIdx = 0u; shapeIdx < shapeCount; ++shapeIdx){
                const float3 aMin = g_NwbPhysicsFloat4Buffers[shapeAabbSlot][shapeIdx * 2u + 0u].xyz;
                const float3 aMax = g_NwbPhysicsFloat4Buffers[shapeAabbSlot][shapeIdx * 2u + 1u].xyz;
                if(!nwbPhysicsQueryAabbOverlaps(qBoxMin, qBoxMax, aMin, aMax))
                    continue;
                if(queryType == NWB_PHYSICS_QUERY_TYPE_RAYCAST_CLOSEST){
                    float tAabb = 0.0f;
                    float3 nAabb = float3(0.0f, 1.0f, 0.0f);
                    if(!nwbPhysicsQueryRayAabb(rayOrig, rayInvDir, aMin, aMax, bestMetric, tAabb, nAabb))
                        continue;
                }
                nwbPhysicsTestSpatialQueryShape(shapeSlot, bodyPoseSlot, queryType, qCat, qMask, qFlags, rayOrig, rayDir, rayInvDir, sphereCenter, sphereRadius, qCenter, shapeIdx, aMin, aMax, bestMetric, bestPoint, bestNormal, bestShapeId, bestBodyIdx, hitCount, hasHit);
            }
        }
    }

    NwbPhysicsGpuSpatialQueryResult outRes;
    if(hasHit){
        if(queryType == NWB_PHYSICS_QUERY_TYPE_RAYCAST_CLOSEST){
            outRes.hitPointFraction = float4(bestPoint, bestMetric);
            outRes.hitNormalDist = float4(bestNormal, length(bestPoint - rayOrig));
        }
        else{
            outRes.hitPointFraction = float4(bestPoint, 0.0f);
            outRes.hitNormalDist = float4(bestNormal, sqrt(max(bestMetric, 0.0f)));
        }
        outRes.hitIds = uint4(bestShapeId, bestBodyIdx, hitCount, 1u);
    }
    else{
        outRes.hitPointFraction = float4(0.0f, 0.0f, 0.0f, 0.0f);
        outRes.hitNormalDist = float4(0.0f, 1.0f, 0.0f, 0.0f);
        outRes.hitIds = uint4(NWB_PHYSICS_INVALID_INDEX, NWB_PHYSICS_INVALID_INDEX, 0u, 0u);
    }
    g_NwbPhysicsSpatialQueryResultBuffers[resultSlot][tid] = outRes;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

