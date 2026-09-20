// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_MESH_SHADER_EMULATION_COMPUTE 1
#define NWB_MESH_EMULATION_INDEXED_OUTPUT 1
#define NWB_CSG_ENABLED 0

#include <mesh/runtime.slangi>
#include <mesh/object_geometry_constants.h>
#include <mesh/object_vertex.slangi>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The native fixture serializes the production vertex transform into the legacy 64-byte transport for exact comparison.
[numthreads(64, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint vertexIndex = dispatchThreadID.x;
    if(vertexIndex >= g_NwbMeshPushConstants.dispatch[NWB_MESH_PUSH_DISPATCH_MATERIAL_CONSTANT_BYTE_OFFSET])
        return;
    const ByteAddressBuffer cache = NwbHeapRawBuffer(nwbMeshFrameMaterialTypedHeapSlot());
    const uint offset = vertexIndex * NWB_MESH_OBJECT_VERTEX_BYTE_SIZE;
    const float4 position = asfloat(cache.Load4(offset + NWB_MESH_OBJECT_VERTEX_POSITION_BYTE_OFFSET));
    const half4 normal = nwbMeshUnpackGeneratedHalf4(cache.Load2(offset + NWB_MESH_OBJECT_VERTEX_NORMAL_BYTE_OFFSET));
    const half4 tangent = nwbMeshUnpackGeneratedHalf4(cache.Load2(offset + NWB_MESH_OBJECT_VERTEX_TANGENT_BYTE_OFFSET));
    const float2 uv0 = asfloat(cache.Load2(offset + NWB_MESH_OBJECT_VERTEX_UV0_BYTE_OFFSET));
    const half4 color = nwbMeshUnpackGeneratedHalf4(cache.Load2(offset + NWB_MESH_OBJECT_VERTEX_COLOR_BYTE_OFFSET));
    nwbMeshGeneratedVertices()[vertexIndex] = nwbMeshTransformObjectVertex(
        position, float3(normal.xyz), float4(tangent), uv0, color, nwbMeshLoadInstance()
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

