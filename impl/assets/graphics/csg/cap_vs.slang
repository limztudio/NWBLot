// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../mesh/raster_fields.slangi"
#include "../mesh/view_buffer.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NwbCsgCapVertexInput{
    [[vk::location(0)]] float4 positionReceiverIndex : POSITION0;
    [[vk::location(1)]] float4 normalCutterIndex : NORMAL0;
    [[vk::location(2)]] float4 tangent : TANGENT0;
    [[vk::location(3)]] float4 color : COLOR0;
    [[vk::location(4)]] float4 uv0 : TEXCOORD0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NwbMeshRasterVertex main(NwbCsgCapVertexInput input){
    const float3 worldPosition = input.positionReceiverIndex.xyz;
    const float4 clipPosition = mul(g_NwbMeshWorldToClip, float4(worldPosition, 1.0));
    return nwbMeshBuildRasterVertex(
        clipPosition,
        input.color,
        input.normalCutterIndex.xyz,
        input.tangent,
        input.uv0.xy,
        float4(worldPosition, input.positionReceiverIndex.w)
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

