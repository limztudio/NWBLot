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
    generatedVertex.color = clamp(nwbProjectBxdfVertex(source.color), vec3(0.0), vec3(1.0));
    generatedVertex.padding = 0.0;
    return generatedVertex;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
