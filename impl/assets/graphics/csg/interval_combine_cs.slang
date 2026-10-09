// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "interval_dispatch.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if NWB_CSG_RECEIVER_SPAN_LAYER_COUNT * NWB_CSG_PEEL_LAYER_COUNT > 0xffffu
#error "CSG interval source pairs require each span/peel index to fit 16 bits"
#endif

struct NwbCsgRemovedIntervalCandidate{
    float sortDepth;
    uint sourcePair;
};

struct NwbCsgClippedInterval{
    float2 linearDepth;
    float cutterExit;
    uint intervalIndex;
};

// The heap-selected CSG context resolves every input/output texture through typed StorageImage array aliases.
#define g_NwbCsgBackCapNormal (nwbHeapStorageImage2DArrayFloat4(g_NwbCsgBindlessResources.csgPeelSlots.x))
#define g_NwbCsgIntervalDepth (nwbHeapStorageImage2DArrayFloat4(g_NwbCsgBindlessResources.csgPeelSlots.y))
#define g_NwbCsgIntervalId (nwbHeapStorageImage2DArrayUint(g_NwbCsgBindlessResources.csgPeelSlots.z))
#define g_NwbCsgReceiverSpanData (nwbHeapStorageImage2DArrayUint4(g_NwbCsgBindlessResources.csgSpanSlots.y))
#define g_NwbCsgReceiverSpanCount (nwbHeapStorageImage2DArrayUint2(g_NwbCsgBindlessResources.csgSpanSlots.z))
#define g_NwbCsgRemovedIntervalDepth (nwbHeapStorageImage2DArrayFloat4(g_NwbCsgBindlessResources.csgSpanSlots.w))
#define g_NwbCsgRemovedIntervalCapNormal (nwbHeapStorageImage2DArrayFloat4(g_NwbCsgBindlessResources.csgRemovedSlots.x))
#define g_NwbCsgRemovedIntervalData (nwbHeapStorageImage2DArrayUint4(g_NwbCsgBindlessResources.csgRemovedSlots.y))
#define g_NwbCsgRemovedIntervalCount (nwbHeapStorageImage2DArrayUint2(g_NwbCsgBindlessResources.csgRemovedSlots.z))


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


uint2 nwbCsgIntervalCombineFrameExtent(){
    uint width = 1u;
    uint height = 1u;
    uint layerCount = 1u;
    g_NwbCsgRemovedIntervalCount.GetDimensions(width, height, layerCount);
    return uint2(width, height);
}

uint nwbCsgIntervalCombineIntervalLayerCount(){
    uint width = 1u;
    uint height = 1u;
    uint layerCount = 1u;
    g_NwbCsgIntervalId.GetDimensions(width, height, layerCount);
    return min(layerCount, NWB_CSG_PEEL_LAYER_COUNT);
}

uint nwbCsgIntervalCombineReceiverSpanLayerCount(){
    uint width = 1u;
    uint height = 1u;
    uint layerCount = 1u;
    g_NwbCsgReceiverSpanData.GetDimensions(width, height, layerCount);
    return min(layerCount, NWB_CSG_RECEIVER_SPAN_LAYER_COUNT);
}

uint nwbCsgIntervalCombineRemovedIntervalLayerCount(){
    uint width = 1u;
    uint height = 1u;
    uint layerCount = 1u;
    g_NwbCsgRemovedIntervalDepth.GetDimensions(width, height, layerCount);
    return min(layerCount, NWB_CSG_REMOVED_INTERVAL_LAYER_COUNT);
}

uint2 nwbCsgIntervalCombineWorkOffset(){
    return nwbCsgIntervalDispatchWorkOffset();
}

uint2 nwbCsgIntervalCombineWorkExtent(){
    return nwbCsgIntervalDispatchWorkExtent();
}

uint nwbCsgIntervalCombineIdReceiverIndex(const uint id){
    const uint receiverBits = id & NWB_CSG_RECEIVER_INDEX_MASK;
    return receiverBits == 0u ? NWB_CSG_INVALID_RECEIVER_INDEX : receiverBits - 1u;
}

