// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../mesh/raster_fields.slangi"
#include "../mesh/view_buffer.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NwbCsgPlaneCapVertexInput{
    [[vk::location(0)]] float4 positionReceiverIndex : POSITION0;
    [[vk::location(1)]] float4 normalCutterIndex : NORMAL0;
    [[vk::location(2)]] float4 color : COLOR0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NwbMeshRasterVertex main(NwbCsgPlaneCapVertexInput input){
    const float3 worldPosition = input.positionReceiverIndex.xyz;
    const float4 clipPosition = mul(g_NwbMeshWorldToClip, float4(worldPosition, 1.0));
    return nwbMeshBuildRasterVertex(
        clipPosition,
        input.color,
        input.normalCutterIndex.xyz,
        float4(0.0, 0.0, 0.0, input.normalCutterIndex.w),
        float2(0.0),
        float4(worldPosition, input.positionReceiverIndex.w)
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

