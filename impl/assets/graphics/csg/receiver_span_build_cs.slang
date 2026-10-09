// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "interval_dispatch.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NwbCsgReceiverSurfaceEvent{
    float depth;
    uint receiverIndex;
    uint faceSign;
};

// The heap-selected CSG context resolves event reads and span writes from the shared StorageImage table.
#define g_NwbCsgReceiverEventData (nwbHeapStorageImage2DArrayUint4(g_NwbCsgBindlessResources.csgPeelSlots.w))
#define g_NwbCsgReceiverEventCount (nwbHeapStorageImage2DArrayUint(g_NwbCsgBindlessResources.csgSpanSlots.x))
#define g_NwbCsgReceiverSpanData (nwbHeapStorageImage2DArrayUint4(g_NwbCsgBindlessResources.csgSpanSlots.y))
#define g_NwbCsgReceiverSpanCount (nwbHeapStorageImage2DArrayUint2(g_NwbCsgBindlessResources.csgSpanSlots.z))


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


uint2 nwbCsgReceiverSpanBuildFrameExtent(){
    uint width = 1u;
    uint height = 1u;
    uint layerCount = 1u;
    g_NwbCsgReceiverEventCount.GetDimensions(width, height, layerCount);
    return uint2(width, height);
}

uint nwbCsgReceiverSpanBuildEventLayerCount(){
    uint width = 1u;
    uint height = 1u;
    uint layerCount = 1u;
    g_NwbCsgReceiverEventData.GetDimensions(width, height, layerCount);
    return min(layerCount, NWB_CSG_RECEIVER_EVENT_LAYER_COUNT);
}

uint nwbCsgReceiverSpanBuildSpanLayerCount(){
    uint width = 1u;
    uint height = 1u;
    uint layerCount = 1u;
    g_NwbCsgReceiverSpanData.GetDimensions(width, height, layerCount);
    return min(layerCount, NWB_CSG_RECEIVER_SPAN_LAYER_COUNT);
}

uint2 nwbCsgReceiverSpanBuildWorkOffset(){
    return nwbCsgIntervalDispatchWorkOffset();
}

uint2 nwbCsgReceiverSpanBuildWorkExtent(){
    return nwbCsgIntervalDispatchWorkExtent();
}

uint nwbCsgReceiverSpanBuildLoadEventCount(const uint2 pixel, const uint eventLayerCount, inout uint inOutFlags){
    const uint eventCount = g_NwbCsgReceiverEventCount.Load(int3(int2(pixel), 0));
    // The event emit pass keeps counting past the storable layer budget, so a raw count above the budget is exactly the overflow condition; no separate event-flags texture is required.
    if(eventCount > eventLayerCount)
        inOutFlags |= NWB_CSG_RECEIVER_SPAN_EVENT_OVERFLOW_FLAG;

    return min(eventCount, eventLayerCount);
}

NwbCsgReceiverSurfaceEvent nwbCsgReceiverSpanBuildLoadEvent(const uint2 pixel, const uint eventIndex){
    const int3 texel = int3(int2(pixel), int(eventIndex));
    const uint4 data = g_NwbCsgReceiverEventData.Load(texel);

    NwbCsgReceiverSurfaceEvent result;
    result.receiverIndex = data.x;
    result.faceSign = data.y;
    result.depth = asfloat(data.z);
    return result;
}

void nwbCsgReceiverSpanBuildSortEvents(
    __ref uint3 events[NWB_CSG_RECEIVER_EVENT_LAYER_COUNT],
    const uint eventCount
){
    [loop]
    for(uint eventIndex = 1u; eventIndex < eventCount; ++eventIndex){
        const uint3 candidate = events[eventIndex];
        uint destination = eventIndex;
        while(destination > 0u){
            const uint3 predecessor = events[destination - 1u];
            if(!(candidate.x < predecessor.x || (candidate.x == predecessor.x && asfloat(candidate.z) < asfloat(predecessor.z))))
                break;
            events[destination] = predecessor;
            --destination;
        }
        events[destination] = candidate;
    }
}

void nwbCsgReceiverSpanBuildWriteSpan(
    const uint2 pixel,
    const uint spanLayerCount,
    const uint receiverIndex,
    const float enterDepth,
    const float exitDepth,
    inout uint inOutSpanCount,
    inout uint inOutFlags
){
    const uint spanIndex = inOutSpanCount;
    ++inOutSpanCount;

    if(spanIndex >= spanLayerCount){
        inOutFlags |= NWB_CSG_RECEIVER_SPAN_OVERFLOW_FLAG;
        return;
    }

    const uint3 texel = uint3(pixel, spanIndex);
    g_NwbCsgReceiverSpanData[texel] = uint4(receiverIndex, asuint(enterDepth), asuint(exitDepth), 0u);
}

