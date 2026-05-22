// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "nwb_shader_math.slangi"



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NwbSkinnedGeometrySkinInfluence{
    uint4 joint;
    float4 weight;
};

struct NwbSkinnedGeometryJointMatrix{
    float4 column0;
    float4 column1;
    float4 column2;
    float4 column3;
};

[[vk::binding(0, 0)]] StructuredBuffer<uint, Std430DataLayout> g_NwbSkinnedGeometryRestVertexWords;
[[vk::binding(1, 0)]] RWStructuredBuffer<uint, Std430DataLayout> g_NwbSkinnedGeometrySkinnedVertexWords;
[[vk::binding(4, 0)]] StructuredBuffer<NwbSkinnedGeometrySkinInfluence, Std430DataLayout> g_NwbSkinnedGeometrySkinInfluences;
[[vk::binding(5, 0)]] StructuredBuffer<NwbSkinnedGeometryJointMatrix, Std430DataLayout> g_NwbSkinnedGeometryJointPalette;

struct NwbSkinnedGeometryPushConstants{
    uint4 payload0;
    uint4 payload1;
};

[[vk::push_constant]]
ConstantBuffer<NwbSkinnedGeometryPushConstants> g_NwbSkinnedGeometryPushConstants;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


uint nwbSkinnedGeometryVertexCount(){
    return g_NwbSkinnedGeometryPushConstants.payload0.x;
}

uint nwbSkinnedGeometryRestWordStride(){
    return g_NwbSkinnedGeometryPushConstants.payload0.y;
}

