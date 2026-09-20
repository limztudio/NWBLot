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
    const NwbSwBvhInstance instance = nwbLightSpaceInstance(g_NwbLightSpacePush.instanceIndex);
    if(vertex / 3u >= instance.primitiveCount)
        return output;
    const uint positionIndex = NwbHeapRawBuffer(material.indexSlot).Load(vertex * 4u);
    const float3 local = asfloat(NwbHeapRawBuffer(material.positionSlot).Load3(positionIndex * NWB_RAYTRACE_POSITION_STRIDE_BYTES));
    const uint corner = vertex % 3u;
    output.barycentrics = float2(corner == 1u ? 1.0 : 0.0, corner == 2u ? 1.0 : 0.0);
    output.worldPosition = nwbLightSpaceWorldPosition(local, g_NwbLightSpacePush.instanceIndex);
    output.position = nwbLightSpaceProject(view, output.worldPosition);
    return output;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

