// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_CSG_MESH_INSTANCE_INDEX 0u

#include "binding_slots.h"

#include "../mesh/gbuffer_io.slangi"
#include "runtime.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[vk::binding(NWB_CSG_BINDING_CAP_PROXY_OPENING_MASK, NWB_CSG_CAP_PROXY_OPENING_MASK_SET)]]
Texture2D<uint> g_NwbCsgCapProxyOpeningMask;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool nwbCsgCapProxyOpeningMaskMatches(const uint2 pixelPosition, const uint expectedOpeningMaskValue){
    if(g_NwbCsgCapProxyOpeningMask.Load(int3(pixelPosition, 0)) == expectedOpeningMaskValue)
        return true;

    uint width;
    uint height;
    g_NwbCsgCapProxyOpeningMask.GetDimensions(width, height);
    const int searchRadius = 2;
    const int2 pixel = int2(pixelPosition);
    const int2 maxPixel = int2(max(uint2(width, height), uint2(1u, 1u)) - uint2(1u, 1u));
    for(int y = -searchRadius; y <= searchRadius; ++y){
        for(int x = -searchRadius; x <= searchRadius; ++x){
            if(x == 0 && y == 0)
                continue;

            const int2 samplePixel = clamp(pixel + int2(x, y), int2(0, 0), maxPixel);
            if(g_NwbCsgCapProxyOpeningMask.Load(int3(samplePixel, 0)) == expectedOpeningMaskValue)
                return true;
        }
    }

    return false;
}

NwbMeshGBufferOutput main(NwbMeshPixelInput input){
    const uint receiverIndex = uint(input.worldPosition.w + 0.5);
    const uint cutterIndex = uint(input.tangent.w + 0.5);
    const float3 worldPosition = input.worldPosition.xyz;
    const NwbCsgCutter cutter = g_NwbCsgCutters[cutterIndex];

    const uint expectedOpeningMaskValue = nwbCsgPackOpeningMask(receiverIndex, cutterIndex);
    if(
        expectedOpeningMaskValue == NWB_CSG_OPENING_MASK_INVALID_VALUE
        || !nwbCsgCapProxyOpeningMaskMatches(uint2(input.position.xy), expectedOpeningMaskValue)
    )
        discard;

    const NwbCsgReceiverRange receiverRange = g_NwbCsgReceiverRanges[receiverIndex];
    if(!nwbCsgReceiverLocalBoundsContains(receiverRange, worldPosition))
        discard;

    if(nwbCsgReceiverPixelDiscardExcept(receiverIndex, worldPosition, cutterIndex))
        discard;

    const float3 capWorldPosition = nwbCsgProjectWorldPositionToCutterSurface(worldPosition, cutter);
    const float3 capNormal = nwbCsgFlippedCutterWorldNormal(capWorldPosition, cutter, input.normal);
    nwbWriteMeshGBuffer(input.color.rgb, capNormal, capWorldPosition);
    return nwbBuildMeshGBufferOutput();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
