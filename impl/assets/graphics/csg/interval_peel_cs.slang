// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_CSG_MESH_INSTANCE_INDEX 0u
#include "interval_dispatch.slangi"
#undef NWB_CSG_MESH_INSTANCE_INDEX


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NwbCsgIntervalPeelEntry{
    float frontDepth;
    float backDepth;
    float frontLinearDepth;
    float backLinearDepth;
    half3 backCapNormal;
    uint id;
};

// The standalone peel dispatch has no mesh draw constants. Its view UniformBuffer slot therefore rides in its own
// push payload, while b13 remains an intentional local-layout gap.
#define NWB_MESH_VIEW_BUFFER_HEAP_SLOT (g_NwbCsgIntervalDispatchPushConstants.resourceSlots[NWB_CSG_INTERVAL_DISPATCH_PUSH_RESOURCE_SLOT_VIEW])
#include "../bindless/bindless_heap_mesh.slangi"

// Local bindings 0..2 are ABI gaps. The CSG context selects target-generation Texture2DArray UAVs
// from the StorageImage heap; the device's required formatless-storage-image support permits the two float formats.
#define g_NwbCsgBackCapNormal (NwbHeapStorageImage2DArrayFloat4(g_NwbCsgBindlessResources.csgPeelSlots.x))
#define g_NwbCsgIntervalDepth (NwbHeapStorageImage2DArrayFloat4(g_NwbCsgBindlessResources.csgPeelSlots.y))
#define g_NwbCsgIntervalId (NwbHeapStorageImage2DArrayUint(g_NwbCsgBindlessResources.csgPeelSlots.z))


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


uint nwbCsgIntervalPeelFrameWidth(){
    return g_NwbCsgIntervalDispatchPushConstants.frame[NWB_CSG_INTERVAL_DISPATCH_PUSH_FRAME_WIDTH];
}

uint nwbCsgIntervalPeelFrameHeight(){
    return g_NwbCsgIntervalDispatchPushConstants.frame[NWB_CSG_INTERVAL_DISPATCH_PUSH_FRAME_HEIGHT];
}

uint nwbCsgIntervalPeelReceiverCount(){
    return g_NwbCsgIntervalDispatchPushConstants.frame[NWB_CSG_INTERVAL_DISPATCH_PUSH_RECEIVER_COUNT];
}

uint nwbCsgIntervalPeelLayerCount(){
    return min(g_NwbCsgIntervalDispatchPushConstants.frame[NWB_CSG_INTERVAL_DISPATCH_PUSH_LAYER_COUNT], NWB_CSG_PEEL_LAYER_COUNT);
}

uint2 nwbCsgIntervalPeelWorkOffset(){
    return nwbCsgIntervalDispatchWorkOffset();
}

uint2 nwbCsgIntervalPeelWorkExtent(){
    return nwbCsgIntervalDispatchWorkExtent();
}

float nwbCsgSafeHomogeneousW(const float w){
    if(abs(w) >= NWB_CSG_HOMOGENEOUS_W_EPSILON)
        return w;
    return w < 0.0 ? -NWB_CSG_HOMOGENEOUS_W_EPSILON : NWB_CSG_HOMOGENEOUS_W_EPSILON;
}

float3 nwbCsgClipToWorldPosition(const float4 clipPosition){
    const float4 worldPosition = mul(g_NwbMeshClipToWorld, clipPosition);
    return worldPosition.xyz / nwbCsgSafeHomogeneousW(worldPosition.w);
}

float nwbCsgWorldToDepth(const float3 worldPosition){
    const float4 clipPosition = mul(g_NwbMeshWorldToClip, float4(worldPosition, 1.0));
    return clipPosition.z / nwbCsgSafeHomogeneousW(clipPosition.w);
}

