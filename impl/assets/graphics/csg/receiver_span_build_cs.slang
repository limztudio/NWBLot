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

struct NwbCsgIntervalDispatchPushConstants{
    uint4 frame;
    uint4 workRegion;
};

[[vk::push_constant]]
ConstantBuffer<NwbCsgIntervalDispatchPushConstants> g_NwbCsgIntervalDispatchPushConstants;

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

uint2 nwbCsgReceiverSpanBuildWorkOffset(){
    return uint2(
        g_NwbCsgIntervalDispatchPushConstants.workRegion[NWB_CSG_INTERVAL_DISPATCH_PUSH_WORK_OFFSET_X],
        g_NwbCsgIntervalDispatchPushConstants.workRegion[NWB_CSG_INTERVAL_DISPATCH_PUSH_WORK_OFFSET_Y]
    );
}

uint2 nwbCsgReceiverSpanBuildWorkExtent(){
    return uint2(
        g_NwbCsgIntervalDispatchPushConstants.workRegion[NWB_CSG_INTERVAL_DISPATCH_PUSH_WORK_EXTENT_X],
        g_NwbCsgIntervalDispatchPushConstants.workRegion[NWB_CSG_INTERVAL_DISPATCH_PUSH_WORK_EXTENT_Y]
    );
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
    // Build receiver spans from the depth-sorted surface events with a winding/parity walk instead of
    // pairing each front face to a back face. A receiver depth is inside the solid wherever more front
    // faces than back faces have been crossed; a span runs from the front face that raises the winding
    // count off zero to the back face that returns it to zero. This merges nested/overlapping layers of
    // the same receiver into the correct enclosing span (so a cap can never be validated against a
    // mismatched layer and protrude outside the body), while a simple convex receiver still yields the
    // identical single [front, back] span. Events from other receivers interleaved inside an open span
    // are skipped; an unmatched back face or a still-open winding at the end is flagged ambiguous.
    uint winding = 0u;
    float spanEnterDepth = 0.0;
    uint spanReceiverIndex = 0u;
    [loop]
    for(uint eventIndex = 0u; eventIndex < NWB_CSG_RECEIVER_EVENT_LAYER_COUNT; ++eventIndex){
        if(eventIndex >= eventCount)
            break;

        const NwbCsgReceiverSurfaceEvent surfaceEvent = events[eventIndex];
        const bool isFrontFace = surfaceEvent.faceSign == NWB_CSG_RECEIVER_EVENT_FACE_FRONT;

        if(winding == 0u){
            if(isFrontFace){
                spanEnterDepth = surfaceEvent.depth;
                spanReceiverIndex = surfaceEvent.receiverIndex;
                winding = 1u;
            }
            else{
                inOutFlags |= NWB_CSG_RECEIVER_SPAN_AMBIGUOUS_FLAG;
            }
            continue;
        }

        if(surfaceEvent.receiverIndex != spanReceiverIndex)
            continue;

        if(isFrontFace){
            ++winding;
            continue;
        }

        --winding;
        if(winding != 0u)
            continue;

        if(surfaceEvent.depth > spanEnterDepth)
            nwbCsgReceiverSpanBuildWriteSpan(
                pixel,
                spanLayerCount,
                spanReceiverIndex,
                spanEnterDepth,
                surfaceEvent.depth,
                inOutSpanCount,
                inOutFlags
            );
        else
            inOutFlags |= NWB_CSG_RECEIVER_SPAN_AMBIGUOUS_FLAG;
    }

    if(winding != 0u)
        inOutFlags |= NWB_CSG_RECEIVER_SPAN_AMBIGUOUS_FLAG;
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

