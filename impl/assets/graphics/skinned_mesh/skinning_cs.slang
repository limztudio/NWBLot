// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "binding_slots.h"
#include "bindless_resources.slangi"
#include "constants.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NwbSkinnedMeshPushConstants{
    uint4 payload0;
    uint4 payload1;
};

[[vk::push_constant]]
ConstantBuffer<NwbSkinnedMeshPushConstants> g_NwbSkinnedMeshPushConstants;

// All persistent mesh streams, including this per-runtime selector payload, are global heap entries. The selector
// slot occupies payload1.y (word 5) and is uniform for the entire dispatch.
#define g_NwbSkinnedMeshBindlessResources nwbSkinnedMeshHeapBindlessResources(\
    g_NwbSkinnedMeshPushConstants.payload1[NWB_SKINNED_MESH_PUSH_BINDLESS_RESOURCES_SLOT - 4u]\
)


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
    const NwbSkinnedMeshJointMatrix jointMatrix = nwbSkinnedMeshHeapJointPalette(g_NwbSkinnedMeshBindlessResources.streamSlots2.w)[jointId];
    return float4x4(
        jointMatrix.row0,
        jointMatrix.row1,
        jointMatrix.row2,
        float4(0.0, 0.0, 0.0, 1.0)
    );
}

bool nwbSkinnedMeshTransformJointNormal(const float3x3 jointMatrix, const float3 normal, out float3 transformedNormal){
    transformedNormal = normal;

    const float3 row0 = jointMatrix[0];
    const float3 row1 = jointMatrix[1];
    const float3 row2 = jointMatrix[2];
    const float determinant = dot(row0, cross(row1, row2));
    if(!nwbFiniteFloat(determinant) || abs(determinant) <= NWB_SKINNED_MESH_EPSILON)
        return false;

    const float inverseDeterminant = 1.0 / determinant;
    const float3x3 normalMatrix = float3x3(
        cross(row1, row2) * inverseDeterminant,
        cross(row2, row0) * inverseDeterminant,
        cross(row0, row1) * inverseDeterminant
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

bool nwbSkinnedMeshApplyLinearSkinPosition(const uint skinIndex, inout float3 position){
    const uint jointCount = nwbSkinnedMeshJointCount();
    if(skinIndex >= nwbSkinnedMeshSkinCount() || jointCount == 0u)
        return true;

    const NwbSkinnedMeshSkinInfluence skin = nwbSkinnedMeshHeapSkinInfluences(g_NwbSkinnedMeshBindlessResources.streamSlots2.z)[skinIndex];
    float3 skinnedPosition = float3(0.0);
    float totalWeight = 0.0;
    // Preserve the legacy influence filter: position skinning formerly passed this normal to the normal-matrix check.
    const float3 influenceValidationNormal = float3(0.0, 0.0, 1.0);

    for(uint influenceIndex = 0u; influenceIndex < NWB_SKINNED_MESH_MAX_INFLUENCE_COUNT; ++influenceIndex){
        const float weight = skin.weight[influenceIndex];
        const uint jointId = skin.joint[influenceIndex];
        if(abs(weight) <= NWB_SKINNED_MESH_EPSILON || jointId >= jointCount)
            continue;

        const float4x4 jointMatrix = nwbSkinnedMeshLoadJointMatrix(jointId);
        const float3x3 jointMatrix3 = float3x3(jointMatrix);
        float3 transformedValidationNormal = float3(0.0);
        if(!nwbSkinnedMeshTransformJointNormal(jointMatrix3, influenceValidationNormal, transformedValidationNormal))
            continue;

        skinnedPosition += weight * mul(jointMatrix, float4(position, 1.0)).xyz;
        totalWeight += weight;
    }

    if(abs(totalWeight) <= NWB_SKINNED_MESH_EPSILON)
        return true;

    const float inverseTotalWeight = 1.0 / totalWeight;
    position = skinnedPosition * inverseTotalWeight;
    return true;
}

bool nwbSkinnedMeshApplyLinearSkinFrame(const uint skinIndex, inout float3 normal, inout float4 tangent){
    const uint jointCount = nwbSkinnedMeshJointCount();
    if(skinIndex >= nwbSkinnedMeshSkinCount() || jointCount == 0u)
        return true;

    const NwbSkinnedMeshSkinInfluence skin = nwbSkinnedMeshHeapSkinInfluences(g_NwbSkinnedMeshBindlessResources.streamSlots2.z)[skinIndex];
    float3 skinnedNormal = float3(0.0);
    float3 skinnedTangent = float3(0.0);
    float totalWeight = 0.0;

    for(uint influenceIndex = 0u; influenceIndex < NWB_SKINNED_MESH_MAX_INFLUENCE_COUNT; ++influenceIndex){
        const float weight = skin.weight[influenceIndex];
        const uint jointId = skin.joint[influenceIndex];
        if(abs(weight) <= NWB_SKINNED_MESH_EPSILON || jointId >= jointCount)
            continue;

        const float4x4 jointMatrix = nwbSkinnedMeshLoadJointMatrix(jointId);
        const float3x3 jointMatrix3 = float3x3(jointMatrix);
        float3 transformedNormal = float3(0.0);
        if(!nwbSkinnedMeshTransformJointNormal(jointMatrix3, normal, transformedNormal))
            continue;

        skinnedNormal += weight * transformedNormal;
        skinnedTangent += weight * mul(jointMatrix3, tangent.xyz);
        totalWeight += weight;
    }

    if(abs(totalWeight) <= NWB_SKINNED_MESH_EPSILON)
        return true;

    const float inverseTotalWeight = 1.0 / totalWeight;
    normal = skinnedNormal * inverseTotalWeight;
    tangent.xyz = skinnedTangent * inverseTotalWeight;
    return true;
}

bool nwbSkinnedMeshBuildDualQuaternionSkin(
    const uint skinIndex,
    out bool hasSkin,
    out float4 blendedReal,
    out float4 blendedDual
){
    hasSkin = false;
    blendedReal = float4(0.0);
    blendedDual = float4(0.0);

    const uint jointCount = nwbSkinnedMeshJointCount();
    if(skinIndex >= nwbSkinnedMeshSkinCount() || jointCount == 0u)
        return true;

    const NwbSkinnedMeshSkinInfluence skin = nwbSkinnedMeshHeapSkinInfluences(g_NwbSkinnedMeshBindlessResources.streamSlots2.z)[skinIndex];
    float4 referenceReal = float4(0.0);
    bool hasReference = false;
    float totalWeight = 0.0;

    for(uint influenceIndex = 0u; influenceIndex < NWB_SKINNED_MESH_MAX_INFLUENCE_COUNT; ++influenceIndex){
        const float weight = skin.weight[influenceIndex];
        const uint jointId = skin.joint[influenceIndex];
        if(abs(weight) <= NWB_SKINNED_MESH_EPSILON || jointId >= jointCount)
            continue;

        const NwbSkinnedMeshJointMatrix jointPayload = nwbSkinnedMeshHeapJointPalette(g_NwbSkinnedMeshBindlessResources.streamSlots2.w)[jointId];
        float4 real = jointPayload.row0;
        float4 dual = jointPayload.row1;
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

    hasSkin = true;
    return true;
}

bool nwbSkinnedMeshApplyDualQuaternionSkinPosition(const uint skinIndex, inout float3 position){
    bool hasSkin = false;
    float4 blendedReal = float4(0.0);
    float4 blendedDual = float4(0.0);
    if(!nwbSkinnedMeshBuildDualQuaternionSkin(skinIndex, hasSkin, blendedReal, blendedDual))
        return false;
    if(!hasSkin)
        return true;

    position = nwbSkinnedMeshTransformDualQuaternionPosition(blendedReal, blendedDual, position);
    return true;
}

bool nwbSkinnedMeshApplyDualQuaternionSkinFrame(const uint skinIndex, inout float3 normal, inout float4 tangent){
    bool hasSkin = false;
    float4 blendedReal = float4(0.0);
    float4 blendedDual = float4(0.0);
    if(!nwbSkinnedMeshBuildDualQuaternionSkin(skinIndex, hasSkin, blendedReal, blendedDual))
        return false;
    if(!hasSkin)
        return true;

    normal = nwbRotateVectorByQuaternion(normal, blendedReal);
    tangent.xyz = nwbRotateVectorByQuaternion(tangent.xyz, blendedReal);
    return true;
}

void nwbSkinnedMeshApplySkinPosition(const uint skinIndex, inout float3 position){
    const bool skinApplied = nwbSkinnedMeshSkinningMode() == NWB_SKINNED_MESH_SKINNING_MODE_DUAL_QUATERNION
        ? nwbSkinnedMeshApplyDualQuaternionSkinPosition(skinIndex, position)
        : nwbSkinnedMeshApplyLinearSkinPosition(skinIndex, position)
    ;
    if(!skinApplied)
        return;
}

void nwbSkinnedMeshApplySkinFrame(const uint skinIndex, inout float3 normal, inout float4 tangent){
    const bool skinApplied = nwbSkinnedMeshSkinningMode() == NWB_SKINNED_MESH_SKINNING_MODE_DUAL_QUATERNION
        ? nwbSkinnedMeshApplyDualQuaternionSkinFrame(skinIndex, normal, tangent)
        : nwbSkinnedMeshApplyLinearSkinFrame(skinIndex, normal, tangent)
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
        NwbHeapRawBuffer(g_NwbSkinnedMeshBindlessResources.streamSlots1.w),
        meshlet,
        decodeInfo,
        localPositionIndex,
        true
    );
    float3 position = nwbMeshLoadPosition(nwbSkinnedMeshHeapPositions(g_NwbSkinnedMeshBindlessResources.streamSlots0.x)[positionRef.position]);
    const float3 preSkinPosition = position;
    nwbSkinnedMeshApplySkinPosition(positionRef.skin, position);
    if(!nwbFiniteFloat3(position))
        position = preSkinPosition;

    nwbSkinnedMeshHeapWritablePositions(g_NwbSkinnedMeshBindlessResources.streamSlots0.y)[positionRef.position] = nwbMeshStorePosition(position);
}

void nwbSkinnedMeshSkinAttribute(
    const NwbMeshletDesc meshlet,
    const NwbMeshletRefDecodeInfo decodeInfo,
    const uint localAttributeIndex
){
    const NwbMeshletAttributeStreamRef attributeRef = nwbMeshletDecodeAttributeRef(
        NwbHeapRawBuffer(g_NwbSkinnedMeshBindlessResources.streamSlots2.x),
        meshlet,
        decodeInfo,
        localAttributeIndex
    );
    const uint attributeId = attributeRef.normal;

    float3 normal = nwbMeshLoadHalf4(nwbSkinnedMeshHeapHalf4s(g_NwbSkinnedMeshBindlessResources.streamSlots0.z)[attributeId]).xyz;
    float4 tangent = nwbMeshLoadHalf4(nwbSkinnedMeshHeapHalf4s(g_NwbSkinnedMeshBindlessResources.streamSlots1.x)[attributeId]);
    const float3 restNormal = normal;
    const float4 restTangent = tangent;

    nwbOrthonormalizeFrame(normal, tangent, restNormal, restTangent);
    const float3 preSkinNormal = normal;
    const float4 preSkinTangent = tangent;
    nwbSkinnedMeshApplySkinFrame(nwbSkinnedMeshHeapUints(g_NwbSkinnedMeshBindlessResources.streamSlots2.y)[attributeId], normal, tangent);
    if(!nwbFiniteFloat3(normal))
        normal = preSkinNormal;
    if(!nwbFiniteFloat4(tangent))
        tangent = preSkinTangent;
    nwbOrthonormalizeFrame(normal, tangent, preSkinNormal, preSkinTangent);

    nwbSkinnedMeshHeapWritableHalf4s(g_NwbSkinnedMeshBindlessResources.streamSlots0.w)[attributeId] = nwbMeshStoreHalf4(float4(normal, 0.0));
    nwbSkinnedMeshHeapWritableHalf4s(g_NwbSkinnedMeshBindlessResources.streamSlots1.y)[attributeId] = nwbMeshStoreHalf4(tangent);
}

void nwbSkinnedMeshMain(const uint meshletIndex, const uint groupThreadIndex){
    if(meshletIndex >= nwbSkinnedMeshMeshletCount())
        return;

    const NwbMeshletDesc meshlet = nwbSkinnedMeshHeapMeshlets(g_NwbSkinnedMeshBindlessResources.streamSlots1.z)[meshletIndex];
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


#undef g_NwbSkinnedMeshBindlessResources


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

