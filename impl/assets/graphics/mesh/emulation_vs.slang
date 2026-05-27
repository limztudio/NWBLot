// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "raster_fields.slangi"


struct NwbMeshEmulationVertexInput{
    [[vk::location(0)]] float4 position : POSITION0;
    [[vk::location(1)]] float3 normal : NORMAL0;
    [[vk::location(2)]] float4 tangent : TANGENT0;
    [[vk::location(3)]] float2 uv0 : TEXCOORD0;
    [[vk::location(4)]] float4 color : COLOR0;
    [[vk::location(5)]] float4 worldPosition : TEXCOORD1;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NwbMeshRasterVertex main(NwbMeshEmulationVertexInput input){
    return nwbMeshBuildRasterVertex(
        input.position,
        input.color,
        input.normal,
        input.tangent,
        input.uv0,
        input.worldPosition.xyz
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

