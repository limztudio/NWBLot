// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "binding_slots.h"
#include "constants.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NwbCsgReceiverSurfaceEvent{
    float depth;
    uint receiverIndex;
    uint faceSign;
    uint flags;
};

[[vk::binding(NWB_CSG_INTERVAL_BINDING_RECEIVER_EVENT_DEPTH, NWB_CSG_INTERVAL_SET)]]
Texture2DArray<float> g_NwbCsgReceiverEventDepth;

[[vk::binding(NWB_CSG_INTERVAL_BINDING_RECEIVER_EVENT_DATA, NWB_CSG_INTERVAL_SET)]]
Texture2DArray<uint4> g_NwbCsgReceiverEventData;

[[vk::binding(NWB_CSG_INTERVAL_BINDING_RECEIVER_EVENT_COUNT, NWB_CSG_INTERVAL_SET)]]
Texture2DArray<uint> g_NwbCsgReceiverEventCount;

[[vk::binding(NWB_CSG_INTERVAL_BINDING_RECEIVER_EVENT_FLAGS, NWB_CSG_INTERVAL_SET)]]
Texture2DArray<uint> g_NwbCsgReceiverEventFlags;

[[vk::binding(NWB_CSG_INTERVAL_BINDING_RECEIVER_SPAN_DEPTH, NWB_CSG_INTERVAL_SET), vk::image_format(NWB_CSG_RECEIVER_SPAN_DEPTH_IMAGE_FORMAT)]]
RWTexture2DArray<float2> g_NwbCsgReceiverSpanDepth;

[[vk::binding(NWB_CSG_INTERVAL_BINDING_RECEIVER_SPAN_DATA, NWB_CSG_INTERVAL_SET), vk::image_format(NWB_CSG_RECEIVER_SPAN_DATA_IMAGE_FORMAT)]]
RWTexture2DArray<uint4> g_NwbCsgReceiverSpanData;

[[vk::binding(NWB_CSG_INTERVAL_BINDING_RECEIVER_SPAN_COUNT, NWB_CSG_INTERVAL_SET), vk::image_format(NWB_CSG_RECEIVER_SPAN_COUNT_IMAGE_FORMAT)]]
RWTexture2DArray<uint> g_NwbCsgReceiverSpanCount;

[[vk::binding(NWB_CSG_INTERVAL_BINDING_RECEIVER_SPAN_FLAGS, NWB_CSG_INTERVAL_SET), vk::image_format(NWB_CSG_RECEIVER_SPAN_FLAGS_IMAGE_FORMAT)]]
RWTexture2DArray<uint> g_NwbCsgReceiverSpanFlags;


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
    g_NwbCsgReceiverEventDepth.GetDimensions(width, height, layerCount);
    return min(layerCount, NWB_CSG_RECEIVER_EVENT_LAYER_COUNT);
}

uint nwbCsgReceiverSpanBuildSpanLayerCount(){
    uint width = 1u;
    uint height = 1u;
    uint layerCount = 1u;
    g_NwbCsgReceiverSpanDepth.GetDimensions(width, height, layerCount);
    return min(layerCount, NWB_CSG_RECEIVER_SPAN_LAYER_COUNT);
}

uint nwbCsgReceiverSpanBuildLoadEventCount(const uint2 pixel, const uint eventLayerCount, inout uint inOutFlags){
    const uint eventCount = g_NwbCsgReceiverEventCount.Load(int4(int2(pixel), 0, 0));
    const uint eventFlags = g_NwbCsgReceiverEventFlags.Load(int4(int2(pixel), 0, 0));
    if(eventCount > eventLayerCount || (eventFlags & NWB_CSG_RECEIVER_EVENT_OVERFLOW_FLAG) != 0u)
        inOutFlags |= NWB_CSG_RECEIVER_SPAN_EVENT_OVERFLOW_FLAG;

    return min(eventCount, eventLayerCount);
}

NwbCsgReceiverSurfaceEvent nwbCsgReceiverSpanBuildLoadEvent(const uint2 pixel, const uint eventIndex){
    const int4 texel = int4(int2(pixel), int(eventIndex), 0);
    const uint4 data = g_NwbCsgReceiverEventData.Load(texel);

    NwbCsgReceiverSurfaceEvent result;
    result.depth = g_NwbCsgReceiverEventDepth.Load(texel);
    result.receiverIndex = data.x;
    result.faceSign = data.y;
    result.flags = data.z;
    return result;
}

