// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../mesh/raster_fields.slangi"
#include "pixel_runtime.slangi"

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
    nwbCsgMarkReceiverSurfaceEvent(input.position, input.worldPosition.xyz, receiverIndex, input.frontFace);
    if(input.frontFace){
        if(nwbCsgReceiverPixelDiscard(receiverIndex, input.worldPosition.xyz))
            nwbCsgMarkReceiverOpeningSurface(input.position, receiverIndex);
    }else{
        nwbCsgMarkReceiverBackSurface(input.position, receiverIndex);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

