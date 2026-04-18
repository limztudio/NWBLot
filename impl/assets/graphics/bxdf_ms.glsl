// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#version 460

#include "mesh_shader_authoring.glsli"
#include "bxdf.glsli"

vec4 nwbEngineBuildClipPosition(vec3 worldPosition){
    const vec4 viewRotation = nwbMeshViewRotation();
    const vec4 viewPositionDepthBias = nwbMeshViewPositionDepthBias();

    vec3 p = nwbMeshRotateVectorByQuaternion(worldPosition - viewPositionDepthBias.xyz, viewRotation);
    p.z += viewPositionDepthBias.w;

    const float invZ = 1.0 / max(p.z, 0.0001);
    const float ndcX = p.x * invZ;
    const float ndcY = p.y * invZ;
    const float ndcZ = (p.z - 1.0) * 0.5;
    return vec4(ndcX, ndcY, ndcZ, 1.0);
}

NWB_MESH_BUILD_VERTEX_SIGNATURE{
    NwbMeshGeneratedVertex generatedVertex;
    generatedVertex.position = nwbEngineBuildClipPosition(nwbMeshTransformPosition(source.position));
    generatedVertex.normal = nwbMeshTransformDirection(source.normal);
    generatedVertex.padding0 = 0.0;
    generatedVertex.tangent = vec4(nwbMeshTransformDirection(source.tangent.xyz), source.tangent.w);
    generatedVertex.uv0 = source.uv0;
    generatedVertex.padding1 = vec2(0.0);
    generatedVertex.color = vec4(clamp(nwbProjectBxdfVertex(source.color.rgb), vec3(0.0), vec3(1.0)), source.color.a);
    return generatedVertex;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

