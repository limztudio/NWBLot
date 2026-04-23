// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#version 460

#include "mesh_shader_authoring.glsli"
#include "bxdf.glsli"

vec4 nwbEngineBuildClipPosition(vec3 worldPosition){
    return nwbMeshTransformWorldToClip(worldPosition);
}

NWB_MESH_BUILD_VERTEX_SIGNATURE{
    NwbMeshGeneratedVertex generatedVertex;
    const vec3 worldPosition = nwbMeshTransformPosition(source.position);
    generatedVertex.position = nwbEngineBuildClipPosition(worldPosition);
    generatedVertex.normal = nwbMeshTransformDirection(source.normal);
    generatedVertex.padding0 = 0.0;
    generatedVertex.tangent = vec4(nwbMeshTransformDirection(source.tangent.xyz), source.tangent.w);
    generatedVertex.uv0 = source.uv0;
    generatedVertex.padding1 = vec2(0.0);
    generatedVertex.color = vec4(clamp(nwbProjectBxdfVertex(source.color.rgb), vec3(0.0), vec3(1.0)), source.color.a);
    generatedVertex.worldPosition = vec4(worldPosition, 1.0);
    return generatedVertex;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

