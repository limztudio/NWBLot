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
    const NwbMeshInstanceData instance = nwbMeshLoadInstance();
    const vec3 worldPosition = nwbMeshTransformPosition(source.position, instance);
    generatedVertex.position = nwbEngineBuildClipPosition(worldPosition);
    generatedVertex.normal = nwbMeshTransformDirection(source.normal, instance);
    generatedVertex.padding0 = 0.0;
    generatedVertex.tangent = vec4(nwbMeshTransformDirection(source.tangent.xyz, instance), source.tangent.w);
    generatedVertex.uv0 = source.uv0;
    generatedVertex.padding1 = vec2(0.0);
    const vec4 tintedColor = source.color * nwbMeshInstanceColorTint(instance);
    generatedVertex.color = vec4(
        clamp(nwbProjectBxdfVertex(tintedColor.rgb), vec3(0.0), vec3(1.0)),
        tintedColor.a
    );
    generatedVertex.worldPosition = vec4(worldPosition, 1.0);
    return generatedVertex;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

