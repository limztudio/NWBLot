// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#version 460


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


layout(local_size_x = 64) in;

struct NwbSkinnedGeometryVertexMorphRange{
    uint firstDelta;
    uint deltaCount;
    uint padding0;
    uint padding1;
};

struct NwbSkinnedGeometryBlendedMorphDelta{
    vec4 deltaPosition;
    vec4 deltaNormal;
    vec4 deltaTangent;
};

struct NwbSkinnedGeometrySkinInfluence{
    uvec4 joint;
    vec4 weight;
};

struct NwbSkinnedGeometryJointMatrix{
    vec4 column0;
    vec4 column1;
    vec4 column2;
    vec4 column3;
};

layout(std430, binding = 0) readonly buffer NwbSkinnedGeometryRestVerticesBuffer{
    float nwbSkinnedGeometryRestVertexScalars[];
};

layout(std430, binding = 1) buffer NwbSkinnedGeometryDeformedVerticesBuffer{
    float nwbSkinnedGeometryDeformedVertexScalars[];
};

layout(std430, binding = 2) readonly buffer NwbSkinnedGeometryMorphRangesBuffer{
    NwbSkinnedGeometryVertexMorphRange nwbSkinnedGeometryMorphRanges[];
};

layout(std430, binding = 3) readonly buffer NwbSkinnedGeometryMorphDeltasBuffer{
    NwbSkinnedGeometryBlendedMorphDelta nwbSkinnedGeometryMorphDeltas[];
};

layout(std430, binding = 4) readonly buffer NwbSkinnedGeometrySkinInfluencesBuffer{
    NwbSkinnedGeometrySkinInfluence nwbSkinnedGeometrySkinInfluences[];
};

layout(std430, binding = 5) readonly buffer NwbSkinnedGeometryJointPaletteBuffer{
    NwbSkinnedGeometryJointMatrix nwbSkinnedGeometryJointPalette[];
};

layout(binding = 6) uniform texture2D g_NwbSkinnedGeometryDisplacementTexture;
layout(binding = 7) uniform sampler g_NwbSkinnedGeometryDisplacementSampler;

layout(push_constant) uniform NwbSkinnedGeometryPushConstants{
    uvec4 payload0;
    uvec4 payload1;
    vec4 payload2;
    vec4 payload3;
} g_NwbSkinnedGeometryPushConstants;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


uint nwbSkinnedGeometryVertexCount(){
    return g_NwbSkinnedGeometryPushConstants.payload0.x;
}

uint nwbSkinnedGeometryMorphRangeCount(){
    return g_NwbSkinnedGeometryPushConstants.payload0.y;
}

uint nwbSkinnedGeometryRestScalarStride(){
    return g_NwbSkinnedGeometryPushConstants.payload0.z;
}

uint nwbSkinnedGeometryDeformedScalarStride(){
    return g_NwbSkinnedGeometryPushConstants.payload0.w;
}

uint nwbSkinnedGeometrySkinCount(){
    return g_NwbSkinnedGeometryPushConstants.payload1.x;
}

uint nwbSkinnedGeometryJointCount(){
    return g_NwbSkinnedGeometryPushConstants.payload1.y;
}

uint nwbSkinnedGeometryDisplacementMode(){
    return g_NwbSkinnedGeometryPushConstants.payload1.z;
}

uint nwbSkinnedGeometrySkinningMode(){
    return g_NwbSkinnedGeometryPushConstants.payload1.w;
}

float nwbSkinnedGeometryDisplacementAmplitude(){
    return g_NwbSkinnedGeometryPushConstants.payload2.x;
}

float nwbSkinnedGeometryDisplacementBias(){
    return g_NwbSkinnedGeometryPushConstants.payload2.y;
}

vec2 nwbSkinnedGeometryDisplacementUvScale(){
    return g_NwbSkinnedGeometryPushConstants.payload2.zw;
}

vec2 nwbSkinnedGeometryDisplacementUvOffset(){
    return g_NwbSkinnedGeometryPushConstants.payload3.xy;
}

