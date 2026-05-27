// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../common/math.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NwbMeshVertexRef{
    uint position;
    uint normal;
    uint tangent;
    uint uv0;
    uint color;
    uint skin;
};

struct NwbMeshHalf4{
    uint xy;
    uint zw;
};

struct NwbMeshPosition{
    float x;
    float y;
    float z;
};

struct NwbSkinnedMeshSkinInfluence{
    uint4 joint;
    float4 weight;
};

struct NwbSkinnedMeshJointMatrix{
    float4 column0;
    float4 column1;
    float4 column2;
    float4 column3;
};

[[vk::binding(0, 0)]] StructuredBuffer<NwbMeshPosition, Std430DataLayout> g_NwbSkinnedMeshRestPositions;
[[vk::binding(1, 0)]] RWStructuredBuffer<NwbMeshPosition, Std430DataLayout> g_NwbSkinnedMeshSkinnedPositions;
[[vk::binding(2, 0)]] StructuredBuffer<NwbMeshHalf4, Std430DataLayout> g_NwbSkinnedMeshRestNormals;
[[vk::binding(3, 0)]] RWStructuredBuffer<NwbMeshHalf4, Std430DataLayout> g_NwbSkinnedMeshSkinnedNormals;
[[vk::binding(4, 0)]] StructuredBuffer<NwbMeshHalf4, Std430DataLayout> g_NwbSkinnedMeshRestTangents;
[[vk::binding(5, 0)]] RWStructuredBuffer<NwbMeshHalf4, Std430DataLayout> g_NwbSkinnedMeshSkinnedTangents;
[[vk::binding(6, 0)]] StructuredBuffer<NwbMeshVertexRef, Std430DataLayout> g_NwbSkinnedMeshVertexRefs;
[[vk::binding(7, 0)]] StructuredBuffer<NwbSkinnedMeshSkinInfluence, Std430DataLayout> g_NwbSkinnedMeshSkinInfluences;
[[vk::binding(8, 0)]] StructuredBuffer<NwbSkinnedMeshJointMatrix, Std430DataLayout> g_NwbSkinnedMeshJointPalette;

struct NwbSkinnedMeshPushConstants{
    uint4 payload0;
    uint4 payload1;
};

[[vk::push_constant]]
ConstantBuffer<NwbSkinnedMeshPushConstants> g_NwbSkinnedMeshPushConstants;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


uint nwbSkinnedMeshVertexCount(){
    return g_NwbSkinnedMeshPushConstants.payload0.x;
}

uint nwbSkinnedMeshSkinCount(){
    return g_NwbSkinnedMeshPushConstants.payload0.y;
}

uint nwbSkinnedMeshJointCount(){
    return g_NwbSkinnedMeshPushConstants.payload0.z;
}

uint nwbSkinnedMeshSkinningMode(){
    return g_NwbSkinnedMeshPushConstants.payload0.w;
}

float4 nwbSkinnedMeshLoadHalf4(const NwbMeshHalf4 value){
    return float4(
        nwbUnpackHalf2x16(value.xy),
        nwbUnpackHalf2x16(value.zw)
    );
}

float3 nwbSkinnedMeshLoadPosition(const NwbMeshPosition value){
    return float3(value.x, value.y, value.z);
}

NwbMeshPosition nwbSkinnedMeshStorePosition(const float3 value){
    NwbMeshPosition storedValue;
    storedValue.x = value.x;
    storedValue.y = value.y;
    storedValue.z = value.z;
    return storedValue;
}

NwbMeshHalf4 nwbSkinnedMeshStoreHalf4(const float4 value){
    NwbMeshHalf4 packedValue;
    packedValue.xy = nwbPackHalf2x16(value.xy);
    packedValue.zw = nwbPackHalf2x16(value.zw);
    return packedValue;
}

float4x4 nwbSkinnedMeshLoadJointMatrix(const uint jointId){
    const NwbSkinnedMeshJointMatrix jointMatrix = g_NwbSkinnedMeshJointPalette[jointId];
    return float4x4(
        jointMatrix.column0,
        jointMatrix.column1,
        jointMatrix.column2,
        jointMatrix.column3
    );
}

