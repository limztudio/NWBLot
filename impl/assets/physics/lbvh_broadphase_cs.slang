// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "broadphase_helpers.slangi"


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

int nwbPhysicsLbvhClzDelta(const int i, const uint ci, const int j, const uint n, const uint mortonSlot){
    if(j < 0 || j >= int(n))
        return -1;
    const uint cj = g_NwbPhysicsUintBuffers[mortonSlot][uint(j) * 2u + 0u];
    if(ci == cj)
        return int(32u + nwbPhysicsClz32(uint(i) ^ uint(j)));
    return int(nwbPhysicsClz32(ci ^ cj));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


uint nwbPhysicsFloatToOrderedUint(const float f){
    const uint u = asuint(f);
    return ((u & 0x80000000u) != 0u) ? ~u : (u | 0x80000000u);
}

float nwbPhysicsOrderedUintToFloat(const uint o){
    const uint u = ((o & 0x80000000u) != 0u) ? (o & 0x7fffffffu) : ~o;
    return asfloat(u);
}

groupshared uint s_LbvhSortMorton[NWB_PHYSICS_LBVH_LOCAL_SORT_SIZE];
groupshared uint s_LbvhSortShape[NWB_PHYSICS_LBVH_LOCAL_SORT_SIZE];
groupshared uint s_RadixHist[256];
groupshared uint s_RadixLocalHist[1024];
groupshared uint s_RadixAux[256];
groupshared uint s_BoundsMin[3];
groupshared uint s_BoundsMax[3];

void nwbPhysicsLbvhLocalBitonicStep(
    const uint localTid,
    const uint blockBase,
    const uint halfLocal,
    const uint k,
    const uint j
){
    for(uint p = localTid; p < halfLocal; p += NWB_PHYSICS_WORKGROUP_SIZE){
        const uint mask = j - 1u;
        const uint i0 = ((p & ~mask) << 1u) | (p & mask);
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
    const uint surfacePairSlot,
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
    nwbPhysicsEmitBroadphasePair(pairSlot, surfacePairSlot, counterSlot, maxPairs, outA, outB, bodyOutA, bodyOutB, nwbPhysicsBroadphasePairBucket(metaA, metaB));
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
    const uint lbvhMetaSlot = g_NwbPhysicsPushConstants.slots1.x;
    const uint counterSlot = g_NwbPhysicsPushConstants.slots1.y;
    const uint nonCollideSlot = g_NwbPhysicsPushConstants.slots1.z;
    const uint nonCollideMask = g_NwbPhysicsPushConstants.slots1.w;

    if(mode == NWB_PHYSICS_LBVH_MODE_CLEAR_BOUNDS){
        if(tid == 0u){
            const uint infMin = nwbPhysicsFloatToOrderedUint(1.0e30f);
            const uint infMax = nwbPhysicsFloatToOrderedUint(-1.0e30f);
            g_NwbPhysicsUintBuffers[lbvhMetaSlot][0u] = infMin;
            g_NwbPhysicsUintBuffers[lbvhMetaSlot][1u] = infMin;
            g_NwbPhysicsUintBuffers[lbvhMetaSlot][2u] = infMin;
            g_NwbPhysicsUintBuffers[lbvhMetaSlot][3u] = infMax;
            g_NwbPhysicsUintBuffers[lbvhMetaSlot][4u] = infMax;
            g_NwbPhysicsUintBuffers[lbvhMetaSlot][5u] = infMax;
        }
        return;
    }

    if(mode == NWB_PHYSICS_LBVH_MODE_COMPUTE_BOUNDS){
        const uint localTid = groupThreadID.x;
        if(localTid < 3u){
            s_BoundsMin[localTid] = nwbPhysicsFloatToOrderedUint(1.0e30f);
            s_BoundsMax[localTid] = nwbPhysicsFloatToOrderedUint(-1.0e30f);
        }
        GroupMemoryBarrierWithGroupSync();

        if(tid < itemCount){
            const float3 aMin = g_NwbPhysicsFloat4Buffers[shapeAabbSlot][tid * 2u + 0u].xyz;
            const float3 aMax = g_NwbPhysicsFloat4Buffers[shapeAabbSlot][tid * 2u + 1u].xyz;
            const float3 center = (aMin + aMax) * 0.5f;
            if(all(isfinite(center)) && all(abs(center) < 1.0e18f) && all((aMax - aMin) < 1.9e4f)){
                InterlockedMin(s_BoundsMin[0u], nwbPhysicsFloatToOrderedUint(center.x));
                InterlockedMin(s_BoundsMin[1u], nwbPhysicsFloatToOrderedUint(center.y));
                InterlockedMin(s_BoundsMin[2u], nwbPhysicsFloatToOrderedUint(center.z));
                InterlockedMax(s_BoundsMax[0u], nwbPhysicsFloatToOrderedUint(center.x));
                InterlockedMax(s_BoundsMax[1u], nwbPhysicsFloatToOrderedUint(center.y));
                InterlockedMax(s_BoundsMax[2u], nwbPhysicsFloatToOrderedUint(center.z));
            }
        }
        GroupMemoryBarrierWithGroupSync();

        if(localTid < 3u){
            InterlockedMin(g_NwbPhysicsUintBuffers[lbvhMetaSlot][localTid], s_BoundsMin[localTid]);
            InterlockedMax(g_NwbPhysicsUintBuffers[lbvhMetaSlot][3u + localTid], s_BoundsMax[localTid]);
        }
        return;
    }

    if(mode == NWB_PHYSICS_LBVH_MODE_COMPUTE_MORTON){
        if(tid >= itemCount)
            return;

        const uint shapeCount = itemOffset;
        if(tid < shapeCount){
            const float3 aMin = g_NwbPhysicsFloat4Buffers[shapeAabbSlot][tid * 2u + 0u].xyz;
            const float3 aMax = g_NwbPhysicsFloat4Buffers[shapeAabbSlot][tid * 2u + 1u].xyz;
            const float3 center = (aMin + aMax) * 0.5f;
            float3 sceneMin = float3(
                g_NwbPhysicsPushConstants.deltaTime,
                g_NwbPhysicsPushConstants.invDeltaTime,
                g_NwbPhysicsPushConstants.maxLinearVelocity
            );
            float3 invExtent = g_NwbPhysicsPushConstants.gravitySpeculative.xyz;
            if((g_NwbPhysicsPushConstants.flags & 1u) != 0u){
                const float3 bMin = float3(
                    nwbPhysicsOrderedUintToFloat(g_NwbPhysicsUintBuffers[lbvhMetaSlot][0u]),
                    nwbPhysicsOrderedUintToFloat(g_NwbPhysicsUintBuffers[lbvhMetaSlot][1u]),
                    nwbPhysicsOrderedUintToFloat(g_NwbPhysicsUintBuffers[lbvhMetaSlot][2u])
                );
                const float3 bMax = float3(
                    nwbPhysicsOrderedUintToFloat(g_NwbPhysicsUintBuffers[lbvhMetaSlot][3u]),
                    nwbPhysicsOrderedUintToFloat(g_NwbPhysicsUintBuffers[lbvhMetaSlot][4u]),
                    nwbPhysicsOrderedUintToFloat(g_NwbPhysicsUintBuffers[lbvhMetaSlot][5u])
                );
                if(all(bMin <= bMax)){
                    const float3 span = max(bMax - bMin, float3(1.0f, 1.0f, 1.0f));
                    sceneMin = bMin - float3(1.0f, 1.0f, 1.0f);
                    invExtent = float3(1.0f / (span.x + 2.0f), 1.0f / (span.y + 2.0f), 1.0f / (span.z + 2.0f));
                }
                else{
                    sceneMin = float3(-1.0f, -1.0f, -1.0f);
                    invExtent = float3(0.5f, 0.5f, 0.5f);
                }
            }
            const uint code = nwbPhysicsMorton3D((center - sceneMin) * invExtent);
            g_NwbPhysicsUintBuffers[mortonSlot][tid * 2u + 0u] = code;
            g_NwbPhysicsUintBuffers[mortonSlot][tid * 2u + 1u] = tid;
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

    if(mode == NWB_PHYSICS_LBVH_MODE_RADIX_HISTOGRAM){
        const uint localTid = groupThreadID.x;
        const uint blockBase = groupID.x * NWB_PHYSICS_LBVH_RADIX_BLOCK_SIZE;
        if(blockBase >= itemCount)
            return;
        const uint shift = g_NwbPhysicsPushConstants.flags;
        const uint srcMortonSlot = g_NwbPhysicsPushConstants.slots0.x;
        const uint radixHistSlot = g_NwbPhysicsPushConstants.slots0.z;
        const uint localN = min(itemCount - blockBase, NWB_PHYSICS_LBVH_RADIX_BLOCK_SIZE);

        for(uint d = localTid; d < 256u; d += NWB_PHYSICS_WORKGROUP_SIZE)
            s_RadixHist[d] = 0u;
        GroupMemoryBarrierWithGroupSync();

        for(uint i = localTid; i < localN; i += NWB_PHYSICS_WORKGROUP_SIZE){
            const uint m = g_NwbPhysicsUintBuffers[srcMortonSlot][(blockBase + i) * 2u + 0u];
            const uint digit = (m >> shift) & 0xffu;
            InterlockedAdd(s_RadixHist[digit], 1u);
        }
        GroupMemoryBarrierWithGroupSync();

        const uint histBase = groupID.x * 256u;
        for(uint d = localTid; d < 256u; d += NWB_PHYSICS_WORKGROUP_SIZE)
            g_NwbPhysicsUintBuffers[radixHistSlot][histBase + d] = s_RadixHist[d];
        return;
    }

    if(mode == NWB_PHYSICS_LBVH_MODE_RADIX_SCAN){
        const uint localTid = groupThreadID.x;
        const uint numBlocks = itemOffset;
        const uint radixHistSlot = g_NwbPhysicsPushConstants.slots0.z;
        for(uint pass = 0u; pass < 4u; ++pass){
            const uint b = pass * 64u + localTid;
            uint running = 0u;
            for(uint g = 0u; g < numBlocks; ++g){
                const uint addr = g * 256u + b;
                const uint c = g_NwbPhysicsUintBuffers[radixHistSlot][addr];
                g_NwbPhysicsUintBuffers[radixHistSlot][addr] = running;
                running += c;
            }
            s_RadixHist[b] = running;
        }
        GroupMemoryBarrierWithGroupSync();

        const uint bBase = localTid * 4u;
        const uint v0 = s_RadixHist[bBase + 0u];
        const uint v1 = s_RadixHist[bBase + 1u];
        const uint v2 = s_RadixHist[bBase + 2u];
        const uint v3 = s_RadixHist[bBase + 3u];
        const uint subTot = v0 + v1 + v2 + v3;
        s_RadixAux[localTid] = subTot;
        GroupMemoryBarrierWithGroupSync();

        uint scanVal = subTot;
        [unroll]
        for(uint offset = 1u; offset < 64u; offset <<= 1u){
            const uint addVal = (localTid >= offset) ? s_RadixAux[localTid - offset] : 0u;
            GroupMemoryBarrierWithGroupSync();
            scanVal += addVal;
            s_RadixAux[localTid] = scanVal;
            GroupMemoryBarrierWithGroupSync();
        }

        const uint basePrefix = (localTid > 0u) ? s_RadixAux[localTid - 1u] : 0u;
        const uint outBase = numBlocks * 256u + bBase;
        g_NwbPhysicsUintBuffers[radixHistSlot][outBase + 0u] = basePrefix;
        g_NwbPhysicsUintBuffers[radixHistSlot][outBase + 1u] = basePrefix + v0;
        g_NwbPhysicsUintBuffers[radixHistSlot][outBase + 2u] = basePrefix + v0 + v1;
        g_NwbPhysicsUintBuffers[radixHistSlot][outBase + 3u] = basePrefix + v0 + v1 + v2;
        return;
    }

    if(mode == NWB_PHYSICS_LBVH_MODE_RADIX_SCATTER){
        const uint localTid = groupThreadID.x;
        const uint blockBase = groupID.x * NWB_PHYSICS_LBVH_RADIX_BLOCK_SIZE;
        if(blockBase >= itemCount)
            return;
        const uint numBlocks = itemOffset;
        const uint shift = g_NwbPhysicsPushConstants.flags;
        const uint srcMortonSlot = g_NwbPhysicsPushConstants.slots0.x;
        const uint dstMortonSlot = g_NwbPhysicsPushConstants.slots0.y;
        const uint radixHistSlot = g_NwbPhysicsPushConstants.slots0.z;
        const uint localN = min(itemCount - blockBase, NWB_PHYSICS_LBVH_RADIX_BLOCK_SIZE);

        for(uint i = localTid; i < localN; i += NWB_PHYSICS_WORKGROUP_SIZE){
            s_LbvhSortMorton[i] = g_NwbPhysicsUintBuffers[srcMortonSlot][(blockBase + i) * 2u + 0u];
            s_LbvhSortShape[i] = g_NwbPhysicsUintBuffers[srcMortonSlot][(blockBase + i) * 2u + 1u];
        }
        const uint blockHistBase = groupID.x * 256u;
        const uint globalHistBase = numBlocks * 256u;
        for(uint d = localTid; d < 256u; d += NWB_PHYSICS_WORKGROUP_SIZE)
            s_RadixHist[d] = g_NwbPhysicsUintBuffers[radixHistSlot][globalHistBase + d] + g_NwbPhysicsUintBuffers[radixHistSlot][blockHistBase + d];
        GroupMemoryBarrierWithGroupSync();

        [unroll]
        for(uint subPass = 0u; subPass < 2u; ++subPass){
            const uint srcBase = (subPass == 0u) ? 0u : 256u;
            const uint dstBase = (subPass == 0u) ? 256u : 0u;
            const uint subShift = shift + subPass * 4u;

            for(uint b = 0u; b < 16u; ++b)
                s_RadixLocalHist[b * NWB_PHYSICS_WORKGROUP_SIZE + localTid] = 0u;
            [unroll]
            for(uint k = 0u; k < 4u; ++k){
                const uint i = localTid * 4u + k;
                if(i < localN){
                    const uint nib = (s_LbvhSortMorton[srcBase + i] >> subShift) & 0xfu;
                    s_RadixLocalHist[nib * NWB_PHYSICS_WORKGROUP_SIZE + localTid] += 1u;
                }
            }
            GroupMemoryBarrierWithGroupSync();

            const uint segBase = localTid * 16u;
            uint sum = 0u;
            for(uint i = 0u; i < 16u; ++i){
                const uint c = s_RadixLocalHist[segBase + i];
                s_RadixLocalHist[segBase + i] = sum;
                sum += c;
            }
            s_RadixAux[localTid] = sum;
            GroupMemoryBarrierWithGroupSync();

            uint scanVal = sum;
            [unroll]
            for(uint offset = 1u; offset < 64u; offset <<= 1u){
                const uint addVal = (localTid >= offset) ? s_RadixAux[localTid - offset] : 0u;
                GroupMemoryBarrierWithGroupSync();
                scanVal += addVal;
                s_RadixAux[localTid] = scanVal;
                GroupMemoryBarrierWithGroupSync();
            }

            const uint segPrefix = (localTid > 0u) ? s_RadixAux[localTid - 1u] : 0u;
            for(uint i = 0u; i < 16u; ++i)
                s_RadixLocalHist[segBase + i] += segPrefix;
            GroupMemoryBarrierWithGroupSync();

            [unroll]
            for(uint k = 0u; k < 4u; ++k){
                const uint i = localTid * 4u + k;
                if(i < localN){
                    const uint m = s_LbvhSortMorton[srcBase + i];
                    const uint s = s_LbvhSortShape[srcBase + i];
                    const uint nib = (m >> subShift) & 0xfu;
                    const uint dst = s_RadixLocalHist[nib * NWB_PHYSICS_WORKGROUP_SIZE + localTid];
                    s_RadixLocalHist[nib * NWB_PHYSICS_WORKGROUP_SIZE + localTid] = dst + 1u;
                    s_LbvhSortMorton[dstBase + dst] = m;
                    s_LbvhSortShape[dstBase + dst] = s;
                }
            }
            GroupMemoryBarrierWithGroupSync();
        }

        for(uint i = localTid; i < localN; i += NWB_PHYSICS_WORKGROUP_SIZE){
            const uint d = (s_LbvhSortMorton[i] >> shift) & 0xffu;
            if(i == 0u || ((s_LbvhSortMorton[i - 1u] >> shift) & 0xffu) != d)
                s_RadixAux[d] = i;
        }
        GroupMemoryBarrierWithGroupSync();

        for(uint i = localTid; i < localN; i += NWB_PHYSICS_WORKGROUP_SIZE){
            const uint m = s_LbvhSortMorton[i];
            const uint s = s_LbvhSortShape[i];
            const uint d = (m >> shift) & 0xffu;
            const uint dst = s_RadixHist[d] + (i - s_RadixAux[d]);
            g_NwbPhysicsUintBuffers[dstMortonSlot][dst * 2u + 0u] = m;
            g_NwbPhysicsUintBuffers[dstMortonSlot][dst * 2u + 1u] = s;
        }
        return;
    }

    if(mode == NWB_PHYSICS_LBVH_MODE_BUILD_TREE){
        if(tid >= itemCount || itemCount < 2u)
            return;

        if(tid == 0u)
            g_NwbPhysicsUintBuffers[lbvhMetaSlot][0u] = NWB_PHYSICS_INVALID_INDEX;

        const uint shapeIdx = g_NwbPhysicsUintBuffers[mortonSlot][tid * 2u + 1u];
        const uint leafIdx = (itemCount - 1u) + tid;
        if(shapeIdx != NWB_PHYSICS_INVALID_INDEX){
            const float3 aMin = g_NwbPhysicsFloat4Buffers[shapeAabbSlot][shapeIdx * 2u + 0u].xyz;
            const float3 aMax = g_NwbPhysicsFloat4Buffers[shapeAabbSlot][shapeIdx * 2u + 1u].xyz;
            g_NwbPhysicsLbvhNodeReadBuffers[lbvhNodeSlot][leafIdx].minLeft = float4(aMin, asfloat(shapeIdx));
            g_NwbPhysicsLbvhNodeReadBuffers[lbvhNodeSlot][leafIdx].maxRight = float4(aMax, asfloat(shapeIdx));
        }

        if(tid + 1u >= itemCount)
            return;

        const int i = int(tid);
        const uint ci = g_NwbPhysicsUintBuffers[mortonSlot][tid * 2u + 0u];
        const int dPlus = nwbPhysicsLbvhClzDelta(i, ci, i + 1, itemCount, mortonSlot);
        const int dMinus = nwbPhysicsLbvhClzDelta(i, ci, i - 1, itemCount, mortonSlot);
        const int d = (dPlus - dMinus >= 0) ? 1 : -1;
        const int deltaMin = (d > 0) ? dMinus : dPlus;
        int lMax = 2;
        while(nwbPhysicsLbvhClzDelta(i, ci, i + lMax * d, itemCount, mortonSlot) > deltaMin)
            lMax *= 2;

        int l = 0;
        for(int step = lMax / 2; step >= 1; step /= 2){
            if(nwbPhysicsLbvhClzDelta(i, ci, i + (l + step) * d, itemCount, mortonSlot) > deltaMin)
                l += step;
        }
        const int j = i + l * d;
        const int deltaNode = nwbPhysicsLbvhClzDelta(i, ci, j, itemCount, mortonSlot);

        int s = 0;
        int t = (l + 1) / 2;
        while(t >= 1){
            if((s + t) < l && nwbPhysicsLbvhClzDelta(i, ci, i + (s + t) * d, itemCount, mortonSlot) > deltaNode)
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

        g_NwbPhysicsLbvhNodeReadBuffers[lbvhNodeSlot][tid].minLeft = float4(0.0f, 0.0f, 0.0f, asfloat(leftChild));
        g_NwbPhysicsLbvhNodeReadBuffers[lbvhNodeSlot][tid].maxRight = float4(0.0f, 0.0f, 0.0f, asfloat(rightChild));
        g_NwbPhysicsUintBuffers[lbvhMetaSlot][tid * 2u + 1u] = 0u;
        g_NwbPhysicsUintBuffers[lbvhMetaSlot][leftChild * 2u + 0u] = tid;
        g_NwbPhysicsUintBuffers[lbvhMetaSlot][rightChild * 2u + 0u] = tid;
        return;
    }

    if(mode == NWB_PHYSICS_LBVH_MODE_REFIT_AABBS){
        if(tid >= itemCount || itemCount < 2u)
            return;

        uint cur = (itemCount - 1u) + tid;
        const NwbPhysicsGpuLbvhNode leafNode = g_NwbPhysicsLbvhNodeBuffers[lbvhNodeSlot][cur];
        float3 curMin = leafNode.minLeft.xyz;
        float3 curMax = leafNode.maxRight.xyz;
        uint parent = g_NwbPhysicsUintBuffers[lbvhMetaSlot][cur * 2u + 0u];
        bool firstLevel = true;
        while(parent != NWB_PHYSICS_INVALID_INDEX){
            if(!firstLevel)
                DeviceMemoryBarrier();
            firstLevel = false;
            uint prevVisits = 0u;
            InterlockedAdd(g_NwbPhysicsUintBuffers[lbvhMetaSlot][parent * 2u + 1u], 1u, prevVisits);
            if(prevVisits == 0u)
                break;
            DeviceMemoryBarrier();

            const NwbPhysicsGpuLbvhNode pNode = g_NwbPhysicsLbvhNodeBuffers[lbvhNodeSlot][parent];
            const uint leftChild = asuint(pNode.minLeft.w);
            const uint rightChild = asuint(pNode.maxRight.w);
            const uint sibling = (cur == leftChild) ? rightChild : leftChild;
            const NwbPhysicsGpuLbvhNode sibNode = g_NwbPhysicsLbvhNodeBuffers[lbvhNodeSlot][sibling];

            curMin = min(curMin, sibNode.minLeft.xyz);
            curMax = max(curMax, sibNode.maxRight.xyz);
            g_NwbPhysicsLbvhNodeBuffers[lbvhNodeSlot][parent].minLeft = float4(curMin, asfloat(leftChild));
            g_NwbPhysicsLbvhNodeBuffers[lbvhNodeSlot][parent].maxRight = float4(curMax, asfloat(rightChild));

            cur = parent;
            parent = g_NwbPhysicsUintBuffers[lbvhMetaSlot][parent * 2u + 0u];
        }
        return;
    }

    if(mode == NWB_PHYSICS_LBVH_MODE_QUERY_PAIRS){
        const uint totalShapes = asuint(g_NwbPhysicsPushConstants.gravitySpeculative.x);
        const uint surfacePairSlot = asuint(g_NwbPhysicsPushConstants.gravitySpeculative.y);
        const uint maxPairs = g_NwbPhysicsPushConstants.flags;
        const bool active = (tid < itemCount) && (totalShapes >= 2u);

        uint idxA = 0u;
        float3 minA = float3(0.0f, 0.0f, 0.0f);
        float3 maxA = float3(0.0f, 0.0f, 0.0f);
        uint4 metaA = uint4(0u, 0u, 0u, 0u);
        uint4 filterA = uint4(0u, 0u, 0u, 0u);

        uint stack[64];
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
                    nwbPhysicsLbvhTestLeaf(idxA, asuint(lNode.minLeft.w), itemOffset, metaA, filterA, shapeSlot, pairSlot, surfacePairSlot, counterSlot, maxPairs, nonCollideSlot, nonCollideMask);
                }
                else if(sp < 64u){
                    stack[sp++] = leftIdx;
                }
                else{
                    stackOverflow = true;
                }
            }
            if(hitR){
                if(rightIdx >= leafStart){
                    nwbPhysicsLbvhTestLeaf(idxA, asuint(rNode.minLeft.w), itemOffset, metaA, filterA, shapeSlot, pairSlot, surfacePairSlot, counterSlot, maxPairs, nonCollideSlot, nonCollideMask);
                }
                else if(sp < 64u){
                    stack[sp++] = rightIdx;
                }
                else{
                    stackOverflow = true;
                }
            }
            if(stackOverflow)
                break;
        }
        if(stackOverflow)
            InterlockedOr(g_NwbPhysicsUintBuffers[counterSlot][4u], 1u);
        return;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

