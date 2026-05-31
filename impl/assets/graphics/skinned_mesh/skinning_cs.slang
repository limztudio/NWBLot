// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../mesh/meshlet_payload.slangi"
#include "binding_slots.h"
#include "constants.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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

[[vk::binding(NWB_SKINNED_MESH_BINDING_REST_POSITION, NWB_SKINNED_MESH_SET)]] StructuredBuffer<NwbMeshPosition, Std430DataLayout> g_NwbSkinnedMeshRestPositions;
[[vk::binding(NWB_SKINNED_MESH_BINDING_SKINNED_POSITION, NWB_SKINNED_MESH_SET)]] RWStructuredBuffer<NwbMeshPosition, Std430DataLayout> g_NwbSkinnedMeshSkinnedPositions;
[[vk::binding(NWB_SKINNED_MESH_BINDING_REST_NORMAL, NWB_SKINNED_MESH_SET)]] StructuredBuffer<NwbMeshHalf4, Std430DataLayout> g_NwbSkinnedMeshRestNormals;
[[vk::binding(NWB_SKINNED_MESH_BINDING_SKINNED_NORMAL, NWB_SKINNED_MESH_SET)]] RWStructuredBuffer<NwbMeshHalf4, Std430DataLayout> g_NwbSkinnedMeshSkinnedNormals;
[[vk::binding(NWB_SKINNED_MESH_BINDING_REST_TANGENT, NWB_SKINNED_MESH_SET)]] StructuredBuffer<NwbMeshHalf4, Std430DataLayout> g_NwbSkinnedMeshRestTangents;
[[vk::binding(NWB_SKINNED_MESH_BINDING_SKINNED_TANGENT, NWB_SKINNED_MESH_SET)]] RWStructuredBuffer<NwbMeshHalf4, Std430DataLayout> g_NwbSkinnedMeshSkinnedTangents;
[[vk::binding(NWB_SKINNED_MESH_BINDING_MESHLET_DESC, NWB_SKINNED_MESH_SET)]] StructuredBuffer<NwbMeshletDesc, Std430DataLayout> g_NwbSkinnedMeshMeshlets;
[[vk::binding(NWB_SKINNED_MESH_BINDING_POSITION_REF_DELTAS, NWB_SKINNED_MESH_SET)]] ByteAddressBuffer g_NwbSkinnedMeshPositionRefDeltas;
[[vk::binding(NWB_SKINNED_MESH_BINDING_ATTRIBUTE_REF_DELTAS, NWB_SKINNED_MESH_SET)]] ByteAddressBuffer g_NwbSkinnedMeshAttributeRefDeltas;
[[vk::binding(NWB_SKINNED_MESH_BINDING_ATTRIBUTE_SKINS, NWB_SKINNED_MESH_SET)]] StructuredBuffer<uint, Std430DataLayout> g_NwbSkinnedMeshAttributeSkins;
[[vk::binding(NWB_SKINNED_MESH_BINDING_SKIN_INFLUENCES, NWB_SKINNED_MESH_SET)]] StructuredBuffer<NwbSkinnedMeshSkinInfluence, Std430DataLayout> g_NwbSkinnedMeshSkinInfluences;
[[vk::binding(NWB_SKINNED_MESH_BINDING_JOINT_PALETTE, NWB_SKINNED_MESH_SET)]] StructuredBuffer<NwbSkinnedMeshJointMatrix, Std430DataLayout> g_NwbSkinnedMeshJointPalette;

struct NwbSkinnedMeshPushConstants{
    uint4 payload0;
    uint4 payload1;
};

[[vk::push_constant]]
ConstantBuffer<NwbSkinnedMeshPushConstants> g_NwbSkinnedMeshPushConstants;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


uint nwbSkinnedMeshMeshletCount(){
    return g_NwbSkinnedMeshPushConstants.payload0[NWB_SKINNED_MESH_PUSH_MESHLET_COUNT];
}

uint nwbSkinnedMeshSkinCount(){
    return g_NwbSkinnedMeshPushConstants.payload0[NWB_SKINNED_MESH_PUSH_SKIN_COUNT];
}

uint nwbSkinnedMeshJointCount(){
    return g_NwbSkinnedMeshPushConstants.payload0[NWB_SKINNED_MESH_PUSH_JOINT_COUNT];
}

uint nwbSkinnedMeshSkinningMode(){
    return g_NwbSkinnedMeshPushConstants.payload0[NWB_SKINNED_MESH_PUSH_SKINNING_MODE];
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
    if(!nwbFiniteFloat(determinant) || abs(determinant) <= NWB_SKINNED_MESH_EPSILON)
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
    if(!nwbFiniteFloat(lengthSquared) || lengthSquared <= NWB_SKINNED_MESH_EPSILON)
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
        if(abs(weight) <= NWB_SKINNED_MESH_EPSILON || jointId >= jointCount)
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

    if(abs(totalWeight) <= NWB_SKINNED_MESH_EPSILON)
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
        if(abs(weight) <= NWB_SKINNED_MESH_EPSILON || jointId >= jointCount)
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

    if(abs(totalWeight) <= NWB_SKINNED_MESH_EPSILON)
        return true;
    if(!nwbSkinnedMeshNormalizeDualQuaternion(blendedReal, blendedDual))
        return false;

    position = nwbSkinnedMeshTransformDualQuaternionPosition(blendedReal, blendedDual, position);
    normal = nwbRotateVectorByQuaternion(normal, blendedReal);
    tangent.xyz = nwbRotateVectorByQuaternion(tangent.xyz, blendedReal);
    return true;
}

