// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#version 460

#include "mesh_shader_authoring.glsli"
#include "bxdf.glsli"

const uvec2 NWB_ENGINE_MATERIAL_BASE_COLOR = uvec2(0x75bad3d8u, 0x5c10c7e2u);
const uvec2 NWB_ENGINE_MATERIAL_COLOR_TINT = uvec2(0x360abc2cu, 0xca7492edu);

vec4 nwbEngineBuildClipPosition(vec3 worldPosition){
    return nwbMeshTransformWorldToClip(worldPosition);
}

NWB_MESH_BUILD_VERTEX_SIGNATURE{
    NwbMeshGeneratedVertex generatedVertex;
    const vec3 worldPosition = nwbMeshTransformPosition(source.position, instance);
    generatedVertex.position = nwbEngineBuildClipPosition(worldPosition);
    generatedVertex.normal = nwbMeshTransformDirection(source.normal, instance);
    generatedVertex.padding0 = 0.0;
    generatedVertex.tangent = vec4(nwbMeshTransformDirection(source.tangent.xyz, instance), source.tangent.w);
    generatedVertex.uv0 = source.uv0;
    generatedVertex.padding1 = vec2(0.0);
    const mediump vec4 materialColor = nwbMaterialFindFloat4(instance, NWB_ENGINE_MATERIAL_BASE_COLOR, source.color);
    const mediump vec4 tintedColor = materialColor * nwbMaterialFindFloat4(instance, NWB_ENGINE_MATERIAL_COLOR_TINT, vec4(1.0));
    generatedVertex.color = vec4(
        clamp(nwbProjectBxdfVertex(tintedColor.rgb), vec3(0.0), vec3(1.0)),
        tintedColor.a
    );
    generatedVertex.worldPosition = vec4(worldPosition, 1.0);
    return generatedVertex;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

