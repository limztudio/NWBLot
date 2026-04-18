// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#version 460

layout(local_size_x = 64) in;

struct NwbDeformerMorphRange{
    uint firstDelta;
    uint deltaCount;
    float weight;
    uint padding;
};

struct NwbDeformerMorphDelta{
    uvec4 vertex;
    vec4 deltaPosition;
    vec4 deltaNormal;
    vec4 deltaTangent;
};

struct NwbDeformerSkinInfluence{
    uvec4 joint;
    vec4 weight;
};

struct NwbDeformerJointMatrix{
    vec4 column0;
    vec4 column1;
    vec4 column2;
    vec4 column3;
};

layout(std430, binding = 0) readonly buffer NwbDeformerRestVerticesBuffer{
    float nwbDeformerRestVertexScalars[];
};

layout(std430, binding = 1) buffer NwbDeformerDeformedVerticesBuffer{
    float nwbDeformerDeformedVertexScalars[];
};

layout(std430, binding = 2) readonly buffer NwbDeformerMorphRangesBuffer{
    NwbDeformerMorphRange nwbDeformerMorphRanges[];
};

layout(std430, binding = 3) readonly buffer NwbDeformerMorphDeltasBuffer{
    NwbDeformerMorphDelta nwbDeformerMorphDeltas[];
};

layout(std430, binding = 4) readonly buffer NwbDeformerSkinInfluencesBuffer{
    NwbDeformerSkinInfluence nwbDeformerSkinInfluences[];
};

layout(std430, binding = 5) readonly buffer NwbDeformerJointPaletteBuffer{
    NwbDeformerJointMatrix nwbDeformerJointPalette[];
};

layout(push_constant) uniform NwbDeformerPushConstants{
    uvec4 payload0;
    uvec4 payload1;
} g_NwbDeformerPushConstants;

uint nwbDeformerVertexCount(){
    return g_NwbDeformerPushConstants.payload0.x;
}

uint nwbDeformerMorphCount(){
    return g_NwbDeformerPushConstants.payload0.y;
}

uint nwbDeformerRestScalarStride(){
    return g_NwbDeformerPushConstants.payload0.z;
}

uint nwbDeformerDeformedScalarStride(){
    return g_NwbDeformerPushConstants.payload0.w;
}

uint nwbDeformerSkinCount(){
    return g_NwbDeformerPushConstants.payload1.x;
}

uint nwbDeformerJointCount(){
    return g_NwbDeformerPushConstants.payload1.y;
}

float nwbDeformerDisplacementAmplitude(){
    return uintBitsToFloat(g_NwbDeformerPushConstants.payload1.z);
}

uint nwbDeformerDisplacementMode(){
    return g_NwbDeformerPushConstants.payload1.w;
}

void nwbDeformerCopyRestPayload(const uint vertexId, const uint restBase, const uint deformedBase){
    for(uint i = 0u; i < nwbDeformerRestScalarStride(); ++i)
        nwbDeformerDeformedVertexScalars[deformedBase + i] = nwbDeformerRestVertexScalars[restBase + i];
}

mat4 nwbDeformerLoadJointMatrix(const uint jointId){
    const NwbDeformerJointMatrix jointMatrix = nwbDeformerJointPalette[jointId];
    return mat4(
        jointMatrix.column0,
        jointMatrix.column1,
        jointMatrix.column2,
        jointMatrix.column3
    );
}

vec3 nwbDeformerSafeNormalize(const vec3 value, const vec3 fallback){
    const float lengthSquared = dot(value, value);
    return lengthSquared > 0.00000001 ? value * inversesqrt(lengthSquared) : fallback;
}

vec3 nwbDeformerFallbackTangent(const vec3 normal){
    const vec3 axis = abs(normal.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);
    return normalize(cross(axis, normal));
}

float nwbDeformerTangentHandedness(const float handedness, const float fallbackHandedness){
    if(abs(handedness) > 0.000001)
        return handedness < 0.0 ? -1.0 : 1.0;
    return fallbackHandedness < 0.0 ? -1.0 : 1.0;
}

void nwbDeformerOrthonormalizeFrame(inout vec3 normal, inout vec4 tangent, const vec3 fallbackNormal, const vec4 fallbackTangent){
    normal = nwbDeformerSafeNormalize(normal, nwbDeformerSafeNormalize(fallbackNormal, vec3(0.0, 0.0, 1.0)));

    vec3 projectedTangent = tangent.xyz - (normal * dot(tangent.xyz, normal));
    if(dot(projectedTangent, projectedTangent) <= 0.00000001)
        projectedTangent = fallbackTangent.xyz - (normal * dot(fallbackTangent.xyz, normal));
    if(dot(projectedTangent, projectedTangent) <= 0.00000001)
        projectedTangent = nwbDeformerFallbackTangent(normal);

    tangent.xyz = nwbDeformerSafeNormalize(projectedTangent, nwbDeformerFallbackTangent(normal));
    tangent.w = nwbDeformerTangentHandedness(tangent.w, fallbackTangent.w);
}