void nwbCsgReceiverSpanBuildSortEvents(
    inout NwbCsgReceiverSurfaceEvent events[NWB_CSG_RECEIVER_EVENT_LAYER_COUNT],
    const uint eventCount
){
    [unroll]
    for(uint eventIndex = 1u; eventIndex < NWB_CSG_RECEIVER_EVENT_LAYER_COUNT; ++eventIndex){
        if(eventIndex >= eventCount)
            return;

        const NwbCsgReceiverSurfaceEvent candidate = events[eventIndex];
        int insertIndex = int(eventIndex) - 1;
        while(insertIndex >= 0 && candidate.depth < events[uint(insertIndex)].depth){
            events[uint(insertIndex) + 1u] = events[uint(insertIndex)];
            --insertIndex;
        }
        events[uint(insertIndex + 1)] = candidate;
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
    g_NwbCsgReceiverSpanDepth[texel] = float2(enterDepth, exitDepth);
    g_NwbCsgReceiverSpanData[texel] = uint4(receiverIndex, 0u, 0u, 0u);
}

void nwbCsgReceiverSpanBuildPairEvents(
    const uint2 pixel,
    const uint spanLayerCount,
    const NwbCsgReceiverSurfaceEvent events[NWB_CSG_RECEIVER_EVENT_LAYER_COUNT],
    const uint eventCount,
    inout uint inOutSpanCount,
    inout uint inOutFlags
){
    uint usedEvents[NWB_CSG_RECEIVER_EVENT_LAYER_COUNT];
    [unroll]
    for(uint eventIndex = 0u; eventIndex < NWB_CSG_RECEIVER_EVENT_LAYER_COUNT; ++eventIndex)
        usedEvents[eventIndex] = 0u;

    [unroll]
    for(uint eventIndex = 0u; eventIndex < NWB_CSG_RECEIVER_EVENT_LAYER_COUNT; ++eventIndex){
        if(eventIndex >= eventCount)
            return;
        if(usedEvents[eventIndex] != 0u)
            continue;

        const NwbCsgReceiverSurfaceEvent enterEvent = events[eventIndex];
        if(enterEvent.faceSign != NWB_CSG_RECEIVER_EVENT_FACE_FRONT){
            inOutFlags |= NWB_CSG_RECEIVER_SPAN_AMBIGUOUS_FLAG;
            usedEvents[eventIndex] = 1u;
            continue;
        }

        bool foundExit = false;
        uint exitIndex = eventIndex;
        [unroll]
        for(uint candidateIndex = eventIndex + 1u; candidateIndex < NWB_CSG_RECEIVER_EVENT_LAYER_COUNT; ++candidateIndex){
            if(candidateIndex >= eventCount)
                break;
            if(usedEvents[candidateIndex] != 0u)
                continue;

            const NwbCsgReceiverSurfaceEvent exitEvent = events[candidateIndex];
            if(exitEvent.receiverIndex != enterEvent.receiverIndex)
                continue;
            if(exitEvent.faceSign != NWB_CSG_RECEIVER_EVENT_FACE_BACK)
                continue;

            foundExit = true;
            exitIndex = candidateIndex;
            break;
        }

        if(!foundExit){
            inOutFlags |= NWB_CSG_RECEIVER_SPAN_AMBIGUOUS_FLAG;
            usedEvents[eventIndex] = 1u;
            continue;
        }

        const NwbCsgReceiverSurfaceEvent exitEvent = events[exitIndex];
        usedEvents[eventIndex] = 1u;
        usedEvents[exitIndex] = 1u;
        if(exitEvent.depth <= enterEvent.depth){
            inOutFlags |= NWB_CSG_RECEIVER_SPAN_AMBIGUOUS_FLAG;
            continue;
        }

        nwbCsgReceiverSpanBuildWriteSpan(
            pixel,
            spanLayerCount,
            enterEvent.receiverIndex,
            enterEvent.depth,
            exitEvent.depth,
            inOutSpanCount,
            inOutFlags
        );
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_CSG_INTERVAL_PEEL_GROUP_SIZE_X, NWB_CSG_INTERVAL_PEEL_GROUP_SIZE_Y, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint2 frameExtent = nwbCsgReceiverSpanBuildFrameExtent();
    if(dispatchThreadID.x >= frameExtent.x || dispatchThreadID.y >= frameExtent.y)
        return;

    const uint2 pixel = dispatchThreadID.xy;
    uint flags = 0u;
    const uint eventLayerCount = nwbCsgReceiverSpanBuildEventLayerCount();
    const uint spanLayerCount = nwbCsgReceiverSpanBuildSpanLayerCount();
    const uint eventCount = nwbCsgReceiverSpanBuildLoadEventCount(pixel, eventLayerCount, flags);

    NwbCsgReceiverSurfaceEvent events[NWB_CSG_RECEIVER_EVENT_LAYER_COUNT];
    [unroll]
    for(uint eventIndex = 0u; eventIndex < NWB_CSG_RECEIVER_EVENT_LAYER_COUNT; ++eventIndex){
        if(eventIndex < eventCount)
            events[eventIndex] = nwbCsgReceiverSpanBuildLoadEvent(pixel, eventIndex);
    }

    nwbCsgReceiverSpanBuildSortEvents(events, eventCount);

    uint spanCount = 0u;
    nwbCsgReceiverSpanBuildPairEvents(pixel, spanLayerCount, events, eventCount, spanCount, flags);

    g_NwbCsgReceiverSpanCount[uint3(pixel, 0u)] = spanCount;
    g_NwbCsgReceiverSpanFlags[uint3(pixel, 0u)] = flags;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

