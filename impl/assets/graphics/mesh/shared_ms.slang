// The single engine-owned mesh shader, shared by every material. It is pure geometry: meshlet decode,
// skinning/transform, culling, raster emit -- none of which is material-specific. It outputs the mesh's own
// source vertex color verbatim; a material's color/albedo is applied per-pixel in its surface hook (which
// reads the typed .bind material constants), so this shader needs no material interface.
#include "mesh/authoring.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NwbMeshGeneratedVertex nwbMeshBuildVertex(
    const uint triangleIndex,
    const uint corner,
    const NwbMeshSourceVertex source,
    const NwbMeshInstanceData instance
){
    return nwbMeshBuildGeneratedVertex(
        source.clipPosition,
        nwbMeshTransformDirection(source.normal, instance),
        float4(nwbMeshTransformDirection(source.tangent.xyz, instance), source.tangent.w),
        source.uv0,
        source.color,
        source.worldPosition
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

