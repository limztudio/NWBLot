// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "raster_fields.slangi"
#include "runtime_constants.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NwbMeshEmulationVertexInput{
    [[vk::location(NWB_MESH_EMULATION_VERTEX_POSITION_LOCATION)]] float4 position : POSITION0;
    [[vk::location(NWB_MESH_EMULATION_VERTEX_NORMAL_LOCATION)]] half4 normal : NORMAL0;
    [[vk::location(NWB_MESH_EMULATION_VERTEX_TANGENT_LOCATION)]] half4 tangent : TANGENT0;
    [[vk::location(NWB_MESH_EMULATION_VERTEX_UV0_LOCATION)]] float2 uv0 : TEXCOORD0;
    [[vk::location(NWB_MESH_EMULATION_VERTEX_COLOR_LOCATION)]] half4 color : COLOR0;
    [[vk::location(NWB_MESH_EMULATION_VERTEX_WORLD_POSITION_LOCATION)]] float4 worldPosition : TEXCOORD1;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NwbMeshRasterVertex main(NwbMeshEmulationVertexInput input){
    return nwbMeshBuildRasterVertex(
        input.position,
        input.color,
        float3(input.normal.xyz),
        float4(input.tangent),
        input.uv0,
        input.worldPosition
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

