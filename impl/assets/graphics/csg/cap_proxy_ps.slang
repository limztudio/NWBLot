// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_CSG_MESH_INSTANCE_INDEX 0u

#include "binding_slots.h"

#include "../mesh/gbuffer_io.slangi"
#include "../mesh/view_buffer.slangi"
#include "runtime.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[vk::binding(NWB_CSG_BINDING_CAP_PROXY_OPENING_MASK, NWB_CSG_CAP_PROXY_OPENING_MASK_SET)]]
Texture2D<uint> g_NwbCsgCapProxyOpeningMask;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NwbCsgCapProxyGBufferOutput{
    [[vk::location(NWB_MESH_GBUFFER_BASE_COLOR_LOCATION)]] float4 baseColor : SV_Target0;
    [[vk::location(NWB_MESH_GBUFFER_NORMAL_LOCATION)]] float4 normal : SV_Target1;
    [[vk::location(NWB_MESH_GBUFFER_WORLD_POSITION_LOCATION)]] float4 worldPosition : SV_Target2;
    float depth : SV_Depth;
};

float nwbCsgCapProxyDepth(const float3 worldPosition){
    const float4 clipPosition = mul(g_NwbMeshWorldToClip, float4(worldPosition, 1.0));
    return clipPosition.z / clipPosition.w;
}

NwbCsgCapProxyGBufferOutput nwbBuildCsgCapProxyGBufferOutput(const float3 capWorldPosition){
    const NwbMeshGBufferOutput gbufferOutput = nwbBuildMeshGBufferOutput();

    NwbCsgCapProxyGBufferOutput output;
    output.baseColor = gbufferOutput.baseColor;
    output.normal = gbufferOutput.normal;
    output.worldPosition = gbufferOutput.worldPosition;
    output.depth = nwbCsgCapProxyDepth(capWorldPosition);
    return output;
}

bool nwbCsgCapProxyOpeningMaskMatches(const uint2 pixelPosition, const uint expectedOpeningMaskValue){
    return g_NwbCsgCapProxyOpeningMask.Load(int3(pixelPosition, 0)) == expectedOpeningMaskValue;
}

NwbCsgCapProxyGBufferOutput main(NwbMeshPixelInput input){
    const uint receiverIndex = uint(input.worldPosition.w + 0.5);
    const uint cutterIndex = uint(input.tangent.w + 0.5);
    const float3 worldPosition = input.worldPosition.xyz;
    const NwbCsgCutter cutter = g_NwbCsgCutters[cutterIndex];
    const float3 capWorldPosition = nwbCsgProjectWorldPositionToCutterSurface(worldPosition, cutter);

    const uint expectedOpeningMaskValue = nwbCsgPackOpeningMask(receiverIndex, cutterIndex);
    if(
        expectedOpeningMaskValue == NWB_CSG_OPENING_MASK_INVALID_VALUE
        || !nwbCsgCapProxyOpeningMaskMatches(uint2(input.position.xy), expectedOpeningMaskValue)
    )
        discard;

    const NwbCsgReceiverRange receiverRange = g_NwbCsgReceiverRanges[receiverIndex];
    if(!nwbCsgReceiverLocalBoundsContains(receiverRange, capWorldPosition))
        discard;

    if(nwbCsgReceiverPixelDiscardExcept(receiverIndex, capWorldPosition, cutterIndex))
        discard;

    const float3 capNormal = nwbCsgFlippedCutterWorldNormal(capWorldPosition, cutter, input.normal);
    nwbWriteMeshGBuffer(input.color.rgb, capNormal, capWorldPosition);
    return nwbBuildCsgCapProxyGBufferOutput(capWorldPosition);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