void nwbCsgBuildPixelRay(const uint2 pixel, out float3 outRayBegin, out float3 outRayEnd){
    const float2 frameSize = float2(nwbCsgIntervalPeelFrameWidth(), nwbCsgIntervalPeelFrameHeight());
    const float2 uv = (float2(pixel) + 0.5) / frameSize;
    const float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    outRayBegin = nwbCsgClipToWorldPosition(float4(ndc, 0.0, 1.0));
    outRayEnd = nwbCsgClipToWorldPosition(float4(ndc, 1.0, 1.0));
}

uint nwbCsgPackIntervalId(const uint receiverIndex){
    const uint receiverBits = (receiverIndex + 1u) & NWB_CSG_RECEIVER_INDEX_MASK;
    return receiverBits;
}

NwbCsgIntervalPeelEntry nwbCsgInvalidIntervalPeelEntry(){
    NwbCsgIntervalPeelEntry entry;
    entry.frontDepth = NWB_CSG_INVALID_DISTANCE;
    entry.backDepth = NWB_CSG_INVALID_DISTANCE;
    entry.frontLinearDepth = NWB_CSG_INVALID_DISTANCE;
    entry.backLinearDepth = NWB_CSG_INVALID_DISTANCE;
    entry.backCapNormal = half3(0.0h, 0.0h, 0.0h);
    entry.id = 0u;
    return entry;
}

float nwbCsgSampleCutterDistanceOnRay(
    const float3 rayBegin,
    const float3 rayVector,
    const float t,
    const NwbCsgCutter cutter
){
    return nwbCsgEvaluateDistance(rayBegin + rayVector * t, cutter);
}

bool nwbCsgIntersectRaySlab(
    const float origin,
    const float direction,
    const float minBound,
    const float maxBound,
    inout float inOutMinT,
    inout float inOutMaxT
){
    if(abs(direction) <= NWB_CSG_RAY_PARALLEL_DIRECTION_EPSILON)
        return origin >= minBound && origin <= maxBound;

    const float invDirection = 1.0 / direction;
    float slabMinT = (minBound - origin) * invDirection;
    float slabMaxT = (maxBound - origin) * invDirection;
    if(slabMinT > slabMaxT){
        const float temp = slabMinT;
        slabMinT = slabMaxT;
        slabMaxT = temp;
    }

    inOutMinT = max(inOutMinT, slabMinT);
    inOutMaxT = min(inOutMaxT, slabMaxT);
    return inOutMinT <= inOutMaxT;
}

bool nwbCsgFindReceiverRayRange(
    const NwbCsgReceiverRange receiverRange,
    const float3 rayBegin,
    const float3 rayEnd,
    out float outBeginT,
    out float outEndT
){
    outBeginT = 0.0;
    outEndT = 1.0;

    if(!nwbCsgBoundsValid(receiverRange.localBounds))
        return false;
    if(!nwbCsgBoundsFinite(receiverRange.localBounds))
        return true;

    const float3 localBegin = nwbCsgTransformWorldToReceiverLocal(rayBegin, receiverRange);
    const float3 localEnd = nwbCsgTransformWorldToReceiverLocal(rayEnd, receiverRange);
    const float3 localRay = localEnd - localBegin;
    const float3 minBounds = receiverRange.localBounds.minBounds - float3(NWB_CSG_BOUNDS_EPSILON);
    const float3 maxBounds = receiverRange.localBounds.maxBounds + float3(NWB_CSG_BOUNDS_EPSILON);

    return nwbCsgIntersectRaySlab(localBegin.x, localRay.x, minBounds.x, maxBounds.x, outBeginT, outEndT)
        && nwbCsgIntersectRaySlab(localBegin.y, localRay.y, minBounds.y, maxBounds.y, outBeginT, outEndT)
        && nwbCsgIntersectRaySlab(localBegin.z, localRay.z, minBounds.z, maxBounds.z, outBeginT, outEndT)
    ;
}

