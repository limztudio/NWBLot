// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "physics_common.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


uint nwbPhysicsClz32(const uint x){
    if(x == 0u)
        return 32u;
    return 31u - firstbithigh(x);
}

uint nwbPhysicsExpandBits10(uint v){
    v = (v | (v << 16u)) & 0x030000ffu;
    v = (v | (v << 8u)) & 0x0300f00fu;
    v = (v | (v << 4u)) & 0x030c30c3u;
    v = (v | (v << 2u)) & 0x09249249u;
    return v;
}

uint nwbPhysicsMorton3D(const float3 p){
    const float3 q = clamp(p, float3(0.0f, 0.0f, 0.0f), float3(1.0f, 1.0f, 1.0f));
    const uint ux = uint(min(q.x * 1024.0f, 1023.0f));
    const uint uy = uint(min(q.y * 1024.0f, 1023.0f));
    const uint uz = uint(min(q.z * 1024.0f, 1023.0f));
    return (nwbPhysicsExpandBits10(ux) << 2u) | (nwbPhysicsExpandBits10(uy) << 1u) | nwbPhysicsExpandBits10(uz);
}

int nwbPhysicsLbvhClzDelta(const int i, const int j, const uint n, const uint mortonSlot){
    if(j < 0 || j >= int(n))
        return -1;
    const uint ci = g_NwbPhysicsUintBuffers[mortonSlot][uint(i) * 2u + 0u];
    const uint cj = g_NwbPhysicsUintBuffers[mortonSlot][uint(j) * 2u + 0u];
    if(ci == cj)
        return int(32u + nwbPhysicsClz32(uint(i) ^ uint(j)));
    return int(nwbPhysicsClz32(ci ^ cj));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool nwbPhysicsShouldCollideFields(const uint4 metaA, const uint4 filterA, const uint4 metaB, const uint4 filterB){
    const uint bodyIdxA = metaA.x;
    const uint bodyIdxB = metaB.x;
    if(bodyIdxA == bodyIdxB)
        return false;

    const uint flagsUnion = metaA.w | metaB.w;
    const bool anyAwake = (flagsUnion & NWB_PHYSICS_SHAPE_FLAG_AWAKE_BODY) != 0u;
    if(!anyAwake)
        return false;

    const bool anyDynamic = (flagsUnion & NWB_PHYSICS_SHAPE_FLAG_DYNAMIC_BODY) != 0u;
    const bool anySensor = (flagsUnion & NWB_PHYSICS_SHAPE_FLAG_SENSOR) != 0u;
    const bool anyKinematic = (flagsUnion & NWB_PHYSICS_SHAPE_FLAG_KINEMATIC_BODY) != 0u;
    if(!anyDynamic && !(anySensor && anyKinematic))
        return false;

    const int groupA = asint(filterA.z);
    const int groupB = asint(filterB.z);
    if(groupA == groupB && groupA != 0)
        return groupA > 0;

    return ((filterA.x & filterB.y) != 0u) && ((filterB.x & filterA.y) != 0u);
}

bool nwbPhysicsAabbOverlaps(const float3 minA, const float3 maxA, const float3 minB, const float3 maxB){
    return all(minA <= maxB) && all(minB <= maxA);
}

bool nwbPhysicsIsComplexPair(const uint4 metaA, const uint4 metaB){
    const uint typeA = metaA.y;
    const uint typeB = metaB.y;
    const bool isBoxA = (typeA == NWB_PHYSICS_SHAPE_TYPE_HULL) && ((metaA.w & NWB_PHYSICS_SHAPE_FLAG_IS_BOX) != 0u);
    const bool isBoxB = (typeB == NWB_PHYSICS_SHAPE_TYPE_HULL) && ((metaB.w & NWB_PHYSICS_SHAPE_FLAG_IS_BOX) != 0u);
    return (typeA == NWB_PHYSICS_SHAPE_TYPE_MESH)
        || (typeB == NWB_PHYSICS_SHAPE_TYPE_MESH)
        || (typeA == NWB_PHYSICS_SHAPE_TYPE_HULL && !isBoxA)
        || (typeB == NWB_PHYSICS_SHAPE_TYPE_HULL && !isBoxB);
}

void nwbPhysicsEmitBroadphasePair(
    const uint pairSlot,
    const uint counterSlot,
    const uint maxPairs,
    const uint idxA,
    const uint idxB,
    const uint bodyA,
    const uint bodyB,
    const bool isComplex
){
    const uint primOffset = WavePrefixCountBits(!isComplex);
    const uint primCount = WaveActiveCountBits(!isComplex);
    const uint cpxOffset = WavePrefixCountBits(isComplex);
    const uint cpxCount = WaveActiveCountBits(isComplex);
    uint primBase = 0u;
    uint cpxBase = 0u;
    if(WaveIsFirstLane()){
        if(primCount > 0u)
            InterlockedAdd(g_NwbPhysicsUintBuffers[counterSlot][0u], primCount, primBase);
        if(cpxCount > 0u)
            InterlockedAdd(g_NwbPhysicsUintBuffers[counterSlot][1u], cpxCount, cpxBase);
    }
    primBase = WaveReadLaneFirst(primBase);
    cpxBase = WaveReadLaneFirst(cpxBase);
    const uint writeIndex = isComplex ? (cpxBase + cpxOffset) : (primBase + primOffset);
    if(writeIndex < maxPairs){
        NwbPhysicsGpuBroadphasePair pairOut;
        pairOut.indices = uint4(idxA, idxB, bodyA, bodyB);
        const uint dstIdx = isComplex ? (maxPairs - 1u - writeIndex) : writeIndex;
        g_NwbPhysicsBroadphasePairBuffers[pairSlot][dstIdx] = pairOut;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


groupshared uint s_LbvhSortMorton[NWB_PHYSICS_LBVH_LOCAL_SORT_SIZE];
groupshared uint s_LbvhSortShape[NWB_PHYSICS_LBVH_LOCAL_SORT_SIZE];

void nwbPhysicsLbvhLocalBitonicStep(
    const uint localTid,
    const uint blockBase,
    const uint halfLocal,
    const uint k,
    const uint j
){
    for(uint p = localTid; p < halfLocal; p += NWB_PHYSICS_WORKGROUP_SIZE){
        const uint block = p / j;
        const uint rem = p - block * j;
        const uint i0 = block * (2u * j) + rem;
        const uint i1 = i0 + j;
        const bool ascending = (((blockBase + i0) & k) == 0u);
        const uint m0 = s_LbvhSortMorton[i0];
        const uint s0 = s_LbvhSortShape[i0];
        const uint m1 = s_LbvhSortMorton[i1];
        const uint s1 = s_LbvhSortShape[i1];
        const bool greater = (m0 > m1) || (m0 == m1 && s0 > s1);
        if(greater == ascending){
            s_LbvhSortMorton[i0] = m1;
            s_LbvhSortShape[i0] = s1;
            s_LbvhSortMorton[i1] = m0;
            s_LbvhSortShape[i1] = s0;
        }
    }
    GroupMemoryBarrierWithGroupSync();
}

void nwbPhysicsLbvhTestLeaf(
    const uint idxA,
    const uint idxB,
    const uint itemOffset,
    const uint4 metaA,
    const uint4 filterA,
    const uint shapeSlot,
    const uint pairSlot,
    const uint counterSlot,
    const uint maxPairs,
    const uint nonCollideSlot,
    const uint nonCollideMask
){
    if(idxB == idxA)
        return;
    if(idxB >= itemOffset && idxB <= idxA)
        return;
    const uint4 metaB = g_NwbPhysicsShapeProxyBuffers[shapeSlot][idxB].bodyIndexTypeShape;
    const uint4 filterB = g_NwbPhysicsShapeProxyBuffers[shapeSlot][idxB].filterData;
    if(!nwbPhysicsShouldCollideFields(metaA, filterA, metaB, filterB))
        return;
    if(((metaA.w & metaB.w) & NWB_PHYSICS_SHAPE_FLAG_HAS_NONCOLLIDE_JOINT) != 0u && ((metaA.w | metaB.w) & NWB_PHYSICS_SHAPE_FLAG_SENSOR) == 0u && nwbPhysicsIsNonCollidingBodyPair(nonCollideSlot, nonCollideMask, metaA.x, metaB.x))
        return;

    const uint outA = idxA < idxB ? idxA : idxB;
    const uint outB = idxA < idxB ? idxB : idxA;
    const uint bodyOutA = idxA < idxB ? metaA.x : metaB.x;
    const uint bodyOutB = idxA < idxB ? metaB.x : metaA.x;
    nwbPhysicsEmitBroadphasePair(pairSlot, counterSlot, maxPairs, outA, outB, bodyOutA, bodyOutB, nwbPhysicsIsComplexPair(metaA, metaB));
}

[require(spvGroupNonUniform, spvGroupNonUniformBallot)]
[numthreads(NWB_PHYSICS_WORKGROUP_SIZE, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID, uint3 groupID : SV_GroupID, uint3 groupThreadID : SV_GroupThreadID){
    const uint tid = dispatchThreadID.x;
    const uint mode = g_NwbPhysicsPushConstants.modeOrSecondaryCount;
    const uint itemCount = g_NwbPhysicsPushConstants.itemCount;
    const uint itemOffset = g_NwbPhysicsPushConstants.itemOffset;

    const uint shapeAabbSlot = g_NwbPhysicsPushConstants.slots0.x;
    const uint shapeSlot = g_NwbPhysicsPushConstants.slots0.y;
    const uint mortonSlot = g_NwbPhysicsPushConstants.slots0.z;
    const uint lbvhNodeSlot = g_NwbPhysicsPushConstants.slots0.w;

    const uint pairSlot = g_NwbPhysicsPushConstants.slots1.x;
    const uint counterSlot = g_NwbPhysicsPushConstants.slots1.y;
    const uint nonCollideSlot = g_NwbPhysicsPushConstants.slots1.z;
    const uint nonCollideMask = g_NwbPhysicsPushConstants.slots1.w;

    if(mode == NWB_PHYSICS_LBVH_MODE_COMPUTE_MORTON){
        if(tid >= itemCount)
            return;

        const uint shapeCount = itemOffset;
        if(tid < shapeCount){
            const float3 aMin = g_NwbPhysicsFloat4Buffers[shapeAabbSlot][tid * 2u + 0u].xyz;
            const float3 aMax = g_NwbPhysicsFloat4Buffers[shapeAabbSlot][tid * 2u + 1u].xyz;
            const float3 center = (aMin + aMax) * 0.5f;
            const float3 sceneMin = float3(
                g_NwbPhysicsPushConstants.deltaTime,
                g_NwbPhysicsPushConstants.invDeltaTime,
                g_NwbPhysicsPushConstants.maxLinearVelocity
            );
            const float3 invExtent = g_NwbPhysicsPushConstants.gravitySpeculative.xyz;
            const uint code = nwbPhysicsMorton3D((center - sceneMin) * invExtent);
            g_NwbPhysicsUintBuffers[mortonSlot][tid * 2u + 0u] = code;
            g_NwbPhysicsUintBuffers[mortonSlot][tid * 2u + 1u] = tid;

            if(shapeCount >= 2u){
                const uint leafIdx = (shapeCount - 1u) + tid;
                g_NwbPhysicsLbvhNodeBuffers[lbvhNodeSlot][leafIdx].parentMeta = uint4(NWB_PHYSICS_INVALID_INDEX, 0u, 0u, 0u);
                if(tid + 1u < shapeCount)
                    g_NwbPhysicsLbvhNodeBuffers[lbvhNodeSlot][tid].parentMeta = uint4(NWB_PHYSICS_INVALID_INDEX, 0u, 0u, 0u);
            }
        }
        else{
            g_NwbPhysicsUintBuffers[mortonSlot][tid * 2u + 0u] = 0xffffffffu;
            g_NwbPhysicsUintBuffers[mortonSlot][tid * 2u + 1u] = NWB_PHYSICS_INVALID_INDEX;
        }
        return;
    }

    if(mode == NWB_PHYSICS_LBVH_MODE_BITONIC_SORT_LOCAL){
        const uint blockBase = groupID.x * NWB_PHYSICS_LBVH_LOCAL_SORT_SIZE;
        if(blockBase >= itemCount)
            return;
        const uint localN = min(itemCount - blockBase, NWB_PHYSICS_LBVH_LOCAL_SORT_SIZE);
        for(uint i = groupThreadID.x; i < localN; i += NWB_PHYSICS_WORKGROUP_SIZE){
            s_LbvhSortMorton[i] = g_NwbPhysicsUintBuffers[mortonSlot][(blockBase + i) * 2u + 0u];
            s_LbvhSortShape[i] = g_NwbPhysicsUintBuffers[mortonSlot][(blockBase + i) * 2u + 1u];
        }
        GroupMemoryBarrierWithGroupSync();

        const uint halfLocal = localN >> 1u;
        for(uint k = 2u; k <= localN; k <<= 1u){
            for(uint j = k >> 1u; j >= 1u; j >>= 1u)
                nwbPhysicsLbvhLocalBitonicStep(groupThreadID.x, blockBase, halfLocal, k, j);
        }

        for(uint i = groupThreadID.x; i < localN; i += NWB_PHYSICS_WORKGROUP_SIZE){
            g_NwbPhysicsUintBuffers[mortonSlot][(blockBase + i) * 2u + 0u] = s_LbvhSortMorton[i];
            g_NwbPhysicsUintBuffers[mortonSlot][(blockBase + i) * 2u + 1u] = s_LbvhSortShape[i];
        }
        return;
    }

    if(mode == NWB_PHYSICS_LBVH_MODE_BITONIC_MERGE_LOCAL){
        const uint blockBase = groupID.x * NWB_PHYSICS_LBVH_LOCAL_SORT_SIZE;
        if(blockBase >= itemCount)
            return;
        const uint localN = min(itemCount - blockBase, NWB_PHYSICS_LBVH_LOCAL_SORT_SIZE);
        const uint k = g_NwbPhysicsPushConstants.flags;
        for(uint i = groupThreadID.x; i < localN; i += NWB_PHYSICS_WORKGROUP_SIZE){
            s_LbvhSortMorton[i] = g_NwbPhysicsUintBuffers[mortonSlot][(blockBase + i) * 2u + 0u];
            s_LbvhSortShape[i] = g_NwbPhysicsUintBuffers[mortonSlot][(blockBase + i) * 2u + 1u];
        }
        GroupMemoryBarrierWithGroupSync();

        const uint halfLocal = localN >> 1u;
        for(uint j = localN >> 1u; j >= 1u; j >>= 1u)
            nwbPhysicsLbvhLocalBitonicStep(groupThreadID.x, blockBase, halfLocal, k, j);

        for(uint i = groupThreadID.x; i < localN; i += NWB_PHYSICS_WORKGROUP_SIZE){
            g_NwbPhysicsUintBuffers[mortonSlot][(blockBase + i) * 2u + 0u] = s_LbvhSortMorton[i];
            g_NwbPhysicsUintBuffers[mortonSlot][(blockBase + i) * 2u + 1u] = s_LbvhSortShape[i];
        }
        return;
    }

    if(mode == NWB_PHYSICS_LBVH_MODE_BITONIC_SORT){
        if(tid >= itemCount)
            return;

        const uint j = itemOffset;
        const uint k = g_NwbPhysicsPushConstants.flags;
        const uint block = tid / j;
        const uint rem = tid - block * j;
        const uint idx0 = block * (2u * j) + rem;
        const uint idx1 = idx0 + j;
        const bool ascending = ((idx0 & k) == 0u);

        const uint m0 = g_NwbPhysicsUintBuffers[mortonSlot][idx0 * 2u + 0u];
        const uint s0 = g_NwbPhysicsUintBuffers[mortonSlot][idx0 * 2u + 1u];
        const uint m1 = g_NwbPhysicsUintBuffers[mortonSlot][idx1 * 2u + 0u];
        const uint s1 = g_NwbPhysicsUintBuffers[mortonSlot][idx1 * 2u + 1u];

        const bool greater = (m0 > m1) || (m0 == m1 && s0 > s1);
        if(greater == ascending){
            g_NwbPhysicsUintBuffers[mortonSlot][idx0 * 2u + 0u] = m1;
            g_NwbPhysicsUintBuffers[mortonSlot][idx0 * 2u + 1u] = s1;
            g_NwbPhysicsUintBuffers[mortonSlot][idx1 * 2u + 0u] = m0;
            g_NwbPhysicsUintBuffers[mortonSlot][idx1 * 2u + 1u] = s0;
        }
        return;
    }

    if(mode == NWB_PHYSICS_LBVH_MODE_BUILD_TREE){
        if(tid >= itemCount || itemCount < 2u)
            return;

        const uint shapeIdx = g_NwbPhysicsUintBuffers[mortonSlot][tid * 2u + 1u];
        const uint leafIdx = (itemCount - 1u) + tid;
        if(shapeIdx != NWB_PHYSICS_INVALID_INDEX){
            const float3 aMin = g_NwbPhysicsFloat4Buffers[shapeAabbSlot][shapeIdx * 2u + 0u].xyz;
            const float3 aMax = g_NwbPhysicsFloat4Buffers[shapeAabbSlot][shapeIdx * 2u + 1u].xyz;
            g_NwbPhysicsLbvhNodeBuffers[lbvhNodeSlot][leafIdx].minLeft = float4(aMin, asfloat(shapeIdx));
            g_NwbPhysicsLbvhNodeBuffers[lbvhNodeSlot][leafIdx].maxRight = float4(aMax, asfloat(shapeIdx));
        }

        if(tid + 1u >= itemCount)
            return;

        const int i = int(tid);
        const int d = (nwbPhysicsLbvhClzDelta(i, i + 1, itemCount, mortonSlot) - nwbPhysicsLbvhClzDelta(i, i - 1, itemCount, mortonSlot) >= 0) ? 1 : -1;
        const int deltaMin = nwbPhysicsLbvhClzDelta(i, i - d, itemCount, mortonSlot);
        int lMax = 2;
        while(nwbPhysicsLbvhClzDelta(i, i + lMax * d, itemCount, mortonSlot) > deltaMin)
            lMax *= 2;

        int l = 0;
        for(int step = lMax / 2; step >= 1; step /= 2){
            if(nwbPhysicsLbvhClzDelta(i, i + (l + step) * d, itemCount, mortonSlot) > deltaMin)
                l += step;
        }
        const int j = i + l * d;
        const int deltaNode = nwbPhysicsLbvhClzDelta(i, j, itemCount, mortonSlot);

        int s = 0;
        int t = (l + 1) / 2;
        while(t >= 1){
            if((s + t) < l && nwbPhysicsLbvhClzDelta(i, i + (s + t) * d, itemCount, mortonSlot) > deltaNode)
                s += t;
            if(t == 1)
                break;
            t = (t + 1) / 2;
        }
        const int gamma = i + s * d + min(d, 0);
        const int first = min(i, j);
        const int last = max(i, j);
        const uint leafOffset = itemCount - 1u;
        const uint leftChild = (first == gamma) ? (leafOffset + uint(gamma)) : uint(gamma);
        const uint rightChild = (last == (gamma + 1)) ? (leafOffset + uint(gamma + 1)) : uint(gamma + 1);

        g_NwbPhysicsLbvhNodeBuffers[lbvhNodeSlot][tid].minLeft = float4(0.0f, 0.0f, 0.0f, asfloat(leftChild));
        g_NwbPhysicsLbvhNodeBuffers[lbvhNodeSlot][tid].maxRight = float4(0.0f, 0.0f, 0.0f, asfloat(rightChild));
        g_NwbPhysicsLbvhNodeBuffers[lbvhNodeSlot][leftChild].parentMeta.x = tid;
        g_NwbPhysicsLbvhNodeBuffers[lbvhNodeSlot][rightChild].parentMeta.x = tid;
        return;
    }

    if(mode == NWB_PHYSICS_LBVH_MODE_REFIT_AABBS){
        if(tid >= itemCount || itemCount < 2u)
            return;

        uint cur = (itemCount - 1u) + tid;
        uint parent = g_NwbPhysicsLbvhNodeBuffers[lbvhNodeSlot][cur].parentMeta.x;
        bool firstLevel = true;
        while(parent != NWB_PHYSICS_INVALID_INDEX){
            if(!firstLevel)
                DeviceMemoryBarrier();
            firstLevel = false;
            uint prevVisits = 0u;
            InterlockedAdd(g_NwbPhysicsLbvhNodeBuffers[lbvhNodeSlot][parent].parentMeta.z, 1u, prevVisits);
            if(prevVisits == 0u)
                break;
            DeviceMemoryBarrier();

            const NwbPhysicsGpuLbvhNode pNode = g_NwbPhysicsLbvhNodeBuffers[lbvhNodeSlot][parent];
            const uint leftChild = asuint(pNode.minLeft.w);
            const uint rightChild = asuint(pNode.maxRight.w);
            const NwbPhysicsGpuLbvhNode lNode = g_NwbPhysicsLbvhNodeBuffers[lbvhNodeSlot][leftChild];
            const NwbPhysicsGpuLbvhNode rNode = g_NwbPhysicsLbvhNodeBuffers[lbvhNodeSlot][rightChild];

            const float3 uMin = min(lNode.minLeft.xyz, rNode.minLeft.xyz);
            const float3 uMax = max(lNode.maxRight.xyz, rNode.maxRight.xyz);
            g_NwbPhysicsLbvhNodeBuffers[lbvhNodeSlot][parent].minLeft = float4(uMin, asfloat(leftChild));
            g_NwbPhysicsLbvhNodeBuffers[lbvhNodeSlot][parent].maxRight = float4(uMax, asfloat(rightChild));

            cur = parent;
            parent = pNode.parentMeta.x;
        }
        return;
    }

    if(mode == NWB_PHYSICS_LBVH_MODE_QUERY_PAIRS){
        const uint totalShapes = asuint(g_NwbPhysicsPushConstants.gravitySpeculative.x);
        const uint maxPairs = g_NwbPhysicsPushConstants.flags;
        const bool active = (tid < itemCount) && (totalShapes >= 2u);

        uint idxA = 0u;
        float3 minA = float3(0.0f, 0.0f, 0.0f);
        float3 maxA = float3(0.0f, 0.0f, 0.0f);
        uint4 metaA = uint4(0u, 0u, 0u, 0u);
        uint4 filterA = uint4(0u, 0u, 0u, 0u);

        uint stack[32];
        stack[0] = 0u;
        uint sp = 0u;
        if(active){
            idxA = itemOffset + tid;
            minA = g_NwbPhysicsFloat4Buffers[shapeAabbSlot][idxA * 2u + 0u].xyz;
            maxA = g_NwbPhysicsFloat4Buffers[shapeAabbSlot][idxA * 2u + 1u].xyz;
            metaA = g_NwbPhysicsShapeProxyBuffers[shapeSlot][idxA].bodyIndexTypeShape;
            filterA = g_NwbPhysicsShapeProxyBuffers[shapeSlot][idxA].filterData;

            const NwbPhysicsGpuLbvhNode root = g_NwbPhysicsLbvhNodeReadBuffers[lbvhNodeSlot][0u];
            if(nwbPhysicsAabbOverlaps(minA, maxA, root.minLeft.xyz, root.maxRight.xyz))
                stack[sp++] = 0u;
        }

        const uint leafStart = totalShapes - 1u;
        bool stackOverflow = false;
        while(sp > 0u){
            const uint nodeIdx = stack[--sp];
            const NwbPhysicsGpuLbvhNode node = g_NwbPhysicsLbvhNodeReadBuffers[lbvhNodeSlot][nodeIdx];
            const uint leftIdx = asuint(node.minLeft.w);
            const uint rightIdx = asuint(node.maxRight.w);
            const NwbPhysicsGpuLbvhNode lNode = g_NwbPhysicsLbvhNodeReadBuffers[lbvhNodeSlot][leftIdx];
            const NwbPhysicsGpuLbvhNode rNode = g_NwbPhysicsLbvhNodeReadBuffers[lbvhNodeSlot][rightIdx];
            const bool hitL = nwbPhysicsAabbOverlaps(minA, maxA, lNode.minLeft.xyz, lNode.maxRight.xyz);
            const bool hitR = nwbPhysicsAabbOverlaps(minA, maxA, rNode.minLeft.xyz, rNode.maxRight.xyz);
            if(hitL){
                if(leftIdx >= leafStart){
                    nwbPhysicsLbvhTestLeaf(idxA, asuint(lNode.minLeft.w), itemOffset, metaA, filterA, shapeSlot, pairSlot, counterSlot, maxPairs, nonCollideSlot, nonCollideMask);
                }
                else if(sp < 32u){
                    stack[sp++] = leftIdx;
                }
                else{
                    stackOverflow = true;
                }
            }
            if(hitR){
                if(rightIdx >= leafStart){
                    nwbPhysicsLbvhTestLeaf(idxA, asuint(rNode.minLeft.w), itemOffset, metaA, filterA, shapeSlot, pairSlot, counterSlot, maxPairs, nonCollideSlot, nonCollideMask);
                }
                else if(sp < 32u){
                    stack[sp++] = rightIdx;
                }
                else{
                    stackOverflow = true;
                }
            }
            if(stackOverflow)
                break;
        }
        if(stackOverflow){
            uint prevC = 0u;
            InterlockedAdd(g_NwbPhysicsUintBuffers[counterSlot][0u], maxPairs + 1u, prevC);
        }
        return;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