uint nwbCsgIntervalCombineReceiverSpanFlagsToRemovedFlags(const uint spanFlags){
    uint flags = 0u;
    if((spanFlags & NWB_CSG_RECEIVER_SPAN_AMBIGUOUS_FLAG) != 0u)
        flags |= NWB_CSG_REMOVED_INTERVAL_RECEIVER_SPAN_AMBIGUOUS_FLAG;
    if((spanFlags & NWB_CSG_RECEIVER_SPAN_OVERFLOW_FLAG) != 0u)
        flags |= NWB_CSG_REMOVED_INTERVAL_RECEIVER_SPAN_OVERFLOW_FLAG;
    if((spanFlags & NWB_CSG_RECEIVER_SPAN_EVENT_OVERFLOW_FLAG) != 0u)
        flags |= NWB_CSG_REMOVED_INTERVAL_RECEIVER_EVENT_OVERFLOW_FLAG;
    return flags;
}

void nwbCsgIntervalCombineInsertEntry(
    inout NwbCsgRemovedIntervalCandidate entries[NWB_CSG_REMOVED_INTERVAL_LAYER_COUNT],
    const NwbCsgRemovedIntervalCandidate candidate,
    const uint removedIntervalLayerCount,
    inout uint inOutRemovedIntervalCount,
    inout uint inOutFlags
){
    const uint entryIndex = inOutRemovedIntervalCount;
    ++inOutRemovedIntervalCount;
    if(entryIndex >= removedIntervalLayerCount){
        inOutFlags |= NWB_CSG_REMOVED_INTERVAL_OVERFLOW_FLAG;
        return;
    }

    [loop]
    for(uint layerIndex = 0u; layerIndex < removedIntervalLayerCount; ++layerIndex){
        if(candidate.sortDepth >= entries[layerIndex].sortDepth)
            continue;

        NwbCsgRemovedIntervalCandidate displaced = entries[layerIndex];
        entries[layerIndex] = candidate;
        [loop]
        for(uint shiftIndex = layerIndex + 1u; shiftIndex < removedIntervalLayerCount; ++shiftIndex){
            NwbCsgRemovedIntervalCandidate next = entries[shiftIndex];
            entries[shiftIndex] = displaced;
            displaced = next;
        }
        return;
    }
}

bool nwbCsgIntervalCombineBackCapVisible(const float2 spanDepth, const float4 cutterDepth, const half4 capNormal){
    return capNormal.w > half(0.0h)
        && cutterDepth.w + NWB_CSG_INTERVAL_OVERLAP_EPSILON >= spanDepth.x
        && cutterDepth.w <= spanDepth.y + NWB_CSG_INTERVAL_OVERLAP_EPSILON
    ;
}

void nwbCsgIntervalCombineAppendUnion(
    inout NwbCsgRemovedIntervalCandidate entries[NWB_CSG_REMOVED_INTERVAL_LAYER_COUNT],
    const uint2 pixel,
    const float2 spanDepth,
    const uint spanIndex,
    const uint firstInterval,
    const uint lastInterval,
    const uint removedIntervalLayerCount,
    inout uint inOutRemovedIntervalCount,
    inout uint inOutFlags
){
    const float4 firstDepth = g_NwbCsgIntervalDepth.Load(int3(int2(pixel), int(firstInterval)));
    const int3 lastTexel = int3(int2(pixel), int(lastInterval));
    const float4 lastDepth = g_NwbCsgIntervalDepth.Load(lastTexel);
    const half4 capNormal = half4(g_NwbCsgBackCapNormal.Load(lastTexel));
    const bool backCapVisible = nwbCsgIntervalCombineBackCapVisible(spanDepth, lastDepth, capNormal);

    NwbCsgRemovedIntervalCandidate entry;
    entry.sortDepth = backCapVisible ? lastDepth.w : max(spanDepth.x, firstDepth.z);
    const uint firstSource = 1u + spanIndex * NWB_CSG_PEEL_LAYER_COUNT + firstInterval;
    const uint lastSource = 1u + spanIndex * NWB_CSG_PEEL_LAYER_COUNT + lastInterval;
    entry.sourcePair = firstSource | (lastSource << 16u);
    nwbCsgIntervalCombineInsertEntry(
        entries, entry, removedIntervalLayerCount, inOutRemovedIntervalCount, inOutFlags
    );
}

