// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_MESH_SHADER_EMULATION_COMPUTE 0
#define NWB_MESH_EMULATION_INDEXED_OUTPUT 0
#define NWB_CSG_ENABLED 0

#include "runtime.slangi"
#include "object_vertex.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NwbMeshObjectVertexInput{
    [[vk::location(NWB_MESH_EMULATION_VERTEX_POSITION_LOCATION)]] float4 position : POSITION0;
    [[vk::location(NWB_MESH_EMULATION_VERTEX_NORMAL_LOCATION)]] float4 normal : NORMAL0;
    [[vk::location(NWB_MESH_EMULATION_VERTEX_TANGENT_LOCATION)]] float4 tangent : TANGENT0;
    [[vk::location(NWB_MESH_EMULATION_VERTEX_UV0_LOCATION)]] float2 uv0 : TEXCOORD0;
    [[vk::location(NWB_MESH_EMULATION_VERTEX_COLOR_LOCATION)]] float4 color : COLOR0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NwbMeshRasterVertex main(NwbMeshObjectVertexInput input){
    const NwbMeshGeneratedVertex vertex = nwbMeshTransformObjectVertex(
        input.position, input.normal.xyz, input.tangent, input.uv0, half4(input.color), nwbMeshLoadInstance()
    );
    return nwbMeshBuildRasterVertex(
        vertex.position,
        nwbMeshGeneratedVertexColor(vertex),
        nwbMeshGeneratedVertexNormal(vertex).xyz,
        nwbMeshGeneratedVertexTangent(vertex),
        vertex.uv0,
        vertex.worldPosition
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

