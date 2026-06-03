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


NwbMeshGBufferOutput main(NwbMeshPixelInput input){
    const uint receiverIndex = uint(input.worldPosition.w + 0.5);
    const uint openingMaskValue = g_NwbCsgCapProxyOpeningMask.Load(int3(uint2(input.position.xy), 0));
    if(openingMaskValue != receiverIndex + 1u)
        discard;

    const uint cutterIndex = uint(input.tangent.w + 0.5);
    const float3 worldPosition = input.worldPosition.xyz;
    if(nwbCsgReceiverPixelDiscardExcept(receiverIndex, worldPosition, cutterIndex))
        discard;

    nwbWriteMeshGBuffer(input.color.rgb, input.normal, worldPosition);
    return nwbBuildMeshGBufferOutput();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
