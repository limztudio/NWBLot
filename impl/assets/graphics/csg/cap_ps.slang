// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_CSG_MESH_INSTANCE_INDEX 0u

#include "../mesh/gbuffer_io.slangi"
#include "runtime.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NwbMeshGBufferOutput main(NwbMeshPixelInput input){
    const uint receiverIndex = uint(input.worldPosition.w + 0.5);
    const uint cutterIndex = uint(input.tangent.w + 0.5);
    const float3 worldPosition = input.worldPosition.xyz;
    if(nwbCsgReceiverPixelDiscardExcept(receiverIndex, worldPosition, cutterIndex))
        discard;

    nwbWriteMeshGBuffer(input.color.rgb, input.normal, worldPosition);
    return nwbBuildMeshGBufferOutput();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