float nwbCsgRefineCutterIntersection(
    const float3 rayBegin,
    const float3 rayVector,
    const NwbCsgCutter cutter,
    float lowT,
    float highT,
    const bool entering
){
    [unroll]
    for(uint refineIndex = 0u; refineIndex < NWB_CSG_INTERVAL_PEEL_REFINE_STEP_COUNT; ++refineIndex){
        const float midT = (lowT + highT) * 0.5;
        const float midDistance = nwbCsgSampleCutterDistanceOnRay(rayBegin, rayVector, midT, cutter);
        const bool midInside = midDistance <= 0.0;
        if(midInside == entering)
            highT = midT;
        else{
            lowT = midT;
        }
    }

    return (lowT + highT) * 0.5;
}

bool nwbCsgFindCutterIntervalOnRay(
    const float3 rayBegin,
    const float3 rayEnd,
    const uint receiverIndex,
    const uint cutterIndex,
    out NwbCsgIntervalPeelEntry outEntry
){
    outEntry = nwbCsgInvalidIntervalPeelEntry();

    const NwbCsgReceiverRange receiverRange = g_NwbCsgReceiverRanges[receiverIndex];
    const NwbCsgCutter cutter = g_NwbCsgCutters[cutterIndex];

    const float3 rayVector = rayEnd - rayBegin;
    float beginT;
    float endT;
    if(!nwbCsgFindReceiverRayRange(receiverRange, rayBegin, rayEnd, beginT, endT))
        return false;
    if(endT <= beginT)
        return false;

    const float step = (endT - beginT) / float(NWB_CSG_INTERVAL_PEEL_STEP_COUNT);
    float previousT = beginT;
    bool inside = nwbCsgSampleCutterDistanceOnRay(rayBegin, rayVector, previousT, cutter) <= 0.0;
    bool hasFront = inside;
    float frontT = beginT;

    [loop]
    for(uint stepIndex = 1u; stepIndex <= NWB_CSG_INTERVAL_PEEL_STEP_COUNT; ++stepIndex){
        const float t = beginT + float(stepIndex) * step;
        const float distance = nwbCsgSampleCutterDistanceOnRay(rayBegin, rayVector, t, cutter);
        const bool nextInside = distance <= 0.0;
        if(nextInside != inside){
            const bool entering = nextInside;
            const float hitT = nwbCsgRefineCutterIntersection(rayBegin, rayVector, cutter, previousT, t, entering);
            if(entering){
                frontT = hitT;
                hasFront = true;
            }else if(hasFront){
                const float backT = hitT;
                const float3 frontWorldPosition = rayBegin + rayVector * frontT;
                const float3 backWorldPosition = rayBegin + rayVector * backT;
                const float3 midWorldPosition = rayBegin + rayVector * ((frontT + backT) * 0.5);
                if(!nwbCsgReceiverLocalBoundsContains(receiverRange, midWorldPosition)){
                    hasFront = false;
                    inside = nextInside;
                    previousT = t;
                    continue;
                }

                const float frontDepth = saturate(nwbCsgWorldToDepth(frontWorldPosition));
                const float backDepth = saturate(nwbCsgWorldToDepth(backWorldPosition));
                const float frontLinearDepth = length(frontWorldPosition - g_NwbMeshCameraPosition.xyz);
                const float backLinearDepth = length(backWorldPosition - g_NwbMeshCameraPosition.xyz);
                outEntry.frontDepth = min(frontDepth, backDepth);
                outEntry.backDepth = max(frontDepth, backDepth);
                outEntry.frontLinearDepth = min(frontLinearDepth, backLinearDepth);
                outEntry.backLinearDepth = max(frontLinearDepth, backLinearDepth);
                const float3 viewFacingFallback = normalize(rayBegin - rayEnd);
                outEntry.backCapNormal = half3(nwbCsgFlippedCutterWorldNormal(backWorldPosition, cutter, viewFacingFallback));
                outEntry.id = nwbCsgPackIntervalId(receiverIndex);
                return outEntry.backLinearDepth >= outEntry.frontLinearDepth && outEntry.id != 0u;
            }
        }

        inside = nextInside;
        previousT = t;
    }

    return false;
}

