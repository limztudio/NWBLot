// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#version 460


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


layout(local_size_x = 64) in;

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

layout(std430, binding = 1) buffer NwbSkinnedGeometrySkinnedVerticesBuffer{
    float nwbSkinnedGeometrySkinnedVertexScalars[];
};

layout(std430, binding = 4) readonly buffer NwbSkinnedGeometrySkinInfluencesBuffer{
    NwbSkinnedGeometrySkinInfluence nwbSkinnedGeometrySkinInfluences[];
};

layout(std430, binding = 5) readonly buffer NwbSkinnedGeometryJointPaletteBuffer{
    NwbSkinnedGeometryJointMatrix nwbSkinnedGeometryJointPalette[];
};

layout(push_constant) uniform NwbSkinnedGeometryPushConstants{
    uvec4 payload0;
    uvec4 payload1;
} g_NwbSkinnedGeometryPushConstants;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


uint nwbSkinnedGeometryVertexCount(){
    return g_NwbSkinnedGeometryPushConstants.payload0.x;
}

uint nwbSkinnedGeometryRestScalarStride(){
    return g_NwbSkinnedGeometryPushConstants.payload0.y;
}

uint nwbSkinnedGeometrySkinnedScalarStride(){
    return g_NwbSkinnedGeometryPushConstants.payload0.z;
}

uint nwbSkinnedGeometrySkinCount(){
    return g_NwbSkinnedGeometryPushConstants.payload0.w;
}

uint nwbSkinnedGeometryJointCount(){
    return g_NwbSkinnedGeometryPushConstants.payload1.x;
}

uint nwbSkinnedGeometrySkinningMode(){
    return g_NwbSkinnedGeometryPushConstants.payload1.y;
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

void nwbSkinnedGeometryCopyRestPayload(const uint restBase, const uint skinnedBase, const uint restScalarStride){
    for(uint i = 0u; i < restScalarStride; ++i)
        nwbSkinnedGeometrySkinnedVertexScalars[skinnedBase + i] = nwbSkinnedGeometryRestVertexScalars[restBase + i];
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void main(){
    const uint vertexId = gl_GlobalInvocationID.x;
    if(vertexId >= nwbSkinnedGeometryVertexCount())
        return;

    const uint restScalarStride = nwbSkinnedGeometryRestScalarStride();
    const uint skinnedScalarStride = nwbSkinnedGeometrySkinnedScalarStride();
    const uint restBase = vertexId * restScalarStride;
    const uint skinnedBase = vertexId * skinnedScalarStride;

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
    const vec3 restPosition = position;
    const vec3 restNormal = normal;
    const vec4 restTangent = tangent;

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

    nwbSkinnedGeometryCopyRestPayload(restBase, skinnedBase, restScalarStride);
    nwbSkinnedGeometrySkinnedVertexScalars[skinnedBase + 0u] = position.x;
    nwbSkinnedGeometrySkinnedVertexScalars[skinnedBase + 1u] = position.y;
    nwbSkinnedGeometrySkinnedVertexScalars[skinnedBase + 2u] = position.z;
    nwbSkinnedGeometrySkinnedVertexScalars[skinnedBase + 3u] = normal.x;
    nwbSkinnedGeometrySkinnedVertexScalars[skinnedBase + 4u] = normal.y;
    nwbSkinnedGeometrySkinnedVertexScalars[skinnedBase + 5u] = normal.z;
    nwbSkinnedGeometrySkinnedVertexScalars[skinnedBase + 6u] = tangent.x;
    nwbSkinnedGeometrySkinnedVertexScalars[skinnedBase + 7u] = tangent.y;
    nwbSkinnedGeometrySkinnedVertexScalars[skinnedBase + 8u] = tangent.z;
    nwbSkinnedGeometrySkinnedVertexScalars[skinnedBase + 9u] = tangent.w;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
