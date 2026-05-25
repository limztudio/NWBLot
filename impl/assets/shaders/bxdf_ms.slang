// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "mesh_shader_authoring.slangi"
#include "engine/material_interfaces/bxdf.bind"
#include "bxdf.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


float4 nwbProjectBuildClipPosition(float3 worldPosition){
    return nwbMeshTransformWorldToClip(worldPosition);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NwbMeshGeneratedVertex nwbMeshBuildVertex(
    uint triangleIndex,
    uint corner,
    NwbMeshSourceVertex source,
    const NwbMeshInstanceData instance
){
    NwbMeshGeneratedVertex generatedVertex;
    const float3 worldPosition = nwbMeshTransformPosition(source.position, instance);
    generatedVertex.position = nwbProjectBuildClipPosition(worldPosition);
    generatedVertex.normal = nwbMeshTransformDirection(source.normal, instance);
    generatedVertex.padding0 = 0.0;
    generatedVertex.tangent = float4(nwbMeshTransformDirection(source.tangent.xyz, instance), source.tangent.w);
    generatedVertex.uv0 = source.uv0;
    generatedVertex.padding1 = float2(0.0);
    const NwbProjectBxdfSurfaceMaterial material = nwbMaterialBindLoadSurface(instance);
    const float4 tintedColor = material.base_color * material.color_tint;
    generatedVertex.color = float4(
        clamp(nwbProjectBxdfVertex(tintedColor.rgb), float3(0.0), float3(1.0)),
        tintedColor.a
    );
    generatedVertex.worldPosition = float4(worldPosition, 1.0);
    return generatedVertex;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

