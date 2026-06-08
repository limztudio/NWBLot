// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../mesh/raster_fields.slangi"
#include "runtime.slangi"

#define NWB_CSG_RECEIVER_SURFACE_MASK_WRITABLE 1
#include "interval_sample.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NwbCsgReceiverSurfacePixelInput{
    float4 position : SV_Position;
    bool frontFace : SV_IsFrontFace;
    NWB_MESH_RASTER_PAYLOAD_FIELDS
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[earlydepthstencil]
void main(const NwbCsgReceiverSurfacePixelInput input){
    const uint receiverIndex = nwbCsgMeshInstanceIndex();
    if(input.frontFace){
        const float3 worldPositionDx = ddx(input.worldPosition.xyz);
        const float3 worldPositionDy = ddy(input.worldPosition.xyz);
        const float worldDistanceTolerance = max(length(worldPositionDx), length(worldPositionDy)) * 0.5;
        if(nwbCsgReceiverPixelDiscardWithWorldTolerance(receiverIndex, input.worldPosition.xyz, worldDistanceTolerance))
            nwbCsgMarkReceiverOpeningSurface(input.position, receiverIndex);
    }else{
        nwbCsgMarkReceiverBackSurface(input.position, receiverIndex);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

