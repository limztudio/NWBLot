// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "mesh/authoring.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NwbMeshGeneratedVertex nwbMeshBuildVertex(
    const uint meshletIndex,
    const uint localVertexIndex,
    const NwbMeshSourceVertex source,
    const NwbMeshInstanceData instance){
    const float4 clipPosition = source.clipPosition + float4(3.0, 0.0, 0.0, 0.0);
    return nwbMeshBuildGeneratedVertex(
        clipPosition,
        nwbMeshTransformDirection(source.normal, instance),
        float4(nwbMeshTransformDirection(source.tangent.xyz, instance), source.tangent.w),
        source.uv0,
        source.color,
        source.worldPosition
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