uint nwbSkinnedGeometrySkinnedWordStride(){
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

void nwbSkinnedGeometryCopyRestPayload(const uint restBase, const uint skinnedBase, const uint restWordStride){
    for(uint i = 0u; i < restWordStride; ++i)
        g_NwbSkinnedGeometrySkinnedVertexWords[skinnedBase + i] = g_NwbSkinnedGeometryRestVertexWords[restBase + i];
}

float3 nwbSkinnedGeometryLoadRestPosition(const uint restBase){
    return float3(
        asfloat(g_NwbSkinnedGeometryRestVertexWords[restBase + 0u]),
        asfloat(g_NwbSkinnedGeometryRestVertexWords[restBase + 1u]),
        asfloat(g_NwbSkinnedGeometryRestVertexWords[restBase + 2u])
    );
}

float4 nwbSkinnedGeometryLoadRestHalf4(const uint restBase){
    return float4(
        nwbUnpackHalf2x16(g_NwbSkinnedGeometryRestVertexWords[restBase + 0u]),
        nwbUnpackHalf2x16(g_NwbSkinnedGeometryRestVertexWords[restBase + 1u])
    );
}

void nwbSkinnedGeometryStoreSkinnedPosition(const uint skinnedBase, const float3 position){
    g_NwbSkinnedGeometrySkinnedVertexWords[skinnedBase + 0u] = asuint(position.x);
    g_NwbSkinnedGeometrySkinnedVertexWords[skinnedBase + 1u] = asuint(position.y);
    g_NwbSkinnedGeometrySkinnedVertexWords[skinnedBase + 2u] = asuint(position.z);
}

void nwbSkinnedGeometryStoreSkinnedHalf4(const uint skinnedBase, const float4 value){
    g_NwbSkinnedGeometrySkinnedVertexWords[skinnedBase + 0u] = nwbPackHalf2x16(value.xy);
    g_NwbSkinnedGeometrySkinnedVertexWords[skinnedBase + 1u] = nwbPackHalf2x16(value.zw);
}

float4x4 nwbSkinnedGeometryLoadJointMatrix(const uint jointId){
    const NwbSkinnedGeometryJointMatrix jointMatrix = g_NwbSkinnedGeometryJointPalette[jointId];
    return float4x4(
        jointMatrix.column0,
        jointMatrix.column1,
        jointMatrix.column2,
        jointMatrix.column3
    );
}

bool nwbSkinnedGeometryTransformJointNormal(const float3x3 jointMatrix, const float3 normal, out float3 transformedNormal){
    transformedNormal = normal;

    const float3 column0 = jointMatrix[0];
    const float3 column1 = jointMatrix[1];
    const float3 column2 = jointMatrix[2];
    const float determinant = dot(column0, cross(column1, column2));
    if(!nwbFiniteFloat(determinant) || abs(determinant) <= 0.000001)
        return false;

    const float inverseDeterminant = 1.0 / determinant;
    const float3x3 normalMatrix = float3x3(
        cross(column1, column2) * inverseDeterminant,
        cross(column2, column0) * inverseDeterminant,
        cross(column0, column1) * inverseDeterminant
    );
    transformedNormal = mul(normalMatrix, normal);
    return nwbFiniteFloat3(transformedNormal);
}

bool nwbSkinnedGeometryNormalizeDualQuaternion(inout float4 real, inout float4 dual){
    const float lengthSquared = dot(real, real);
    if(!nwbFiniteFloat(lengthSquared) || lengthSquared <= 0.000001)
        return false;

    const float invLength = rsqrt(lengthSquared);
    real *= invLength;
    dual *= invLength;
    dual -= real * dot(real, dual);
    return nwbFiniteFloat4(real) && nwbFiniteFloat4(dual);
}

float3 nwbSkinnedGeometryTransformDualQuaternionPosition(const float4 real, const float4 dual, const float3 position){
    const float3 rotatedPosition = nwbRotateVectorByQuaternion(position, real);
    const float3 translation = 2.0 * nwbQuaternionMultiply(
        dual,
        float4(-real.xyz, real.w)
    ).xyz;
    return rotatedPosition + translation;
}

bool nwbSkinnedGeometryApplyLinearSkin(const uint vertexId, inout float3 position, inout float3 normal, inout float4 tangent){
    const uint jointCount = nwbSkinnedGeometryJointCount();
    if(nwbSkinnedGeometrySkinCount() != nwbSkinnedGeometryVertexCount() || jointCount == 0u)
        return true;

    const NwbSkinnedGeometrySkinInfluence skin = g_NwbSkinnedGeometrySkinInfluences[vertexId];
    float3 skinnedPosition = float3(0.0);
    float3 skinnedNormal = float3(0.0);
    float3 skinnedTangent = float3(0.0);
    float totalWeight = 0.0;

    for(uint influenceIndex = 0u; influenceIndex < 4u; ++influenceIndex){
        const float weight = skin.weight[influenceIndex];
        const uint jointId = skin.joint[influenceIndex];
        if(abs(weight) <= 0.000001 || jointId >= jointCount)
            continue;

        const float4x4 jointMatrix = nwbSkinnedGeometryLoadJointMatrix(jointId);
        const float3x3 jointMatrix3 = float3x3(jointMatrix);
        float3 transformedNormal = float3(0.0);
        if(!nwbSkinnedGeometryTransformJointNormal(jointMatrix3, normal, transformedNormal))
            continue;

        skinnedPosition += weight * mul(jointMatrix, float4(position, 1.0)).xyz;
        skinnedNormal += weight * transformedNormal;
        skinnedTangent += weight * mul(jointMatrix3, tangent.xyz);
        totalWeight += weight;
    }

    if(abs(totalWeight) <= 0.000001)
        return true;

    const float inverseTotalWeight = 1.0 / totalWeight;
    position = skinnedPosition * inverseTotalWeight;
    normal = skinnedNormal * inverseTotalWeight;
    tangent.xyz = skinnedTangent * inverseTotalWeight;
    return true;
}

bool nwbSkinnedGeometryApplyDualQuaternionSkin(const uint vertexId, inout float3 position, inout float3 normal, inout float4 tangent){
    const uint jointCount = nwbSkinnedGeometryJointCount();
    if(nwbSkinnedGeometrySkinCount() != nwbSkinnedGeometryVertexCount() || jointCount == 0u)
        return true;

    const NwbSkinnedGeometrySkinInfluence skin = g_NwbSkinnedGeometrySkinInfluences[vertexId];
    float4 blendedReal = float4(0.0);
    float4 blendedDual = float4(0.0);
    float4 referenceReal = float4(0.0);
    bool hasReference = false;
    float totalWeight = 0.0;

    for(uint influenceIndex = 0u; influenceIndex < 4u; ++influenceIndex){
        const float weight = skin.weight[influenceIndex];
        const uint jointId = skin.joint[influenceIndex];
        if(abs(weight) <= 0.000001 || jointId >= jointCount)
            continue;

        const NwbSkinnedGeometryJointMatrix jointPayload = g_NwbSkinnedGeometryJointPalette[jointId];
        float4 real = jointPayload.column0;
        float4 dual = jointPayload.column1;
        if(!nwbFiniteFloat4(real) || !nwbFiniteFloat4(dual))
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
    normal = nwbRotateVectorByQuaternion(normal, blendedReal);
    tangent.xyz = nwbRotateVectorByQuaternion(tangent.xyz, blendedReal);
    return true;
}

void nwbSkinnedGeometryApplySkin(const uint vertexId, inout float3 position, inout float3 normal, inout float4 tangent){
    const uint dualQuaternionSkinningMode = 1u;
    const bool skinApplied = nwbSkinnedGeometrySkinningMode() == dualQuaternionSkinningMode
        ? nwbSkinnedGeometryApplyDualQuaternionSkin(vertexId, position, normal, tangent)
        : nwbSkinnedGeometryApplyLinearSkin(vertexId, position, normal, tangent)
    ;
    if(!skinApplied)
        return;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void nwbSkinnedGeometryMain(uint vertexId){
    if(vertexId >= nwbSkinnedGeometryVertexCount())
        return;

    const uint restWordStride = nwbSkinnedGeometryRestWordStride();
    const uint skinnedWordStride = nwbSkinnedGeometrySkinnedWordStride();
    const uint restBase = vertexId * restWordStride;
    const uint skinnedBase = vertexId * skinnedWordStride;

    float3 position = nwbSkinnedGeometryLoadRestPosition(restBase);
    float3 normal = nwbSkinnedGeometryLoadRestHalf4(restBase + 3u).xyz;
    float4 tangent = nwbSkinnedGeometryLoadRestHalf4(restBase + 5u);
    const float3 restNormal = normal;
    const float4 restTangent = tangent;

    nwbOrthonormalizeFrame(normal, tangent, restNormal, restTangent);
    const float3 preSkinPosition = position;
    const float3 preSkinNormal = normal;
    const float4 preSkinTangent = tangent;
    nwbSkinnedGeometryApplySkin(vertexId, position, normal, tangent);
    if(!nwbFiniteFloat3(position))
        position = preSkinPosition;
    if(!nwbFiniteFloat3(normal))
        normal = preSkinNormal;
    if(!nwbFiniteFloat4(tangent))
        tangent = preSkinTangent;
    nwbOrthonormalizeFrame(normal, tangent, preSkinNormal, preSkinTangent);

    nwbSkinnedGeometryCopyRestPayload(restBase, skinnedBase, restWordStride);
    nwbSkinnedGeometryStoreSkinnedPosition(skinnedBase, position);
    nwbSkinnedGeometryStoreSkinnedHalf4(skinnedBase + 3u, float4(normal, 0.0));
    nwbSkinnedGeometryStoreSkinnedHalf4(skinnedBase + 5u, tangent);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(64, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    nwbSkinnedGeometryMain(dispatchThreadID.x);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