bool nwbSkinnedGeometryFiniteFloat(const float value){
    return !isnan(value) && !isinf(value);
}

bool nwbSkinnedGeometryFiniteVec3(const vec3 value){
    return !any(isnan(value)) && !any(isinf(value));
}

bool nwbSkinnedGeometryFiniteVec4(const vec4 value){
    return !any(isnan(value)) && !any(isinf(value));
}

void nwbSkinnedGeometryCopyRestPayload(const uint restBase, const uint deformedBase, const uint restScalarStride){
    for(uint i = 0u; i < restScalarStride; ++i)
        nwbSkinnedGeometryDeformedVertexScalars[deformedBase + i] = nwbSkinnedGeometryRestVertexScalars[restBase + i];
}

mat4 nwbSkinnedGeometryLoadJointMatrix(const uint jointId){
    const NwbSkinnedGeometryJointMatrix jointMatrix = nwbSkinnedGeometryJointPalette[jointId];
    return mat4(
        jointMatrix.column0,
        jointMatrix.column1,
        jointMatrix.column2,
        jointMatrix.column3
    );
}

vec3 nwbSkinnedGeometrySafeNormalize(const vec3 value, const vec3 fallback){
    if(!nwbSkinnedGeometryFiniteVec3(value))
        return fallback;

    const float lengthSquared = dot(value, value);
    return
        nwbSkinnedGeometryFiniteFloat(lengthSquared) && lengthSquared > 0.00000001
            ? value * inversesqrt(lengthSquared)
            : fallback
    ;
}

bool nwbSkinnedGeometryValidFrameDirection(const vec3 value){
    return
        nwbSkinnedGeometryFiniteVec3(value)
        && dot(value, value) > 0.00000001
    ;
}

vec3 nwbSkinnedGeometryProjectOntoFramePlane(const vec3 value, const vec3 normal){
    return value - (normal * dot(value, normal));
}

vec3 nwbSkinnedGeometryFallbackTangent(const vec3 normal){
    const vec3 axis = abs(normal.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);
    return nwbSkinnedGeometrySafeNormalize(cross(axis, normal), vec3(1.0, 0.0, 0.0));
}

float nwbSkinnedGeometryTangentHandedness(const float handedness, const float fallbackHandedness){
    if(abs(handedness) > 0.000001)
        return handedness < 0.0 ? -1.0 : 1.0;
    return fallbackHandedness < 0.0 ? -1.0 : 1.0;
}

vec3 nwbSkinnedGeometryResolveFrameTangent(const vec3 normal, const vec3 tangent, const vec3 fallbackTangent){
    const vec3 safeFallbackTangent = nwbSkinnedGeometryFallbackTangent(normal);

    vec3 projectedTangent = nwbSkinnedGeometryFiniteVec3(tangent)
        ? nwbSkinnedGeometryProjectOntoFramePlane(tangent, normal)
        : safeFallbackTangent
    ;
    if(!nwbSkinnedGeometryValidFrameDirection(projectedTangent)){
        projectedTangent = nwbSkinnedGeometryFiniteVec3(fallbackTangent)
            ? nwbSkinnedGeometryProjectOntoFramePlane(fallbackTangent, normal)
            : safeFallbackTangent
        ;
    }
    if(!nwbSkinnedGeometryValidFrameDirection(projectedTangent))
        return safeFallbackTangent;

    return nwbSkinnedGeometrySafeNormalize(projectedTangent, safeFallbackTangent);
}

void nwbSkinnedGeometryOrthonormalizeFrame(
    inout vec3 normal,
    inout vec4 tangent,
    const vec3 fallbackNormal,
    const vec4 fallbackTangent
){
    normal = nwbSkinnedGeometrySafeNormalize(normal, nwbSkinnedGeometrySafeNormalize(fallbackNormal, vec3(0.0, 0.0, 1.0)));
    tangent.xyz = nwbSkinnedGeometryResolveFrameTangent(normal, tangent.xyz, fallbackTangent.xyz);
    tangent.w = nwbSkinnedGeometryTangentHandedness(tangent.w, fallbackTangent.w);
}

