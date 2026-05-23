// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "mesh_raster_fields.slangi"


struct NwbMeshEmulationVertexInput{
    [[vk::location(0)]] float4 position : POSITION0;
    [[vk::location(1)]] float3 normal : NORMAL0;
    [[vk::location(2)]] float4 tangent : TANGENT0;
    [[vk::location(3)]] float2 uv0 : TEXCOORD0;
    [[vk::location(4)]] float4 color : COLOR0;
    [[vk::location(5)]] float4 worldPosition : TEXCOORD1;
};

struct NwbMeshEmulationVertexOutput{
    float4 position : SV_Position;
    NWB_MESH_RASTER_PAYLOAD_FIELDS
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NwbMeshEmulationVertexOutput main(NwbMeshEmulationVertexInput input){
    NwbMeshEmulationVertexOutput output;
    output.position = input.position;
    output.color = input.color;
    output.normal = input.normal;
    output.tangent = input.tangent;
    output.uv0 = input.uv0;
    output.worldPosition = input.worldPosition.xyz;
    return output;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

