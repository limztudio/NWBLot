// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#version 460

layout(local_size_x = 64) in;

struct NwbDeformerVertexMorphRange{
    uint firstDelta;
    uint deltaCount;
    uint padding0;
    uint padding1;
};

struct NwbDeformerBlendedMorphDelta{
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
    NwbDeformerVertexMorphRange nwbDeformerMorphRanges[];
};

layout(std430, binding = 3) readonly buffer NwbDeformerMorphDeltasBuffer{
    NwbDeformerBlendedMorphDelta nwbDeformerMorphDeltas[];
};

layout(std430, binding = 4) readonly buffer NwbDeformerSkinInfluencesBuffer{
    NwbDeformerSkinInfluence nwbDeformerSkinInfluences[];
};

layout(std430, binding = 5) readonly buffer NwbDeformerJointPaletteBuffer{
    NwbDeformerJointMatrix nwbDeformerJointPalette[];
};

layout(binding = 6) uniform texture2D g_NwbDeformerDisplacementTexture;
layout(binding = 7) uniform sampler g_NwbDeformerDisplacementSampler;

layout(push_constant) uniform NwbDeformerPushConstants{
    uvec4 payload0;
    uvec4 payload1;
    vec4 payload2;
    vec4 payload3;
} g_NwbDeformerPushConstants;

uint nwbDeformerVertexCount(){
    return g_NwbDeformerPushConstants.payload0.x;
}