void nwbDeformerApplySkin(const uint vertexId, inout vec3 position, inout vec3 normal, inout vec4 tangent){
    if(nwbDeformerSkinCount() != nwbDeformerVertexCount() || nwbDeformerJointCount() == 0u)
        return;

    const NwbDeformerSkinInfluence skin = nwbDeformerSkinInfluences[vertexId];
    vec3 skinnedPosition = vec3(0.0);
    vec3 skinnedNormal = vec3(0.0);
    vec3 skinnedTangent = vec3(0.0);
    float totalWeight = 0.0;

    for(uint influenceIndex = 0u; influenceIndex < 4u; ++influenceIndex){
        const float weight = skin.weight[influenceIndex];
        const uint jointId = skin.joint[influenceIndex];
        if(abs(weight) <= 0.000001 || jointId >= nwbDeformerJointCount())
            continue;

        const mat4 jointMatrix = nwbDeformerLoadJointMatrix(jointId);
        skinnedPosition += weight * (jointMatrix * vec4(position, 1.0)).xyz;
        skinnedNormal += weight * (mat3(jointMatrix) * normal);
        skinnedTangent += weight * (mat3(jointMatrix) * tangent.xyz);
        totalWeight += weight;
    }

    if(abs(totalWeight) <= 0.000001)
        return;

    position = skinnedPosition;
    normal = skinnedNormal;
    tangent.xyz = skinnedTangent;
}

void nwbDeformerApplyDisplacement(inout vec3 position, const vec3 normal, const vec2 uv0){
    const uint scalarUvRampMode = 1u;
    if(nwbDeformerDisplacementMode() != scalarUvRampMode)
        return;

    const float amplitude = nwbDeformerDisplacementAmplitude();
    if(abs(amplitude) <= 0.000001)
        return;

    position += normal * (clamp(uv0.x, 0.0, 1.0) * amplitude);
}

void main(){
    const uint vertexId = gl_GlobalInvocationID.x;
    if(vertexId >= nwbDeformerVertexCount())
        return;

    const uint restBase = vertexId * nwbDeformerRestScalarStride();
    const uint deformedBase = vertexId * nwbDeformerDeformedScalarStride();

    vec3 position = vec3(
        nwbDeformerRestVertexScalars[restBase + 0u],
        nwbDeformerRestVertexScalars[restBase + 1u],
        nwbDeformerRestVertexScalars[restBase + 2u]
    );
    vec3 normal = vec3(
        nwbDeformerRestVertexScalars[restBase + 3u],
        nwbDeformerRestVertexScalars[restBase + 4u],
        nwbDeformerRestVertexScalars[restBase + 5u]
    );
    vec4 tangent = vec4(
        nwbDeformerRestVertexScalars[restBase + 6u],
        nwbDeformerRestVertexScalars[restBase + 7u],
        nwbDeformerRestVertexScalars[restBase + 8u],
        nwbDeformerRestVertexScalars[restBase + 9u]
    );
    const vec2 uv0 = vec2(
        nwbDeformerRestVertexScalars[restBase + 10u],
        nwbDeformerRestVertexScalars[restBase + 11u]
    );
    const vec3 restNormal = normal;
    const vec4 restTangent = tangent;

    for(uint morphIndex = 0u; morphIndex < nwbDeformerMorphCount(); ++morphIndex){
        const NwbDeformerMorphRange morph = nwbDeformerMorphRanges[morphIndex];
        for(uint deltaIndex = 0u; deltaIndex < morph.deltaCount; ++deltaIndex){
            const NwbDeformerMorphDelta delta = nwbDeformerMorphDeltas[morph.firstDelta + deltaIndex];
            if(delta.vertex.x != vertexId)
                continue;

            position += morph.weight * delta.deltaPosition.xyz;
            normal += morph.weight * delta.deltaNormal.xyz;
            tangent += morph.weight * delta.deltaTangent;
        }
    }

    nwbDeformerOrthonormalizeFrame(normal, tangent, restNormal, restTangent);
    const vec3 preSkinNormal = normal;
    const vec4 preSkinTangent = tangent;
    nwbDeformerApplySkin(vertexId, position, normal, tangent);
    nwbDeformerOrthonormalizeFrame(normal, tangent, preSkinNormal, preSkinTangent);
    nwbDeformerApplyDisplacement(position, normal, uv0);

    nwbDeformerCopyRestPayload(vertexId, restBase, deformedBase);
    nwbDeformerDeformedVertexScalars[deformedBase + 0u] = position.x;
    nwbDeformerDeformedVertexScalars[deformedBase + 1u] = position.y;
    nwbDeformerDeformedVertexScalars[deformedBase + 2u] = position.z;
    nwbDeformerDeformedVertexScalars[deformedBase + 3u] = normal.x;
    nwbDeformerDeformedVertexScalars[deformedBase + 4u] = normal.y;
    nwbDeformerDeformedVertexScalars[deformedBase + 5u] = normal.z;
    nwbDeformerDeformedVertexScalars[deformedBase + 6u] = tangent.x;
    nwbDeformerDeformedVertexScalars[deformedBase + 7u] = tangent.y;
    nwbDeformerDeformedVertexScalars[deformedBase + 8u] = tangent.z;
    nwbDeformerDeformedVertexScalars[deformedBase + 9u] = tangent.w;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