void nwbCsgIntervalCombineSpan(
    inout NwbCsgRemovedIntervalCandidate entries[NWB_CSG_REMOVED_INTERVAL_LAYER_COUNT],
    const uint2 pixel,
    const uint4 spanData,
    const uint spanIndex,
    const uint intervalLayerCount,
    const uint removedIntervalLayerCount,
    inout uint inOutRemovedIntervalCount,
    inout uint inOutFlags
){
    const float2 spanDepth = float2(asfloat(spanData.y), asfloat(spanData.z));
    NwbCsgClippedInterval intervals[NWB_CSG_PEEL_LAYER_COUNT];
    [unroll]
    for(uint intervalIndex = 0u; intervalIndex < NWB_CSG_PEEL_LAYER_COUNT; ++intervalIndex)
        intervals[intervalIndex] = (NwbCsgClippedInterval)0;
    uint intervalCount = 0u;
    [unroll]
    for(uint intervalIndex = 0u; intervalIndex < NWB_CSG_PEEL_LAYER_COUNT; ++intervalIndex){
        if(intervalIndex >= intervalLayerCount)
            break;
        const int3 texel = int3(int2(pixel), int(intervalIndex));
        const uint intervalId = g_NwbCsgIntervalId.Load(texel);
        if(intervalId == 0u || nwbCsgIntervalCombineIdReceiverIndex(intervalId) != spanData.x)
            continue;

        const float2 cutterDepth = g_NwbCsgIntervalDepth.Load(texel).zw;
        NwbCsgClippedInterval candidate;
        candidate.linearDepth = float2(max(spanDepth.x, cutterDepth.x), min(spanDepth.y, cutterDepth.y));
        if(candidate.linearDepth.y <= candidate.linearDepth.x + NWB_CSG_INTERVAL_OVERLAP_EPSILON)
            continue;
        candidate.cutterExit = cutterDepth.y;
        candidate.intervalIndex = intervalIndex;

        int insertIndex = int(intervalCount);
        while(insertIndex > 0 && candidate.linearDepth.x < intervals[uint(insertIndex - 1)].linearDepth.x){
            intervals[uint(insertIndex)] = intervals[uint(insertIndex - 1)];
            --insertIndex;
        }
        intervals[uint(insertIndex)] = candidate;
        ++intervalCount;
    }
    if(intervalCount == 0u)
        return;

    uint firstInterval = intervals[0].intervalIndex;
    uint lastInterval = firstInterval;
    float unionExit = intervals[0].linearDepth.y;
    float unionCutterExit = intervals[0].cutterExit;
    [unroll]
    for(uint intervalIndex = 1u; intervalIndex < NWB_CSG_PEEL_LAYER_COUNT; ++intervalIndex){
        if(intervalIndex >= intervalCount)
            break;
        const NwbCsgClippedInterval candidate = intervals[intervalIndex];
        if(candidate.linearDepth.x > unionExit + NWB_CSG_INTERVAL_OVERLAP_EPSILON){
            nwbCsgIntervalCombineAppendUnion(
                entries, pixel, spanDepth, spanIndex, firstInterval, lastInterval,
                removedIntervalLayerCount, inOutRemovedIntervalCount, inOutFlags
            );
            firstInterval = candidate.intervalIndex;
            lastInterval = firstInterval;
            unionExit = candidate.linearDepth.y;
            unionCutterExit = candidate.cutterExit;
        }
        else if(candidate.cutterExit > unionCutterExit){
            // Only the cutter extending the union's far boundary owns its visible cap and normal.
            lastInterval = candidate.intervalIndex;
            unionExit = candidate.linearDepth.y;
            unionCutterExit = candidate.cutterExit;
        }
    }
    nwbCsgIntervalCombineAppendUnion(
        entries, pixel, spanDepth, spanIndex, firstInterval, lastInterval,
        removedIntervalLayerCount, inOutRemovedIntervalCount, inOutFlags
    );
}