void nwbCsgReceiverSpanBuildPairEvents(
    const uint2 pixel,
    const uint spanLayerCount,
    __constref uint3 events[NWB_CSG_RECEIVER_EVENT_LAYER_COUNT],
    const uint eventCount,
    inout uint inOutSpanCount,
    inout uint inOutFlags
){
    // Each receiver has an independent winding walk; nested shells merge only within that receiver.
    uint winding = 0u;
    float spanEnterDepth = 0.0;
    uint spanReceiverIndex = 0u;
    [loop]
    for(uint eventIndex = 0u; eventIndex < eventCount; ++eventIndex){
        const uint3 surfaceEvent = events[eventIndex];
        if(eventIndex == 0u || surfaceEvent.x != spanReceiverIndex){
            if(winding != 0u)
                inOutFlags |= NWB_CSG_RECEIVER_SPAN_AMBIGUOUS_FLAG;
            winding = 0u;
            spanReceiverIndex = surfaceEvent.x;
        }
        const float depth = asfloat(surfaceEvent.z);
        const bool isFrontFace = surfaceEvent.y == NWB_CSG_RECEIVER_EVENT_FACE_FRONT;

        if(winding == 0u){
            if(isFrontFace){
                spanEnterDepth = depth;
                winding = 1u;
            }
            else
                inOutFlags |= NWB_CSG_RECEIVER_SPAN_AMBIGUOUS_FLAG;
            continue;
        }

        if(isFrontFace){
            ++winding;
            continue;
        }

        --winding;
        if(winding != 0u)
            continue;

        if(depth > spanEnterDepth)
            nwbCsgReceiverSpanBuildWriteSpan(
                pixel,
                spanLayerCount,
                spanReceiverIndex,
                spanEnterDepth,
                depth,
                inOutSpanCount,
                inOutFlags
            );
        else
            inOutFlags |= NWB_CSG_RECEIVER_SPAN_AMBIGUOUS_FLAG;
    }

    if(winding != 0u)
        inOutFlags |= NWB_CSG_RECEIVER_SPAN_AMBIGUOUS_FLAG;
}

bool nwbCsgReceiverSpanBuildPairSingleReceiver(
    const uint2 pixel,
    const uint spanLayerCount,
    inout uint inOutSpanCount,
    inout uint inOutFlags
){
    const NwbCsgReceiverSurfaceEvent first = nwbCsgReceiverSpanBuildLoadEvent(pixel, 0u);
    const NwbCsgReceiverSurfaceEvent second = nwbCsgReceiverSpanBuildLoadEvent(pixel, 1u);
    if(
        first.receiverIndex != second.receiverIndex
        || first.faceSign > NWB_CSG_RECEIVER_EVENT_FACE_FRONT
        || second.faceSign > NWB_CSG_RECEIVER_EVENT_FACE_FRONT
    )
        return false;

    // A closed receiver's two events need no local array or sort, regardless of raster arrival order.
    if(first.faceSign == second.faceSign){
        inOutFlags |= NWB_CSG_RECEIVER_SPAN_AMBIGUOUS_FLAG;
        return true;
    }
    const bool firstFront = first.faceSign == NWB_CSG_RECEIVER_EVENT_FACE_FRONT;
    const float enterDepth = firstFront ? first.depth : second.depth;
    const float exitDepth = firstFront ? second.depth : first.depth;
    if(exitDepth > enterDepth)
        nwbCsgReceiverSpanBuildWriteSpan(
            pixel,
            spanLayerCount,
            first.receiverIndex,
            enterDepth,
            exitDepth,
            inOutSpanCount,
            inOutFlags
        );
    else
        inOutFlags |= NWB_CSG_RECEIVER_SPAN_AMBIGUOUS_FLAG;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_CSG_INTERVAL_PEEL_GROUP_SIZE_X, NWB_CSG_INTERVAL_PEEL_GROUP_SIZE_Y, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint2 frameExtent = nwbCsgReceiverSpanBuildFrameExtent();
    const uint2 workExtent = nwbCsgReceiverSpanBuildWorkExtent();
    if(dispatchThreadID.x >= workExtent.x || dispatchThreadID.y >= workExtent.y)
        return;

    const uint2 pixel = dispatchThreadID.xy + nwbCsgReceiverSpanBuildWorkOffset();
    if(pixel.x >= frameExtent.x || pixel.y >= frameExtent.y)
        return;

    uint flags = 0u;
    const uint eventLayerCount = nwbCsgReceiverSpanBuildEventLayerCount();
    const uint spanLayerCount = nwbCsgReceiverSpanBuildSpanLayerCount();
    const uint eventCount = nwbCsgReceiverSpanBuildLoadEventCount(pixel, eventLayerCount, flags);

    uint spanCount = 0u;
    if(eventCount == 2u && nwbCsgReceiverSpanBuildPairSingleReceiver(pixel, spanLayerCount, spanCount, flags)){
        g_NwbCsgReceiverSpanCount[uint3(pixel, 0u)] = uint2(spanCount, flags);
        return;
    }

    // Preserve all receiver/face bits and FP32 depth bits without aggregate-array copies.
    uint3 events[NWB_CSG_RECEIVER_EVENT_LAYER_COUNT];
    [loop]
    for(uint eventIndex = 0u; eventIndex < eventCount; ++eventIndex)
        events[eventIndex] = g_NwbCsgReceiverEventData.Load(int3(int2(pixel), int(eventIndex))).xyz;

    nwbCsgReceiverSpanBuildSortEvents(events, eventCount);

    nwbCsgReceiverSpanBuildPairEvents(pixel, spanLayerCount, events, eventCount, spanCount, flags);

    g_NwbCsgReceiverSpanCount[uint3(pixel, 0u)] = uint2(spanCount, flags);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