bool nwbSkinnedGeometryTransformJointNormal(const mat3 jointMatrix, const vec3 normal, out vec3 transformedNormal){
    const vec3 column0 = jointMatrix[0];
    const vec3 column1 = jointMatrix[1];
    const vec3 column2 = jointMatrix[2];
    const float determinant = dot(column0, cross(column1, column2));
    if(!nwbSkinnedGeometryFiniteFloat(determinant) || abs(determinant) <= 0.000001)
        return false;

    const float inverseDeterminant = 1.0 / determinant;
    const mat3 normalMatrix = mat3(
        cross(column1, column2) * inverseDeterminant,
        cross(column2, column0) * inverseDeterminant,
        cross(column0, column1) * inverseDeterminant
    );
    transformedNormal = normalMatrix * normal;
    return nwbSkinnedGeometryFiniteVec3(transformedNormal);
}

vec4 nwbSkinnedGeometryQuaternionMultiply(const vec4 lhs, const vec4 rhs){
    return vec4(
        (lhs.w * rhs.x) + (lhs.x * rhs.w) + (lhs.y * rhs.z) - (lhs.z * rhs.y),
        (lhs.w * rhs.y) - (lhs.x * rhs.z) + (lhs.y * rhs.w) + (lhs.z * rhs.x),
        (lhs.w * rhs.z) + (lhs.x * rhs.y) - (lhs.y * rhs.x) + (lhs.z * rhs.w),
        (lhs.w * rhs.w) - dot(lhs.xyz, rhs.xyz)
    );
}

vec3 nwbSkinnedGeometryRotateVectorByQuaternion(const vec3 value, const vec4 rotation){
    const vec3 twiceCross = 2.0 * cross(rotation.xyz, value);
    return value + (rotation.w * twiceCross) + cross(rotation.xyz, twiceCross);
}

bool nwbSkinnedGeometryNormalizeDualQuaternion(inout vec4 real, inout vec4 dual){
    const float lengthSquared = dot(real, real);
    if(!nwbSkinnedGeometryFiniteFloat(lengthSquared) || lengthSquared <= 0.000001)
        return false;

    const float invLength = inversesqrt(lengthSquared);
    real *= invLength;
    dual *= invLength;
    dual -= real * dot(real, dual);
    return nwbSkinnedGeometryFiniteVec4(real) && nwbSkinnedGeometryFiniteVec4(dual);
}

vec3 nwbSkinnedGeometryTransformDualQuaternionPosition(const vec4 real, const vec4 dual, const vec3 position){
    const vec3 rotatedPosition = nwbSkinnedGeometryRotateVectorByQuaternion(position, real);
    const vec3 translation = 2.0 * nwbSkinnedGeometryQuaternionMultiply(
        dual,
        vec4(-real.xyz, real.w)
    ).xyz;
    return rotatedPosition + translation;
}

