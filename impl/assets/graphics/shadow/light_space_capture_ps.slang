// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "light_space_resources.slangi"
#include "light_space_capture_io.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void main(NwbLightSpaceCaptureVertex input, uint primitive : SV_PrimitiveID){
    const NwbLightSpaceView view = nwbLightSpaceLoadView(g_NwbLightSpacePush.viewIndex);
    const uint2 pixel = uint2(input.position.xy);
    if(view.depthRange.w == 0.0 || any(pixel >= view.map.xy))
        discard;
    const uint pixelIndex = nwbLightSpacePixelIndex(view, pixel);
    uint record;
    g_NwbHeapStorageBuffers[g_NwbLightSpacePush.countsSlot].InterlockedAdd(pixelIndex * NWB_LIGHT_SPACE_PIXEL_WORD_BYTES, 1u, record);
    if(record >= NWB_LIGHT_SPACE_EVENTS_PER_TEXEL)
        return;
    const float depth = nwbLightSpaceLinearDepth(view, input.worldPosition);
    const uint address = (pixelIndex * NWB_LIGHT_SPACE_EVENTS_PER_TEXEL + record) * NWB_LIGHT_SPACE_EVENT_BYTES;
    g_NwbHeapStorageBuffers[g_NwbLightSpacePush.eventsSlot].Store4(address, uint4(asuint(depth), 0u, 0u, g_NwbLightSpacePush.instanceIndex));
    g_NwbHeapStorageBuffers[g_NwbLightSpacePush.eventsSlot].Store(address + NWB_LIGHT_SPACE_EVENT_TAIL_OFFSET, input.primitiveBase + primitive);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

