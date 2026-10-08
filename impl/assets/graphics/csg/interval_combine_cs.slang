// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "interval_dispatch.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NwbCsgRemovedIntervalCandidate{
    float sortDepth;
    uint source;
};

// Every input/output texture is a CSG StorageImage heap entry. The local binding numbers remain intentional gaps; the heap-selected CSG context resolves the typed array aliases for this dispatch.
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

void nwbCsgIntervalCombineAppendCandidate(
    inout NwbCsgRemovedIntervalCandidate entries[NWB_CSG_REMOVED_INTERVAL_LAYER_COUNT],
    const uint2 pixel,
    const uint4 spanData,
    const uint spanIndex,
    const uint intervalIndex,
    const uint removedIntervalLayerCount,
    inout uint inOutRemovedIntervalCount,
    inout uint inOutFlags
){
    const uint receiverIndex = spanData.x;
    const float2 spanDepth = float2(asfloat(spanData.y), asfloat(spanData.z));

    const int3 intervalTexel = int3(int2(pixel), int(intervalIndex));
    const uint intervalId = g_NwbCsgIntervalId.Load(intervalTexel);
    if(intervalId == 0u)
        return;
    if(nwbCsgIntervalCombineIdReceiverIndex(intervalId) != receiverIndex)
        return;

    const float4 cutterDepth = g_NwbCsgIntervalDepth.Load(intervalTexel);
    const float2 cutterLinearDepth = cutterDepth.zw;
    const float removedEnterLinearDepth = max(spanDepth.x, cutterLinearDepth.x);
    const float removedExitLinearDepth = min(spanDepth.y, cutterLinearDepth.y);
    if(removedExitLinearDepth <= removedEnterLinearDepth + NWB_CSG_INTERVAL_OVERLAP_EPSILON)
        return;

    const bool backCapVisible =
        cutterLinearDepth.y + NWB_CSG_INTERVAL_OVERLAP_EPSILON >= spanDepth.x
        && cutterLinearDepth.y <= spanDepth.y + NWB_CSG_INTERVAL_OVERLAP_EPSILON
    ;
    NwbCsgRemovedIntervalCandidate entry;
    entry.sortDepth = backCapVisible ? cutterLinearDepth.y : removedEnterLinearDepth;
    // Zero retains the original empty entry when no candidate sorts before the invalid-distance sentinel.
    entry.source = 1u + spanIndex * NWB_CSG_PEEL_LAYER_COUNT + intervalIndex;

    nwbCsgIntervalCombineInsertEntry(
        entries,
        entry,
        removedIntervalLayerCount,
        inOutRemovedIntervalCount,
        inOutFlags
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
        const uint source = entries[layerIndex].source;
        if(source == 0u){
            g_NwbCsgRemovedIntervalDepth[texel] = float4(0.0, 0.0, 0.0, 0.0);
            g_NwbCsgRemovedIntervalCapNormal[texel] = float4(0.0, 0.0, 0.0, 0.0);
            g_NwbCsgRemovedIntervalData[texel] = uint4(0u, 0u, 0u, 0u);
            continue;
        }

        const uint spanIndex = (source - 1u) / NWB_CSG_PEEL_LAYER_COUNT;
        const uint intervalIndex = (source - 1u) % NWB_CSG_PEEL_LAYER_COUNT;
        const uint4 spanData = g_NwbCsgReceiverSpanData.Load(int3(int2(pixel), int(spanIndex)));
        const float2 spanDepth = float2(asfloat(spanData.y), asfloat(spanData.z));
        const int3 intervalTexel = int3(int2(pixel), int(intervalIndex));
        const float4 cutterDepth = g_NwbCsgIntervalDepth.Load(intervalTexel);
        const float2 cutterLinearDepth = cutterDepth.zw;
        const float removedEnterLinearDepth = max(spanDepth.x, cutterLinearDepth.x);
        const float removedExitLinearDepth = min(spanDepth.y, cutterLinearDepth.y);
        const bool backCapVisible = cutterLinearDepth.y + NWB_CSG_INTERVAL_OVERLAP_EPSILON >= spanDepth.x && cutterLinearDepth.y <= spanDepth.y + NWB_CSG_INTERVAL_OVERLAP_EPSILON;
        const uint entryFlags = backCapVisible ? NWB_CSG_REMOVED_INTERVAL_BACK_CAP_VISIBLE_FLAG : 0u;
        const half4 capNormal = half4(g_NwbCsgBackCapNormal.Load(intervalTexel));
        g_NwbCsgRemovedIntervalDepth[texel] = float4(cutterDepth.xy, removedEnterLinearDepth, removedExitLinearDepth);
        g_NwbCsgRemovedIntervalCapNormal[texel] = float4(capNormal);
        g_NwbCsgRemovedIntervalData[texel] = uint4(spanData.x, entryFlags, spanIndex, intervalIndex);
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
        entries[layerIndex].source = 0u;
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
        [loop]
        for(uint intervalIndex = 0u; intervalIndex < intervalLayerCount; ++intervalIndex){
            nwbCsgIntervalCombineAppendCandidate(
                entries,
                pixel,
                spanData,
                spanIndex,
                intervalIndex,
                removedIntervalLayerCount,
                removedIntervalCount,
                flags
            );
        }
    }

    nwbCsgIntervalCombineWriteEntries(pixel, entries, removedIntervalLayerCount, removedIntervalCount, flags);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

