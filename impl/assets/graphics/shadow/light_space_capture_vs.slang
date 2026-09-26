// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "light_space_scene.slangi"
#include "light_space_capture_io.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NwbLightSpaceCaptureVertex main(uint vertex : SV_VertexID){
    NwbLightSpaceCaptureVertex output = (NwbLightSpaceCaptureVertex)0;
    output.position = float4(2.0, 2.0, 2.0, 1.0);
    const NwbLightSpaceView view = nwbLightSpaceLoadView(g_NwbLightSpacePush.viewIndex);
    if(view.depthRange.w == 0.0 || g_NwbLightSpacePush.instanceIndex >= g_NwbLightSpacePush.instanceCount)
        return output;
    const NwbRtInstanceMaterial material = nwbLightSpaceMaterial(g_NwbLightSpacePush.instanceIndex);
    // SV_VertexID is the canonical position index fetched by the native index buffer.
    const float3 local = asfloat(NwbHeapRawBuffer(material.positionSlot).Load3(vertex * NWB_RAYTRACE_POSITION_STRIDE_BYTES));
    output.worldPosition = nwbLightSpaceWorldPosition(local, g_NwbLightSpacePush.instanceIndex);
    output.position = nwbLightSpaceProject(view, output.worldPosition);
    return output;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

