// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "depth_constants.h"
#include "../bindless/bindless_heap.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NwbReflectionDepthReducePushConstants{
#define NWB_REFLECTION_DEPTH_UINT_FIELD(name) uint name;
    NWB_REFLECTION_DEPTH_UINT_FIELDS(NWB_REFLECTION_DEPTH_UINT_FIELD)
#undef NWB_REFLECTION_DEPTH_UINT_FIELD
};
[[vk::push_constant]] ConstantBuffer<NwbReflectionDepthReducePushConstants> g_NwbReflectionDepthPush;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_REFLECTION_DEPTH_GROUP_SIZE, NWB_REFLECTION_DEPTH_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID){
    const uint2 destination = dispatchThreadId.xy;
    const uint2 sourceExtent = uint2(g_NwbReflectionDepthPush.sourceWidth, g_NwbReflectionDepthPush.sourceHeight);
    const uint2 destinationExtent = uint2(g_NwbReflectionDepthPush.destinationWidth, g_NwbReflectionDepthPush.destinationHeight);
    if(any(destination >= destinationExtent))
        return;

    float2 depthInterval = float2(1.0, 0.0);
    if(all(sourceExtent == destinationExtent)){
        // A depth sample represents a pixel center. The neighboring samples conservatively expand its local
        // planar footprint before hierarchy rejection; a center-only range can miss an edge crossing on a slope.
        // Sky remains depth one in both bounds. These are visible-depth bounds, not a volume's entry/exit depths.
        for(int y = -1; y <= 1; ++y){
            for(int x = -1; x <= 1; ++x){
                const int2 source = clamp(int2(destination) + int2(x, y), int2(0), int2(sourceExtent) - 1);
                const float depth = NwbHeapSampledImage2D(g_NwbReflectionDepthPush.sourceSlot).Load(int3(source, 0)).x;
                const float validDepth = isfinite(depth) && depth >= 0.0 ? min(depth, 1.0) : 1.0;
                depthInterval.x = min(depthInterval.x, validDepth);
                depthInterval.y = max(depthInterval.y, validDepth);
            }
        }
        NwbHeapStorageImage2DFloat4(g_NwbReflectionDepthPush.destinationSlot)[destination] = float4(depthInterval, 0.0, 0.0);
        return;
    }

    // Native mip extents floor-halve each axis. An odd source axis contributes three overlapping samples, so no
    // edge texel disappears on reductions such as 45 -> 22. An unchanged axis preserves a 1 -> 1 dimension.
    const uint2 begin = uint2(
        sourceExtent.x == destinationExtent.x ? destination.x : destination.x * 2u,
        sourceExtent.y == destinationExtent.y ? destination.y : destination.y * 2u
    );
    const uint2 span = uint2(
        sourceExtent.x == destinationExtent.x ? 1u : 2u + (sourceExtent.x & 1u),
        sourceExtent.y == destinationExtent.y ? 1u : 2u + (sourceExtent.y & 1u)
    );
    for(uint y = 0u; y < span.y; ++y){
        for(uint x = 0u; x < span.x; ++x){
            // The frozen source descriptor exposes one mip; level zero is relative to that view.
            const float2 interval = NwbHeapSampledImage2D(g_NwbReflectionDepthPush.sourceSlot).Load(int3(begin + uint2(x, y), 0)).xy;
            depthInterval.x = min(depthInterval.x, interval.x);
            depthInterval.y = max(depthInterval.y, interval.y);
        }
    }
    NwbHeapStorageImage2DFloat4(g_NwbReflectionDepthPush.destinationSlot)[destination] = float4(depthInterval, 0.0, 0.0);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