uint nwbDeformerMorphRangeCount(){
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

uint nwbDeformerDisplacementMode(){
    return g_NwbDeformerPushConstants.payload1.z;
}

uint nwbDeformerSkinningMode(){
    return g_NwbDeformerPushConstants.payload1.w;
}

float nwbDeformerDisplacementAmplitude(){
    return g_NwbDeformerPushConstants.payload2.x;
}

float nwbDeformerDisplacementBias(){
    return g_NwbDeformerPushConstants.payload2.y;
}

vec2 nwbDeformerDisplacementUvScale(){
    return g_NwbDeformerPushConstants.payload2.zw;
}

vec2 nwbDeformerDisplacementUvOffset(){
    return g_NwbDeformerPushConstants.payload3.xy;
}

bool nwbDeformerFiniteFloat(const float value){
    return !isnan(value) && !isinf(value);
}

bool nwbDeformerFiniteVec3(const vec3 value){
    return !any(isnan(value)) && !any(isinf(value));
}

bool nwbDeformerFiniteVec4(const vec4 value){
    return !any(isnan(value)) && !any(isinf(value));
}

void nwbDeformerCopyRestPayload(const uint restBase, const uint deformedBase, const uint restScalarStride){
    for(uint i = 0u; i < restScalarStride; ++i)
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
    if(!nwbDeformerFiniteVec3(value))
        return fallback;

    const float lengthSquared = dot(value, value);
    return
        nwbDeformerFiniteFloat(lengthSquared) && lengthSquared > 0.00000001
            ? value * inversesqrt(lengthSquared)
            : fallback
    ;
}

bool nwbDeformerValidFrameDirection(const vec3 value){
    return
        nwbDeformerFiniteVec3(value)
        && dot(value, value) > 0.00000001
    ;
}

vec3 nwbDeformerProjectOntoFramePlane(const vec3 value, const vec3 normal){
    return value - (normal * dot(value, normal));
}

vec3 nwbDeformerFallbackTangent(const vec3 normal){
    const vec3 axis = abs(normal.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);
    return nwbDeformerSafeNormalize(cross(axis, normal), vec3(1.0, 0.0, 0.0));
}

float nwbDeformerTangentHandedness(const float handedness, const float fallbackHandedness){
    if(abs(handedness) > 0.000001)
        return handedness < 0.0 ? -1.0 : 1.0;
    return fallbackHandedness < 0.0 ? -1.0 : 1.0;
}

vec3 nwbDeformerResolveFrameTangent(const vec3 normal, const vec3 tangent, const vec3 fallbackTangent){
    const vec3 safeFallbackTangent = nwbDeformerFallbackTangent(normal);

    vec3 projectedTangent = nwbDeformerFiniteVec3(tangent)
        ? nwbDeformerProjectOntoFramePlane(tangent, normal)
        : safeFallbackTangent
    ;
    if(!nwbDeformerValidFrameDirection(projectedTangent)){
        projectedTangent = nwbDeformerFiniteVec3(fallbackTangent)
            ? nwbDeformerProjectOntoFramePlane(fallbackTangent, normal)
            : safeFallbackTangent
        ;
    }
    if(!nwbDeformerValidFrameDirection(projectedTangent))
        return safeFallbackTangent;

    return nwbDeformerSafeNormalize(projectedTangent, safeFallbackTangent);
}

void nwbDeformerOrthonormalizeFrame(inout vec3 normal, inout vec4 tangent, const vec3 fallbackNormal, const vec4 fallbackTangent){
    normal = nwbDeformerSafeNormalize(normal, nwbDeformerSafeNormalize(fallbackNormal, vec3(0.0, 0.0, 1.0)));
    tangent.xyz = nwbDeformerResolveFrameTangent(normal, tangent.xyz, fallbackTangent.xyz);
    tangent.w = nwbDeformerTangentHandedness(tangent.w, fallbackTangent.w);
}

bool nwbDeformerTransformJointNormal(const mat3 jointMatrix, const vec3 normal, out vec3 transformedNormal){
    const vec3 column0 = jointMatrix[0];
    const vec3 column1 = jointMatrix[1];
    const vec3 column2 = jointMatrix[2];
    const float determinant = dot(column0, cross(column1, column2));
    if(!nwbDeformerFiniteFloat(determinant) || abs(determinant) <= 0.000001)
        return false;

    const float inverseDeterminant = 1.0 / determinant;
    const mat3 normalMatrix = mat3(
        cross(column1, column2) * inverseDeterminant,
        cross(column2, column0) * inverseDeterminant,
        cross(column0, column1) * inverseDeterminant
    );
    transformedNormal = normalMatrix * normal;
    return nwbDeformerFiniteVec3(transformedNormal);
}

vec4 nwbDeformerQuaternionMultiply(const vec4 lhs, const vec4 rhs){
    return vec4(
        (lhs.w * rhs.x) + (lhs.x * rhs.w) + (lhs.y * rhs.z) - (lhs.z * rhs.y),
        (lhs.w * rhs.y) - (lhs.x * rhs.z) + (lhs.y * rhs.w) + (lhs.z * rhs.x),
        (lhs.w * rhs.z) + (lhs.x * rhs.y) - (lhs.y * rhs.x) + (lhs.z * rhs.w),
        (lhs.w * rhs.w) - dot(lhs.xyz, rhs.xyz)
    );
}

vec3 nwbDeformerRotateVectorByQuaternion(const vec3 value, const vec4 rotation){
    const vec3 twiceCross = 2.0 * cross(rotation.xyz, value);
    return value + (rotation.w * twiceCross) + cross(rotation.xyz, twiceCross);
}

bool nwbDeformerNormalizeDualQuaternion(inout vec4 real, inout vec4 dual){
    const float lengthSquared = dot(real, real);
    if(!nwbDeformerFiniteFloat(lengthSquared) || lengthSquared <= 0.000001)
        return false;

    const float invLength = inversesqrt(lengthSquared);
    real *= invLength;
    dual *= invLength;
    dual -= real * dot(real, dual);
    return nwbDeformerFiniteVec4(real) && nwbDeformerFiniteVec4(dual);
}

vec3 nwbDeformerTransformDualQuaternionPosition(const vec4 real, const vec4 dual, const vec3 position){
    const vec3 rotatedPosition = nwbDeformerRotateVectorByQuaternion(position, real);
    const vec3 translation = 2.0 * nwbDeformerQuaternionMultiply(
        dual,
        vec4(-real.xyz, real.w)
    ).xyz;
    return rotatedPosition + translation;
}

bool nwbDeformerApplyLinearSkin(const uint vertexId, inout vec3 position, inout vec3 normal, inout vec4 tangent){
    const uint jointCount = nwbDeformerJointCount();
    if(nwbDeformerSkinCount() != nwbDeformerVertexCount() || jointCount == 0u)
        return true;

    const NwbDeformerSkinInfluence skin = nwbDeformerSkinInfluences[vertexId];
    vec3 skinnedPosition = vec3(0.0);
    vec3 skinnedNormal = vec3(0.0);
    vec3 skinnedTangent = vec3(0.0);
    float totalWeight = 0.0;

    for(uint influenceIndex = 0u; influenceIndex < 4u; ++influenceIndex){
        const float weight = skin.weight[influenceIndex];
        const uint jointId = skin.joint[influenceIndex];
        if(abs(weight) <= 0.000001 || jointId >= jointCount)
            continue;

        const mat4 jointMatrix = nwbDeformerLoadJointMatrix(jointId);
        const mat3 jointMatrix3 = mat3(jointMatrix);
        vec3 transformedNormal = vec3(0.0);
        if(!nwbDeformerTransformJointNormal(jointMatrix3, normal, transformedNormal))
            continue;

        skinnedPosition += weight * (jointMatrix * vec4(position, 1.0)).xyz;
        skinnedNormal += weight * transformedNormal;
        skinnedTangent += weight * (jointMatrix3 * tangent.xyz);
        totalWeight += weight;
    }

    if(abs(totalWeight) <= 0.000001)
        return true;

    position = skinnedPosition;
    normal = skinnedNormal;
    tangent.xyz = skinnedTangent;
    return true;
}

bool nwbDeformerApplyDualQuaternionSkin(const uint vertexId, inout vec3 position, inout vec3 normal, inout vec4 tangent){
    const uint jointCount = nwbDeformerJointCount();
    if(nwbDeformerSkinCount() != nwbDeformerVertexCount() || jointCount == 0u)
        return true;

    const NwbDeformerSkinInfluence skin = nwbDeformerSkinInfluences[vertexId];
    vec4 blendedReal = vec4(0.0);
    vec4 blendedDual = vec4(0.0);
    vec4 referenceReal = vec4(0.0);
    bool hasReference = false;
    float totalWeight = 0.0;

    for(uint influenceIndex = 0u; influenceIndex < 4u; ++influenceIndex){
        const float weight = skin.weight[influenceIndex];
        const uint jointId = skin.joint[influenceIndex];
        if(abs(weight) <= 0.000001 || jointId >= jointCount)
            continue;

        const NwbDeformerJointMatrix jointPayload = nwbDeformerJointPalette[jointId];
        vec4 real = jointPayload.column0;
        vec4 dual = jointPayload.column1;
        if(!nwbDeformerFiniteVec4(real) || !nwbDeformerFiniteVec4(dual))
            return false;

        if(!hasReference){
            referenceReal = real;
            hasReference = true;
        }
        else if(dot(referenceReal, real) < 0.0){
            real = -real;
            dual = -dual;
        }

        blendedReal += real * weight;
        blendedDual += dual * weight;
        totalWeight += weight;
    }

    if(abs(totalWeight) <= 0.000001)
        return true;
    if(!nwbDeformerNormalizeDualQuaternion(blendedReal, blendedDual))
        return false;

    position = nwbDeformerTransformDualQuaternionPosition(blendedReal, blendedDual, position);
    normal = nwbDeformerRotateVectorByQuaternion(normal, blendedReal);
    tangent.xyz = nwbDeformerRotateVectorByQuaternion(tangent.xyz, blendedReal);
    return true;
}

void nwbDeformerApplySkin(const uint vertexId, inout vec3 position, inout vec3 normal, inout vec4 tangent){
    const uint dualQuaternionSkinningMode = 1u;
    const bool skinApplied = nwbDeformerSkinningMode() == dualQuaternionSkinningMode
        ? nwbDeformerApplyDualQuaternionSkin(vertexId, position, normal, tangent)
        : nwbDeformerApplyLinearSkin(vertexId, position, normal, tangent)
    ;
    if(!skinApplied)
        return;
}

vec2 nwbDeformerDisplacementTextureCoord(const vec2 uv0){
    return clamp(
        (uv0 * nwbDeformerDisplacementUvScale()) + nwbDeformerDisplacementUvOffset(),
        vec2(0.0),
        vec2(1.0)
    );
}

vec4 nwbDeformerSampleDisplacementTextureCoord(const vec2 uv){
    return texture(
        sampler2D(g_NwbDeformerDisplacementTexture, g_NwbDeformerDisplacementSampler),
        clamp(uv, vec2(0.0), vec2(1.0))
    );
}

vec4 nwbDeformerSampleDisplacementTexture(const vec2 uv0){
    return nwbDeformerSampleDisplacementTextureCoord(nwbDeformerDisplacementTextureCoord(uv0));
}

float nwbDeformerDisplacementTextureCoordStep(const int size){
    return size > 1 ? 1.0 / float(size - 1) : 1.0;
}

void nwbDeformerApplyScalarTextureNormal(inout vec3 normal, inout vec4 tangent, const vec2 uv0){
    const ivec2 textureExtent = textureSize(
        sampler2D(g_NwbDeformerDisplacementTexture, g_NwbDeformerDisplacementSampler),
        0
    );
    if(textureExtent.x <= 1 && textureExtent.y <= 1)
        return;

    const vec2 uv = nwbDeformerDisplacementTextureCoord(uv0);
    const float du = nwbDeformerDisplacementTextureCoordStep(textureExtent.x);
    const float dv = nwbDeformerDisplacementTextureCoordStep(textureExtent.y);
    const float amplitude = nwbDeformerDisplacementAmplitude();
    const float heightU =
        nwbDeformerSampleDisplacementTextureCoord(uv + vec2(du, 0.0)).x
        - nwbDeformerSampleDisplacementTextureCoord(uv - vec2(du, 0.0)).x
    ;
    const float heightV =
        nwbDeformerSampleDisplacementTextureCoord(uv + vec2(0.0, dv)).x
        - nwbDeformerSampleDisplacementTextureCoord(uv - vec2(0.0, dv)).x
    ;
    const vec3 bitangent = nwbDeformerSafeNormalize(
        cross(normal, tangent.xyz),
        vec3(0.0, 1.0, 0.0)
    ) * nwbDeformerTangentHandedness(tangent.w, 1.0);
    const vec3 adjustedNormal = normal
        - tangent.xyz * (heightU * amplitude * 0.5)
        - bitangent * (heightV * amplitude * 0.5)
    ;
    normal = nwbDeformerSafeNormalize(adjustedNormal, normal);
    tangent.xyz = nwbDeformerResolveFrameTangent(normal, tangent.xyz, tangent.xyz);
}

vec3 nwbDeformerVectorTextureOffsetToWorld(const vec3 sampleValue, const uint mode, const vec3 normal, const vec4 tangent){
    const uint vectorTangentTextureMode = 3u;
    vec3 vectorOffset = (sampleValue + vec3(nwbDeformerDisplacementBias())) * nwbDeformerDisplacementAmplitude();
    if(mode != vectorTangentTextureMode)
        return vectorOffset;

    const vec3 bitangent = nwbDeformerSafeNormalize(
        cross(normal, tangent.xyz),
        vec3(0.0, 1.0, 0.0)
    ) * nwbDeformerTangentHandedness(tangent.w, 1.0);
    return tangent.xyz * vectorOffset.x
        + bitangent * vectorOffset.y
        + normal * vectorOffset.z
    ;
}

void nwbDeformerApplyVectorTextureNormal(inout vec3 normal, inout vec4 tangent, const vec2 uv0, const uint mode){
    const ivec2 textureExtent = textureSize(
        sampler2D(g_NwbDeformerDisplacementTexture, g_NwbDeformerDisplacementSampler),
        0
    );
    if(textureExtent.x <= 1 && textureExtent.y <= 1)
        return;

    const vec2 uv = nwbDeformerDisplacementTextureCoord(uv0);
    const float du = nwbDeformerDisplacementTextureCoordStep(textureExtent.x);
    const float dv = nwbDeformerDisplacementTextureCoordStep(textureExtent.y);
    const vec3 right = nwbDeformerVectorTextureOffsetToWorld(
        nwbDeformerSampleDisplacementTextureCoord(uv + vec2(du, 0.0)).xyz,
        mode,
        normal,
        tangent
    );
    const vec3 left = nwbDeformerVectorTextureOffsetToWorld(
        nwbDeformerSampleDisplacementTextureCoord(uv - vec2(du, 0.0)).xyz,
        mode,
        normal,
        tangent
    );
    const vec3 up = nwbDeformerVectorTextureOffsetToWorld(
        nwbDeformerSampleDisplacementTextureCoord(uv + vec2(0.0, dv)).xyz,
        mode,
        normal,
        tangent
    );
    const vec3 down = nwbDeformerVectorTextureOffsetToWorld(
        nwbDeformerSampleDisplacementTextureCoord(uv - vec2(0.0, dv)).xyz,
        mode,
        normal,
        tangent
    );
    if(!nwbDeformerFiniteVec3(right)
        || !nwbDeformerFiniteVec3(left)
        || !nwbDeformerFiniteVec3(up)
        || !nwbDeformerFiniteVec3(down)
    )
        return;

    const vec3 derivativeU = (right - left) * 0.5;
    const vec3 derivativeV = (up - down) * 0.5;
    const float handedness = nwbDeformerTangentHandedness(tangent.w, 1.0);
    const vec3 bitangent = nwbDeformerSafeNormalize(
        cross(normal, tangent.xyz),
        vec3(0.0, 1.0, 0.0)
    ) * handedness;
    const vec3 displacedTangent = tangent.xyz + derivativeU;
    const vec3 displacedBitangent = bitangent + derivativeV;
    const vec3 adjustedNormal = cross(displacedTangent, displacedBitangent) * handedness;
    normal = nwbDeformerSafeNormalize(adjustedNormal, normal);
    tangent.xyz = nwbDeformerResolveFrameTangent(normal, displacedTangent, tangent.xyz);
    tangent.w = handedness;
}

void nwbDeformerApplyDisplacement(inout vec3 position, inout vec3 normal, inout vec4 tangent, const vec2 uv0){
    const uint noneMode = 0u;
    const uint scalarUvRampMode = 1u;
    const uint scalarTextureMode = 2u;
    const uint vectorTangentTextureMode = 3u;
    const uint vectorObjectTextureMode = 4u;
    const uint displacementMode = nwbDeformerDisplacementMode();
    if(displacementMode == noneMode)
        return;

    const float amplitude = nwbDeformerDisplacementAmplitude();
    if(abs(amplitude) <= 0.000001)
        return;

    if(displacementMode == scalarUvRampMode){
        position += normal * (clamp(uv0.x, 0.0, 1.0) * amplitude);
        return;
    }

    const vec4 sampleValue = nwbDeformerSampleDisplacementTexture(uv0);
    if(displacementMode == scalarTextureMode){
        position += normal * ((sampleValue.x + nwbDeformerDisplacementBias()) * amplitude);
        nwbDeformerApplyScalarTextureNormal(normal, tangent, uv0);
        return;
    }

    vec3 vectorOffset = nwbDeformerVectorTextureOffsetToWorld(sampleValue.xyz, displacementMode, normal, tangent);
    if(!nwbDeformerFiniteVec3(vectorOffset))
        return;

    if(displacementMode != vectorTangentTextureMode && displacementMode != vectorObjectTextureMode)
        return;

    nwbDeformerApplyVectorTextureNormal(normal, tangent, uv0, displacementMode);
    position += vectorOffset;
}

void main(){
    const uint vertexId = gl_GlobalInvocationID.x;
    if(vertexId >= nwbDeformerVertexCount())
        return;

    const uint restScalarStride = nwbDeformerRestScalarStride();
    const uint deformedScalarStride = nwbDeformerDeformedScalarStride();
    const uint restBase = vertexId * restScalarStride;
    const uint deformedBase = vertexId * deformedScalarStride;

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
    const vec3 restPosition = position;
    const vec3 restNormal = normal;
    const vec4 restTangent = tangent;

    if(nwbDeformerMorphRangeCount() == nwbDeformerVertexCount()){
        const NwbDeformerVertexMorphRange morph = nwbDeformerMorphRanges[vertexId];
        for(uint deltaIndex = 0u; deltaIndex < morph.deltaCount; ++deltaIndex){
            const NwbDeformerBlendedMorphDelta delta = nwbDeformerMorphDeltas[morph.firstDelta + deltaIndex];
            position += delta.deltaPosition.xyz;
            normal += delta.deltaNormal.xyz;
            tangent += delta.deltaTangent;
        }
    }

    if(!nwbDeformerFiniteVec3(position))
        position = restPosition;
    if(!nwbDeformerFiniteVec3(normal))
        normal = restNormal;
    if(!nwbDeformerFiniteVec4(tangent))
        tangent = restTangent;

    nwbDeformerOrthonormalizeFrame(normal, tangent, restNormal, restTangent);
    const vec3 preSkinPosition = position;
    const vec3 preSkinNormal = normal;
    const vec4 preSkinTangent = tangent;
    nwbDeformerApplySkin(vertexId, position, normal, tangent);
    if(!nwbDeformerFiniteVec3(position))
        position = preSkinPosition;
    if(!nwbDeformerFiniteVec3(normal))
        normal = preSkinNormal;
    if(!nwbDeformerFiniteVec4(tangent))
        tangent = preSkinTangent;
    nwbDeformerOrthonormalizeFrame(normal, tangent, preSkinNormal, preSkinTangent);
    const vec3 preDisplacementPosition = position;
    const vec3 preDisplacementNormal = normal;
    const vec4 preDisplacementTangent = tangent;
    nwbDeformerApplyDisplacement(position, normal, tangent, uv0);
    if(!nwbDeformerFiniteVec3(position))
        position = preDisplacementPosition;
    if(!nwbDeformerFiniteVec3(normal))
        normal = preDisplacementNormal;
    if(!nwbDeformerFiniteVec4(tangent))
        tangent = preDisplacementTangent;
    nwbDeformerOrthonormalizeFrame(normal, tangent, preDisplacementNormal, preDisplacementTangent);

    nwbDeformerCopyRestPayload(restBase, deformedBase, restScalarStride);
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