void nwbCsgIntervalCombineWriteEntries(
    const uint2 pixel,
    const NwbCsgRemovedIntervalCandidate entries[NWB_CSG_REMOVED_INTERVAL_LAYER_COUNT],
    const uint removedIntervalLayerCount,
    const uint removedIntervalCount,
    const uint flags
){
    const uint storedIntervalCount = min(removedIntervalCount, removedIntervalLayerCount);
    [loop]
    for(uint layerIndex = 0u; layerIndex < storedIntervalCount; ++layerIndex){
        const uint3 texel = uint3(pixel, layerIndex);
        const uint sourcePair = entries[layerIndex].sourcePair;
        if(sourcePair == 0u){
            g_NwbCsgRemovedIntervalDepth[texel] = float4(0.0, 0.0, 0.0, 0.0);
            g_NwbCsgRemovedIntervalCapNormal[texel] = float4(0.0, 0.0, 0.0, 0.0);
            g_NwbCsgRemovedIntervalData[texel] = uint4(0u, 0u, 0u, 0u);
            continue;
        }

        const uint firstSource = sourcePair & 0xffffu;
        const uint lastSource = sourcePair >> 16u;
        const uint spanIndex = (firstSource - 1u) / NWB_CSG_PEEL_LAYER_COUNT;
        const uint firstInterval = (firstSource - 1u) % NWB_CSG_PEEL_LAYER_COUNT;
        const uint lastInterval = (lastSource - 1u) % NWB_CSG_PEEL_LAYER_COUNT;
        const uint4 spanData = g_NwbCsgReceiverSpanData.Load(int3(int2(pixel), int(spanIndex)));
        const float2 spanDepth = float2(asfloat(spanData.y), asfloat(spanData.z));
        const float4 firstDepth = g_NwbCsgIntervalDepth.Load(int3(int2(pixel), int(firstInterval)));
        const int3 lastTexel = int3(int2(pixel), int(lastInterval));
        const float4 lastDepth = g_NwbCsgIntervalDepth.Load(lastTexel);
        const float removedEnterLinearDepth = max(spanDepth.x, firstDepth.z);
        const float removedExitLinearDepth = min(spanDepth.y, lastDepth.w);
        const half4 capNormal = half4(g_NwbCsgBackCapNormal.Load(lastTexel));
        const bool backCapVisible = nwbCsgIntervalCombineBackCapVisible(spanDepth, lastDepth, capNormal);
        const uint entryFlags = backCapVisible ? NWB_CSG_REMOVED_INTERVAL_BACK_CAP_VISIBLE_FLAG : 0u;
        g_NwbCsgRemovedIntervalDepth[texel] = float4(firstDepth.x, lastDepth.y, removedEnterLinearDepth, removedExitLinearDepth);
        g_NwbCsgRemovedIntervalCapNormal[texel] = float4(capNormal);
        g_NwbCsgRemovedIntervalData[texel] = uint4(spanData.x, entryFlags, spanIndex, lastInterval);
    }

    g_NwbCsgRemovedIntervalCount[uint3(pixel, 0u)] = uint2(removedIntervalCount, flags);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_CSG_INTERVAL_PEEL_GROUP_SIZE_X, NWB_CSG_INTERVAL_PEEL_GROUP_SIZE_Y, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint2 frameExtent = nwbCsgIntervalCombineFrameExtent();
    const uint2 workExtent = nwbCsgIntervalCombineWorkExtent();
    if(dispatchThreadID.x >= workExtent.x || dispatchThreadID.y >= workExtent.y)
        return;

    const uint2 pixel = dispatchThreadID.xy + nwbCsgIntervalCombineWorkOffset();
    if(pixel.x >= frameExtent.x || pixel.y >= frameExtent.y)
        return;

    const uint intervalLayerCount = nwbCsgIntervalCombineIntervalLayerCount();
    const uint spanLayerCount = nwbCsgIntervalCombineReceiverSpanLayerCount();
    const uint removedIntervalLayerCount = nwbCsgIntervalCombineRemovedIntervalLayerCount();

    NwbCsgRemovedIntervalCandidate entries[NWB_CSG_REMOVED_INTERVAL_LAYER_COUNT];
    [unroll]
    for(uint layerIndex = 0u; layerIndex < NWB_CSG_REMOVED_INTERVAL_LAYER_COUNT; ++layerIndex){
        entries[layerIndex].sortDepth = NWB_CSG_INVALID_DISTANCE;
        entries[layerIndex].sourcePair = 0u;
    }

    const uint2 spanCounts = g_NwbCsgReceiverSpanCount.Load(int3(int2(pixel), 0));
    uint flags = 0u;
    flags |= nwbCsgIntervalCombineReceiverSpanFlagsToRemovedFlags(spanCounts.y);

    const uint rawSpanCount = spanCounts.x;
    if(rawSpanCount > spanLayerCount)
        flags |= NWB_CSG_REMOVED_INTERVAL_RECEIVER_SPAN_OVERFLOW_FLAG;
    const uint spanCount = min(rawSpanCount, spanLayerCount);

    uint removedIntervalCount = 0u;
    [loop]
    for(uint spanIndex = 0u; spanIndex < spanCount; ++spanIndex){
        const uint4 spanData = g_NwbCsgReceiverSpanData.Load(int3(int2(pixel), int(spanIndex)));
        nwbCsgIntervalCombineSpan(
            entries, pixel, spanData, spanIndex, intervalLayerCount,
            removedIntervalLayerCount, removedIntervalCount, flags
        );
    }

    nwbCsgIntervalCombineWriteEntries(pixel, entries, removedIntervalLayerCount, removedIntervalCount, flags);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

