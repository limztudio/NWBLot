#version 460

#include "mesh_shader_authoring.glsli"
#include "bxdf.glsli"

mat3 nwbEngineBuildRotationY(float angle){
    const float c = cos(angle);
    const float s = sin(angle);
    return mat3(
        c,   0.0, s,
        0.0, 1.0, 0.0,
       -s,   0.0, c
    );
}

mat3 nwbEngineBuildRotationX(float angle){
    const float c = cos(angle);
    const float s = sin(angle);
    return mat3(
        1.0, 0.0, 0.0,
        0.0, c,   s,
        0.0, -s,  c
    );
}

vec4 nwbEngineBuildClipPosition(vec3 worldPosition){
    const mat3 rotY = nwbEngineBuildRotationY(0.82);
    const mat3 rotX = nwbEngineBuildRotationX(0.94);

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
    generatedVertex.position = nwbEngineBuildClipPosition(source.position);
    generatedVertex.color = clamp(nwbProjectBxdfVertex(source.color), vec3(0.0), vec3(1.0));
    generatedVertex.padding = 0.0;
    return generatedVertex;
}
