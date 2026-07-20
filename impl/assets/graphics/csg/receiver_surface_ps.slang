#include "../mesh/raster_fields.slangi"
#include "pixel_runtime.slangi"

#define NWB_CSG_RECEIVER_EVENTS_WRITABLE 1
#include "interval_sample.slangi"
#undef NWB_CSG_RECEIVER_EVENTS_WRITABLE


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NwbCsgReceiverSurfacePixelInput{
    float4 position : SV_Position;
    bool frontFace : SV_IsFrontFace;
    NWB_MESH_RASTER_PAYLOAD_FIELDS
};

struct NwbCsgReceiverSurfacePixelOutput{
    [[vk::location(0)]] half4 color : SV_Target0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[earlydepthstencil]
NwbCsgReceiverSurfacePixelOutput main(const NwbCsgReceiverSurfacePixelInput input){
    const uint receiverIndex = nwbCsgMeshInstanceIndex();
    nwbCsgMarkReceiverSurfaceEvent(input.position, input.worldPosition.xyz, receiverIndex, input.frontFace);

    NwbCsgReceiverSurfacePixelOutput output;
    output.color = half4(0.0);
    return output;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

