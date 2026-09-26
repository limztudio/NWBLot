// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "light_space_resources.slangi"
#include "light_space_capture_io.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Store only covered geometry. Material evaluation runs once per retained crossing in compute, avoiding helper-lane work on tiny triangles.
void main(NwbLightSpaceCaptureVertex input, uint primitive : SV_PrimitiveID){
    const NwbLightSpaceView view = nwbLightSpaceLoadView(g_NwbLightSpacePush.viewIndex);
    const uint2 pixel = uint2(input.position.xy);
    if(view.depthRange.w == 0.0 || any(pixel >= view.map.xy))
        discard;
    const uint pixelIndex = nwbLightSpacePixelIndex(view, pixel);
    uint record;
    g_NwbHeapStorageBuffers[g_NwbLightSpacePush.countsSlot].InterlockedAdd(pixelIndex * 4u, 1u, record);
    if(record >= NWB_LIGHT_SPACE_EVENTS_PER_TEXEL)
        return;
    const float depth = nwbLightSpaceLinearDepth(view, input.worldPosition);
    // Raw record: captured linear depth, two reserved zeros, instance, canonical primitive.
    const uint address = (pixelIndex * NWB_LIGHT_SPACE_EVENTS_PER_TEXEL + record) * NWB_LIGHT_SPACE_EVENT_BYTES;
    g_NwbHeapStorageBuffers[g_NwbLightSpacePush.eventsSlot].Store4(address,
        uint4(asuint(depth), 0u, 0u, g_NwbLightSpacePush.instanceIndex));
    g_NwbHeapStorageBuffers[g_NwbLightSpacePush.eventsSlot].Store(address + 16u, primitive);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