bool nwbSkinnedGeometryApplyLinearSkin(const uint vertexId, inout vec3 position, inout vec3 normal, inout vec4 tangent){
    const uint jointCount = nwbSkinnedGeometryJointCount();
    if(nwbSkinnedGeometrySkinCount() != nwbSkinnedGeometryVertexCount() || jointCount == 0u)
        return true;

    const NwbSkinnedGeometrySkinInfluence skin = nwbSkinnedGeometrySkinInfluences[vertexId];
    vec3 skinnedPosition = vec3(0.0);
    vec3 skinnedNormal = vec3(0.0);
    vec3 skinnedTangent = vec3(0.0);
    float totalWeight = 0.0;

    for(uint influenceIndex = 0u; influenceIndex < 4u; ++influenceIndex){
        const float weight = skin.weight[influenceIndex];
        const uint jointId = skin.joint[influenceIndex];
        if(abs(weight) <= 0.000001 || jointId >= jointCount)
            continue;

        const mat4 jointMatrix = nwbSkinnedGeometryLoadJointMatrix(jointId);
        const mat3 jointMatrix3 = mat3(jointMatrix);
        vec3 transformedNormal = vec3(0.0);
        if(!nwbSkinnedGeometryTransformJointNormal(jointMatrix3, normal, transformedNormal))
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

bool nwbSkinnedGeometryApplyDualQuaternionSkin(const uint vertexId, inout vec3 position, inout vec3 normal, inout vec4 tangent){
    const uint jointCount = nwbSkinnedGeometryJointCount();
    if(nwbSkinnedGeometrySkinCount() != nwbSkinnedGeometryVertexCount() || jointCount == 0u)
        return true;

    const NwbSkinnedGeometrySkinInfluence skin = nwbSkinnedGeometrySkinInfluences[vertexId];
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

        const NwbSkinnedGeometryJointMatrix jointPayload = nwbSkinnedGeometryJointPalette[jointId];
        vec4 real = jointPayload.column0;
        vec4 dual = jointPayload.column1;
        if(!nwbSkinnedGeometryFiniteVec4(real) || !nwbSkinnedGeometryFiniteVec4(dual))
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
    if(!nwbSkinnedGeometryNormalizeDualQuaternion(blendedReal, blendedDual))
        return false;

    position = nwbSkinnedGeometryTransformDualQuaternionPosition(blendedReal, blendedDual, position);
    normal = nwbSkinnedGeometryRotateVectorByQuaternion(normal, blendedReal);
    tangent.xyz = nwbSkinnedGeometryRotateVectorByQuaternion(tangent.xyz, blendedReal);
    return true;
}

void nwbSkinnedGeometryApplySkin(const uint vertexId, inout vec3 position, inout vec3 normal, inout vec4 tangent){
    const uint dualQuaternionSkinningMode = 1u;
    const bool skinApplied = nwbSkinnedGeometrySkinningMode() == dualQuaternionSkinningMode
        ? nwbSkinnedGeometryApplyDualQuaternionSkin(vertexId, position, normal, tangent)
        : nwbSkinnedGeometryApplyLinearSkin(vertexId, position, normal, tangent)
    ;
    if(!skinApplied)
        return;
}

vec2 nwbSkinnedGeometryDisplacementTextureCoord(const vec2 uv0){
    return clamp(
        (uv0 * nwbSkinnedGeometryDisplacementUvScale()) + nwbSkinnedGeometryDisplacementUvOffset(),
        vec2(0.0),
        vec2(1.0)
    );
}

vec4 nwbSkinnedGeometrySampleDisplacementTextureCoord(const vec2 uv){
    return texture(
        sampler2D(g_NwbSkinnedGeometryDisplacementTexture, g_NwbSkinnedGeometryDisplacementSampler),
        clamp(uv, vec2(0.0), vec2(1.0))
    );
}

vec4 nwbSkinnedGeometrySampleDisplacementTexture(const vec2 uv0){
    return nwbSkinnedGeometrySampleDisplacementTextureCoord(nwbSkinnedGeometryDisplacementTextureCoord(uv0));
}

float nwbSkinnedGeometryDisplacementTextureCoordStep(const int size){
    return size > 1 ? 1.0 / float(size - 1) : 1.0;
}

struct NwbSkinnedGeometryDisplacementTextureSampling{
    vec2 uv;
    float du;
    float dv;
};

bool nwbSkinnedGeometryPrepareDisplacementTextureSampling(
    const vec2 uv0,
    out NwbSkinnedGeometryDisplacementTextureSampling outSampling
){
    const ivec2 textureExtent = textureSize(
        sampler2D(g_NwbSkinnedGeometryDisplacementTexture, g_NwbSkinnedGeometryDisplacementSampler),
        0
    );
    if(textureExtent.x <= 1 && textureExtent.y <= 1)
        return false;

    outSampling.uv = nwbSkinnedGeometryDisplacementTextureCoord(uv0);
    outSampling.du = nwbSkinnedGeometryDisplacementTextureCoordStep(textureExtent.x);
    outSampling.dv = nwbSkinnedGeometryDisplacementTextureCoordStep(textureExtent.y);
    return true;
}

void nwbSkinnedGeometryApplyScalarTextureNormal(inout vec3 normal, inout vec4 tangent, const vec2 uv0){
    NwbSkinnedGeometryDisplacementTextureSampling sampling;
    if(!nwbSkinnedGeometryPrepareDisplacementTextureSampling(uv0, sampling))
        return;

    const float amplitude = nwbSkinnedGeometryDisplacementAmplitude();
    const float heightU =
        nwbSkinnedGeometrySampleDisplacementTextureCoord(sampling.uv + vec2(sampling.du, 0.0)).x
        - nwbSkinnedGeometrySampleDisplacementTextureCoord(sampling.uv - vec2(sampling.du, 0.0)).x
    ;
    const float heightV =
        nwbSkinnedGeometrySampleDisplacementTextureCoord(sampling.uv + vec2(0.0, sampling.dv)).x
        - nwbSkinnedGeometrySampleDisplacementTextureCoord(sampling.uv - vec2(0.0, sampling.dv)).x
    ;
    const vec3 bitangent = nwbSkinnedGeometrySafeNormalize(
        cross(normal, tangent.xyz),
        vec3(0.0, 1.0, 0.0)
    ) * nwbSkinnedGeometryTangentHandedness(tangent.w, 1.0);
    const vec3 adjustedNormal = normal
        - tangent.xyz * (heightU * amplitude * 0.5)
        - bitangent * (heightV * amplitude * 0.5)
    ;
    normal = nwbSkinnedGeometrySafeNormalize(adjustedNormal, normal);
    tangent.xyz = nwbSkinnedGeometryResolveFrameTangent(normal, tangent.xyz, tangent.xyz);
}

vec3 nwbSkinnedGeometryVectorTextureOffsetToWorld(const vec3 sampleValue, const uint mode, const vec3 normal, const vec4 tangent){
    const uint vectorTangentTextureMode = 3u;
    vec3 vectorOffset = (sampleValue + vec3(nwbSkinnedGeometryDisplacementBias())) * nwbSkinnedGeometryDisplacementAmplitude();
    if(mode != vectorTangentTextureMode)
        return vectorOffset;

    const vec3 bitangent = nwbSkinnedGeometrySafeNormalize(
        cross(normal, tangent.xyz),
        vec3(0.0, 1.0, 0.0)
    ) * nwbSkinnedGeometryTangentHandedness(tangent.w, 1.0);
    return tangent.xyz * vectorOffset.x
        + bitangent * vectorOffset.y
        + normal * vectorOffset.z
    ;
}

void nwbSkinnedGeometryApplyVectorTextureNormal(inout vec3 normal, inout vec4 tangent, const vec2 uv0, const uint mode){
    NwbSkinnedGeometryDisplacementTextureSampling sampling;
    if(!nwbSkinnedGeometryPrepareDisplacementTextureSampling(uv0, sampling))
        return;

    const vec3 right = nwbSkinnedGeometryVectorTextureOffsetToWorld(
        nwbSkinnedGeometrySampleDisplacementTextureCoord(sampling.uv + vec2(sampling.du, 0.0)).xyz,
        mode,
        normal,
        tangent
    );
    const vec3 left = nwbSkinnedGeometryVectorTextureOffsetToWorld(
        nwbSkinnedGeometrySampleDisplacementTextureCoord(sampling.uv - vec2(sampling.du, 0.0)).xyz,
        mode,
        normal,
        tangent
    );
    const vec3 up = nwbSkinnedGeometryVectorTextureOffsetToWorld(
        nwbSkinnedGeometrySampleDisplacementTextureCoord(sampling.uv + vec2(0.0, sampling.dv)).xyz,
        mode,
        normal,
        tangent
    );
    const vec3 down = nwbSkinnedGeometryVectorTextureOffsetToWorld(
        nwbSkinnedGeometrySampleDisplacementTextureCoord(sampling.uv - vec2(0.0, sampling.dv)).xyz,
        mode,
        normal,
        tangent
    );
    if(!nwbSkinnedGeometryFiniteVec3(right)
        || !nwbSkinnedGeometryFiniteVec3(left)
        || !nwbSkinnedGeometryFiniteVec3(up)
        || !nwbSkinnedGeometryFiniteVec3(down)
    )
        return;

    const vec3 derivativeU = (right - left) * 0.5;
    const vec3 derivativeV = (up - down) * 0.5;
    const float handedness = nwbSkinnedGeometryTangentHandedness(tangent.w, 1.0);
    const vec3 bitangent = nwbSkinnedGeometrySafeNormalize(
        cross(normal, tangent.xyz),
        vec3(0.0, 1.0, 0.0)
    ) * handedness;
    const vec3 displacedTangent = tangent.xyz + derivativeU;
    const vec3 displacedBitangent = bitangent + derivativeV;
    const vec3 adjustedNormal = cross(displacedTangent, displacedBitangent) * handedness;
    normal = nwbSkinnedGeometrySafeNormalize(adjustedNormal, normal);
    tangent.xyz = nwbSkinnedGeometryResolveFrameTangent(normal, displacedTangent, tangent.xyz);
    tangent.w = handedness;
}

void nwbSkinnedGeometryApplyDisplacement(inout vec3 position, inout vec3 normal, inout vec4 tangent, const vec2 uv0){
    const uint noneMode = 0u;
    const uint scalarUvRampMode = 1u;
    const uint scalarTextureMode = 2u;
    const uint vectorTangentTextureMode = 3u;
    const uint vectorObjectTextureMode = 4u;
    const uint displacementMode = nwbSkinnedGeometryDisplacementMode();
    if(displacementMode == noneMode)
        return;

    const float amplitude = nwbSkinnedGeometryDisplacementAmplitude();
    if(abs(amplitude) <= 0.000001)
        return;

    if(displacementMode == scalarUvRampMode){
        position += normal * (clamp(uv0.x, 0.0, 1.0) * amplitude);
        return;
    }

    const vec4 sampleValue = nwbSkinnedGeometrySampleDisplacementTexture(uv0);
    if(displacementMode == scalarTextureMode){
        position += normal * ((sampleValue.x + nwbSkinnedGeometryDisplacementBias()) * amplitude);
        nwbSkinnedGeometryApplyScalarTextureNormal(normal, tangent, uv0);
        return;
    }

    vec3 vectorOffset = nwbSkinnedGeometryVectorTextureOffsetToWorld(sampleValue.xyz, displacementMode, normal, tangent);
    if(!nwbSkinnedGeometryFiniteVec3(vectorOffset))
        return;

    if(displacementMode != vectorTangentTextureMode && displacementMode != vectorObjectTextureMode)
        return;

    nwbSkinnedGeometryApplyVectorTextureNormal(normal, tangent, uv0, displacementMode);
    position += vectorOffset;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void main(){
    const uint vertexId = gl_GlobalInvocationID.x;
    if(vertexId >= nwbSkinnedGeometryVertexCount())
        return;

    const uint restScalarStride = nwbSkinnedGeometryRestScalarStride();
    const uint deformedScalarStride = nwbSkinnedGeometryDeformedScalarStride();
    const uint restBase = vertexId * restScalarStride;
    const uint deformedBase = vertexId * deformedScalarStride;

    vec3 position = vec3(
        nwbSkinnedGeometryRestVertexScalars[restBase + 0u],
        nwbSkinnedGeometryRestVertexScalars[restBase + 1u],
        nwbSkinnedGeometryRestVertexScalars[restBase + 2u]
    );
    vec3 normal = vec3(
        nwbSkinnedGeometryRestVertexScalars[restBase + 3u],
        nwbSkinnedGeometryRestVertexScalars[restBase + 4u],
        nwbSkinnedGeometryRestVertexScalars[restBase + 5u]
    );
    vec4 tangent = vec4(
        nwbSkinnedGeometryRestVertexScalars[restBase + 6u],
        nwbSkinnedGeometryRestVertexScalars[restBase + 7u],
        nwbSkinnedGeometryRestVertexScalars[restBase + 8u],
        nwbSkinnedGeometryRestVertexScalars[restBase + 9u]
    );
    const vec2 uv0 = vec2(
        nwbSkinnedGeometryRestVertexScalars[restBase + 10u],
        nwbSkinnedGeometryRestVertexScalars[restBase + 11u]
    );
    const vec3 restPosition = position;
    const vec3 restNormal = normal;
    const vec4 restTangent = tangent;

    if(nwbSkinnedGeometryMorphRangeCount() == nwbSkinnedGeometryVertexCount()){
        const NwbSkinnedGeometryVertexMorphRange morph = nwbSkinnedGeometryMorphRanges[vertexId];
        for(uint deltaIndex = 0u; deltaIndex < morph.deltaCount; ++deltaIndex){
            const NwbSkinnedGeometryBlendedMorphDelta delta = nwbSkinnedGeometryMorphDeltas[morph.firstDelta + deltaIndex];
            position += delta.deltaPosition.xyz;
            normal += delta.deltaNormal.xyz;
            tangent += delta.deltaTangent;
        }
    }

    if(!nwbSkinnedGeometryFiniteVec3(position))
        position = restPosition;
    if(!nwbSkinnedGeometryFiniteVec3(normal))
        normal = restNormal;
    if(!nwbSkinnedGeometryFiniteVec4(tangent))
        tangent = restTangent;

    nwbSkinnedGeometryOrthonormalizeFrame(normal, tangent, restNormal, restTangent);
    const vec3 preSkinPosition = position;
    const vec3 preSkinNormal = normal;
    const vec4 preSkinTangent = tangent;
    nwbSkinnedGeometryApplySkin(vertexId, position, normal, tangent);
    if(!nwbSkinnedGeometryFiniteVec3(position))
        position = preSkinPosition;
    if(!nwbSkinnedGeometryFiniteVec3(normal))
        normal = preSkinNormal;
    if(!nwbSkinnedGeometryFiniteVec4(tangent))
        tangent = preSkinTangent;
    nwbSkinnedGeometryOrthonormalizeFrame(normal, tangent, preSkinNormal, preSkinTangent);
    const vec3 preDisplacementPosition = position;
    const vec3 preDisplacementNormal = normal;
    const vec4 preDisplacementTangent = tangent;
    nwbSkinnedGeometryApplyDisplacement(position, normal, tangent, uv0);
    if(!nwbSkinnedGeometryFiniteVec3(position))
        position = preDisplacementPosition;
    if(!nwbSkinnedGeometryFiniteVec3(normal))
        normal = preDisplacementNormal;
    if(!nwbSkinnedGeometryFiniteVec4(tangent))
        tangent = preDisplacementTangent;
    nwbSkinnedGeometryOrthonormalizeFrame(normal, tangent, preDisplacementNormal, preDisplacementTangent);

    nwbSkinnedGeometryCopyRestPayload(restBase, deformedBase, restScalarStride);
    nwbSkinnedGeometryDeformedVertexScalars[deformedBase + 0u] = position.x;
    nwbSkinnedGeometryDeformedVertexScalars[deformedBase + 1u] = position.y;
    nwbSkinnedGeometryDeformedVertexScalars[deformedBase + 2u] = position.z;
    nwbSkinnedGeometryDeformedVertexScalars[deformedBase + 3u] = normal.x;
    nwbSkinnedGeometryDeformedVertexScalars[deformedBase + 4u] = normal.y;
    nwbSkinnedGeometryDeformedVertexScalars[deformedBase + 5u] = normal.z;
    nwbSkinnedGeometryDeformedVertexScalars[deformedBase + 6u] = tangent.x;
    nwbSkinnedGeometryDeformedVertexScalars[deformedBase + 7u] = tangent.y;
    nwbSkinnedGeometryDeformedVertexScalars[deformedBase + 8u] = tangent.z;
    nwbSkinnedGeometryDeformedVertexScalars[deformedBase + 9u] = tangent.w;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

