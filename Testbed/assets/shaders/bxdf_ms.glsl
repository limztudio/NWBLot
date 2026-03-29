#version 460

#include "mesh_shader_authoring.glsli"

#ifndef NWB_PROJECT_BXDF
#define NWB_PROJECT_BXDF 0
#endif

vec3 nwbProjectApplyBxdf(vec3 color){
#if NWB_PROJECT_BXDF == 0
    return color;
#elif NWB_PROJECT_BXDF == 1
    return sqrt(max(color, vec3(0.0)));
#elif NWB_PROJECT_BXDF == 2
    return color * color;
#else
    return color;
#endif
}

vec3 nwbProjectShadeVertex(vec3 color){
    return clamp(nwbProjectApplyBxdf(color), vec3(0.0), vec3(1.0));
}

mat3 nwbProjectBuildRotationY(float angle){
    const float c = cos(angle);
    const float s = sin(angle);
    return mat3(
        c,   0.0, s,
        0.0, 1.0, 0.0,
       -s,   0.0, c
    );
}

mat3 nwbProjectBuildRotationX(float angle){
    const float c = cos(angle);
    const float s = sin(angle);
    return mat3(
        1.0, 0.0, 0.0,
        0.0, c,   s,
        0.0, -s,  c
    );
}

vec4 nwbProjectBuildClipPosition(vec3 worldPosition){
    const mat3 rotY = nwbProjectBuildRotationY(0.82);
    const mat3 rotX = nwbProjectBuildRotationX(0.94);

    vec3 p = rotY * (rotX * worldPosition);
    p.z += 2.2;

    const float invZ = 1.0 / p.z;
    const float ndcX = p.x * invZ;
    const float ndcY = p.y * invZ;
    const float ndcZ = (p.z - 1.0) * 0.5;
    return vec4(ndcX, ndcY, ndcZ, 1.0);
}

NWB_MESH_BUILD_VERTEX_SIGNATURE{
    NwbMeshGeneratedVertex generatedVertex;
    generatedVertex.position = nwbProjectBuildClipPosition(source.position);
    generatedVertex.color = nwbProjectShadeVertex(source.color);
    generatedVertex.padding = 0.0;
    return generatedVertex;
}