void nwbCsgInsertIntervalPeelEntry(
    inout NwbCsgIntervalPeelEntry entries[NWB_CSG_PEEL_LAYER_COUNT],
    const NwbCsgIntervalPeelEntry candidate
){
    if(candidate.id == 0u)
        return;

    [unroll]
    for(uint layerIndex = 0u; layerIndex < NWB_CSG_PEEL_LAYER_COUNT; ++layerIndex){
        if(candidate.frontLinearDepth >= entries[layerIndex].frontLinearDepth)
            continue;

        NwbCsgIntervalPeelEntry displaced = entries[layerIndex];
        entries[layerIndex] = candidate;
        [unroll]
        for(uint shiftIndex = layerIndex + 1u; shiftIndex < NWB_CSG_PEEL_LAYER_COUNT; ++shiftIndex){
            NwbCsgIntervalPeelEntry next = entries[shiftIndex];
            entries[shiftIndex] = displaced;
            displaced = next;
        }
        return;
    }
}

void nwbCsgWriteIntervalPeelEntries(
    const uint2 pixel,
    const NwbCsgIntervalPeelEntry entries[NWB_CSG_PEEL_LAYER_COUNT]
){
    const uint layerCount = nwbCsgIntervalPeelLayerCount();
    [unroll]
    for(uint layerIndex = 0u; layerIndex < NWB_CSG_PEEL_LAYER_COUNT; ++layerIndex){
        if(layerIndex >= layerCount)
            continue;

        const uint3 texel = uint3(pixel, layerIndex);
        const NwbCsgIntervalPeelEntry entry = entries[layerIndex];
        if(entry.id == 0u)
            continue;

        g_NwbCsgBackCapNormal[texel] = float4(float3(entry.backCapNormal), 1.0);
        g_NwbCsgIntervalDepth[texel] = float4(entry.frontDepth, entry.backDepth, entry.frontLinearDepth, entry.backLinearDepth);
        g_NwbCsgIntervalId[texel] = entry.id;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_CSG_INTERVAL_PEEL_GROUP_SIZE_X, NWB_CSG_INTERVAL_PEEL_GROUP_SIZE_Y, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint frameWidth = nwbCsgIntervalPeelFrameWidth();
    const uint frameHeight = nwbCsgIntervalPeelFrameHeight();
    const uint2 workExtent = nwbCsgIntervalPeelWorkExtent();
    if(dispatchThreadID.x >= workExtent.x || dispatchThreadID.y >= workExtent.y)
        return;

    const uint2 pixel = dispatchThreadID.xy + nwbCsgIntervalPeelWorkOffset();
    if(pixel.x >= frameWidth || pixel.y >= frameHeight)
        return;

    float3 rayBegin;
    float3 rayEnd;
    nwbCsgBuildPixelRay(pixel, rayBegin, rayEnd);

    NwbCsgIntervalPeelEntry entries[NWB_CSG_PEEL_LAYER_COUNT];
    [unroll]
    for(uint layerIndex = 0u; layerIndex < NWB_CSG_PEEL_LAYER_COUNT; ++layerIndex)
        entries[layerIndex] = nwbCsgInvalidIntervalPeelEntry();

    const uint receiverCount = nwbCsgIntervalPeelReceiverCount();
    [loop]
    for(uint receiverIndex = 0u; receiverIndex < receiverCount; ++receiverIndex){
        const NwbCsgReceiverRange receiverRange = g_NwbCsgReceiverRanges[receiverIndex];
        [loop]
        for(uint localCutterIndex = 0u; localCutterIndex < receiverRange.rangeInfo.y; ++localCutterIndex){
            const uint cutterIndex = receiverRange.rangeInfo.x + localCutterIndex;
            NwbCsgIntervalPeelEntry entry;
            if(nwbCsgFindCutterIntervalOnRay(rayBegin, rayEnd, receiverIndex, cutterIndex, entry))
                nwbCsgInsertIntervalPeelEntry(entries, entry);
        }
    }

    nwbCsgWriteIntervalPeelEntries(pixel, entries);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

