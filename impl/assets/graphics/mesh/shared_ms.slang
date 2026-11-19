// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_MESH_EMULATION_INDEXED_OUTPUT
#define NWB_MESH_EMULATION_INDEXED_OUTPUT (!NWB_CSG_ENABLED)
#endif

#include "mesh/authoring.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NwbMeshGeneratedVertex nwbMeshBuildVertex(const uint triangleIndex, const uint corner, const NwbMeshSourceVertex source, const NwbMeshInstanceData instance){
    return nwbMeshBuildGeneratedVertex(source.clipPosition, nwbMeshTransformDirection(source.normal, instance), float4(nwbMeshTransformDirection(source.tangent.xyz, instance), source.tangent.w), source.uv0, source.color, source.worldPosition);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