void nwbSkinnedMeshApplySkin(const uint skinIndex, inout float3 position, inout float3 normal, inout float4 tangent){
    const bool skinApplied = nwbSkinnedMeshSkinningMode() == NWB_SKINNED_MESH_SKINNING_MODE_DUAL_QUATERNION
        ? nwbSkinnedMeshApplyDualQuaternionSkin(skinIndex, position, normal, tangent)
        : nwbSkinnedMeshApplyLinearSkin(skinIndex, position, normal, tangent)
    ;
    if(!skinApplied)
        return;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void nwbSkinnedMeshSkinPosition(
    const NwbMeshletDesc meshlet,
    const NwbMeshletRefDecodeInfo decodeInfo,
    const uint localPositionIndex
){
    const NwbMeshletPositionStreamRef positionRef = nwbMeshletDecodePositionRef(
        g_NwbSkinnedMeshPositionRefDeltas,
        meshlet,
        decodeInfo,
        localPositionIndex,
        true
    );
    float3 position = nwbMeshLoadPosition(g_NwbSkinnedMeshRestPositions[positionRef.position]);
    float3 normal = float3(0.0, 0.0, 1.0);
    float4 tangent = float4(1.0, 0.0, 0.0, 1.0);
    const float3 preSkinPosition = position;
    nwbSkinnedMeshApplySkin(positionRef.skin, position, normal, tangent);
    if(!nwbFiniteFloat3(position))
        position = preSkinPosition;

    g_NwbSkinnedMeshSkinnedPositions[positionRef.position] = nwbMeshStorePosition(position);
}

void nwbSkinnedMeshSkinAttribute(
    const NwbMeshletDesc meshlet,
    const NwbMeshletRefDecodeInfo decodeInfo,
    const uint localAttributeIndex
){
    const NwbMeshletAttributeStreamRef attributeRef = nwbMeshletDecodeAttributeRef(
        g_NwbSkinnedMeshAttributeRefDeltas,
        meshlet,
        decodeInfo,
        localAttributeIndex
    );
    const uint attributeId = attributeRef.normal;

    float3 position = float3(0.0);
    float3 normal = nwbMeshLoadHalf4(g_NwbSkinnedMeshRestNormals[attributeId]).xyz;
    float4 tangent = nwbMeshLoadHalf4(g_NwbSkinnedMeshRestTangents[attributeId]);
    const float3 restNormal = normal;
    const float4 restTangent = tangent;

    nwbOrthonormalizeFrame(normal, tangent, restNormal, restTangent);
    const float3 preSkinNormal = normal;
    const float4 preSkinTangent = tangent;
    nwbSkinnedMeshApplySkin(g_NwbSkinnedMeshAttributeSkins[attributeId], position, normal, tangent);
    if(!nwbFiniteFloat3(normal))
        normal = preSkinNormal;
    if(!nwbFiniteFloat4(tangent))
        tangent = preSkinTangent;
    nwbOrthonormalizeFrame(normal, tangent, preSkinNormal, preSkinTangent);

    g_NwbSkinnedMeshSkinnedNormals[attributeId] = nwbMeshStoreHalf4(float4(normal, 0.0));
    g_NwbSkinnedMeshSkinnedTangents[attributeId] = nwbMeshStoreHalf4(tangent);
}

void nwbSkinnedMeshMain(const uint meshletIndex, const uint groupThreadIndex){
    if(meshletIndex >= nwbSkinnedMeshMeshletCount())
        return;

    const NwbMeshletDesc meshlet = g_NwbSkinnedMeshMeshlets[meshletIndex];
    const NwbMeshletRefDecodeInfo decodeInfo = nwbMeshletBuildRefDecodeInfo(meshlet);
    const uint positionCount = nwbMeshletPositionCount(meshlet);
    const uint attributeCount = nwbMeshletAttributeCount(meshlet);
    for(uint localPositionIndex = groupThreadIndex; localPositionIndex < positionCount; localPositionIndex += NWB_SKINNED_MESH_GROUP_SIZE_X)
        nwbSkinnedMeshSkinPosition(meshlet, decodeInfo, localPositionIndex);
    for(uint localAttributeIndex = groupThreadIndex; localAttributeIndex < attributeCount; localAttributeIndex += NWB_SKINNED_MESH_GROUP_SIZE_X)
        nwbSkinnedMeshSkinAttribute(meshlet, decodeInfo, localAttributeIndex);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_SKINNED_MESH_GROUP_SIZE_X, 1, 1)]
void main(uint3 groupThreadID : SV_GroupThreadID, uint3 groupID : SV_GroupID){
    nwbSkinnedMeshMain(groupID.x, groupThreadID.x);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