bool nwbSkinnedMeshTransformJointNormal(const float3x3 jointMatrix, const float3 normal, out float3 transformedNormal){
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

bool nwbSkinnedMeshNormalizeDualQuaternion(inout float4 real, inout float4 dual){
    const float lengthSquared = dot(real, real);
    if(!nwbFiniteFloat(lengthSquared) || lengthSquared <= 0.000001)
        return false;

    const float invLength = rsqrt(lengthSquared);
    real *= invLength;
    dual *= invLength;
    dual -= real * dot(real, dual);
    return nwbFiniteFloat4(real) && nwbFiniteFloat4(dual);
}

float3 nwbSkinnedMeshTransformDualQuaternionPosition(const float4 real, const float4 dual, const float3 position){
    const float3 rotatedPosition = nwbRotateVectorByQuaternion(position, real);
    const float3 translation = 2.0 * nwbQuaternionMultiply(
        dual,
        float4(-real.xyz, real.w)
    ).xyz;
    return rotatedPosition + translation;
}

bool nwbSkinnedMeshApplyLinearSkin(const uint skinIndex, inout float3 position, inout float3 normal, inout float4 tangent){
    const uint jointCount = nwbSkinnedMeshJointCount();
    if(skinIndex >= nwbSkinnedMeshSkinCount() || jointCount == 0u)
        return true;

    const NwbSkinnedMeshSkinInfluence skin = g_NwbSkinnedMeshSkinInfluences[skinIndex];
    float3 skinnedPosition = float3(0.0);
    float3 skinnedNormal = float3(0.0);
    float3 skinnedTangent = float3(0.0);
    float totalWeight = 0.0;

    for(uint influenceIndex = 0u; influenceIndex < 4u; ++influenceIndex){
        const float weight = skin.weight[influenceIndex];
        const uint jointId = skin.joint[influenceIndex];
        if(abs(weight) <= 0.000001 || jointId >= jointCount)
            continue;

        const float4x4 jointMatrix = nwbSkinnedMeshLoadJointMatrix(jointId);
        const float3x3 jointMatrix3 = float3x3(jointMatrix);
        float3 transformedNormal = float3(0.0);
        if(!nwbSkinnedMeshTransformJointNormal(jointMatrix3, normal, transformedNormal))
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

bool nwbSkinnedMeshApplyDualQuaternionSkin(const uint skinIndex, inout float3 position, inout float3 normal, inout float4 tangent){
    const uint jointCount = nwbSkinnedMeshJointCount();
    if(skinIndex >= nwbSkinnedMeshSkinCount() || jointCount == 0u)
        return true;

    const NwbSkinnedMeshSkinInfluence skin = g_NwbSkinnedMeshSkinInfluences[skinIndex];
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

        const NwbSkinnedMeshJointMatrix jointPayload = g_NwbSkinnedMeshJointPalette[jointId];
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
    if(!nwbSkinnedMeshNormalizeDualQuaternion(blendedReal, blendedDual))
        return false;

    position = nwbSkinnedMeshTransformDualQuaternionPosition(blendedReal, blendedDual, position);
    normal = nwbRotateVectorByQuaternion(normal, blendedReal);
    tangent.xyz = nwbRotateVectorByQuaternion(tangent.xyz, blendedReal);
    return true;
}

void nwbSkinnedMeshApplySkin(const uint skinIndex, inout float3 position, inout float3 normal, inout float4 tangent){
    const uint dualQuaternionSkinningMode = 1u;
    const bool skinApplied = nwbSkinnedMeshSkinningMode() == dualQuaternionSkinningMode
        ? nwbSkinnedMeshApplyDualQuaternionSkin(skinIndex, position, normal, tangent)
        : nwbSkinnedMeshApplyLinearSkin(skinIndex, position, normal, tangent)
    ;
    if(!skinApplied)
        return;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void nwbSkinnedMeshMain(uint vertexId){
    if(vertexId >= nwbSkinnedMeshVertexCount())
        return;

    const NwbMeshVertexRef vertexRef = g_NwbSkinnedMeshVertexRefs[vertexId];
    float3 position = nwbSkinnedMeshLoadPosition(g_NwbSkinnedMeshRestPositions[vertexRef.position]);
    float3 normal = nwbSkinnedMeshLoadHalf4(g_NwbSkinnedMeshRestNormals[vertexRef.normal]).xyz;
    float4 tangent = nwbSkinnedMeshLoadHalf4(g_NwbSkinnedMeshRestTangents[vertexRef.tangent]);
    const float3 restNormal = normal;
    const float4 restTangent = tangent;

    nwbOrthonormalizeFrame(normal, tangent, restNormal, restTangent);
    const float3 preSkinPosition = position;
    const float3 preSkinNormal = normal;
    const float4 preSkinTangent = tangent;
    nwbSkinnedMeshApplySkin(vertexRef.skin, position, normal, tangent);
    if(!nwbFiniteFloat3(position))
        position = preSkinPosition;
    if(!nwbFiniteFloat3(normal))
        normal = preSkinNormal;
    if(!nwbFiniteFloat4(tangent))
        tangent = preSkinTangent;
    nwbOrthonormalizeFrame(normal, tangent, preSkinNormal, preSkinTangent);

    g_NwbSkinnedMeshSkinnedPositions[vertexId] = nwbSkinnedMeshStorePosition(position);
    g_NwbSkinnedMeshSkinnedNormals[vertexId] = nwbSkinnedMeshStoreHalf4(float4(normal, 0.0));
    g_NwbSkinnedMeshSkinnedTangents[vertexId] = nwbSkinnedMeshStoreHalf4(tangent);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(64, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    nwbSkinnedMeshMain(dispatchThreadID.x);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

