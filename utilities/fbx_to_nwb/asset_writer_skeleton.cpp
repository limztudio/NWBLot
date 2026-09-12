// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset_writer_skeleton.h"

#include <core/common/log.h>

#include <global/simdmath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AssetWriterSkeletonDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr f32 s_InvertibleJointDeterminantEpsilon = 0.000000000001f;
static constexpr usize s_PositionSkinKeySkinShiftBits = 32u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ValidateStreamIndex(const u32 index, const usize count, const char* fieldName, const AStringView context){
    if(index < count)
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("{}: vertex_ref {} index is out of range"), StringConvert(context), StringConvert(fieldName));
    return false;
}

bool ValidateMeshGeometry(const SourceMeshStreams& mesh, const AStringView context){
    if(mesh.positions.empty() || mesh.normals.empty() || mesh.uv0.empty() || mesh.colors.empty() || mesh.vertexRefs.empty() || mesh.indices.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: mesh payload is incomplete"), StringConvert(context));
        return false;
    }
    if(mesh.tangents.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: mesh tangent stream is required"), StringConvert(context));
        return false;
    }
    if((mesh.indices.size() % s_TriangleIndexCount) != 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: mesh index stream must contain whole triangles"), StringConvert(context));
        return false;
    }

    for(const SourceVertexRef& ref : mesh.vertexRefs){
        if(!ValidateStreamIndex(ref.position, mesh.positions.size(), "position", context))
            return false;
        if(!ValidateStreamIndex(ref.normal, mesh.normals.size(), "normal", context))
            return false;
        if(ref.tangent == s_MissingSourceStreamIndex){
            NWB_LOGGER_ERROR(NWB_TEXT("{}: mesh vertex_ref tangent is missing"), StringConvert(context));
            return false;
        }
        if(!ValidateStreamIndex(ref.tangent, mesh.tangents.size(), "tangent", context))
            return false;
        if(!ValidateStreamIndex(ref.uv0, mesh.uv0.size(), "uv0", context))
            return false;
        if(!ValidateStreamIndex(ref.color, mesh.colors.size(), "color", context))
            return false;
    }

    for(const u32 index : mesh.indices){
        if(index >= mesh.vertexRefs.size()){
            NWB_LOGGER_ERROR(NWB_TEXT("{}: mesh triangle index references an out-of-range vertex_ref"), StringConvert(context));
            return false;
        }
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool InvertJointMatrix(const SIMDMatrix& matrix, SIMDMatrix& outInverse){
    SIMDVector determinant;
    outInverse = MatrixInverse(&determinant, matrix);

    return VectorIsFinite(determinant, VectorComponentMask::s_XYZW)
        && Vector4Greater(VectorAbs(determinant), VectorReplicate(s_InvertibleJointDeterminantEpsilon))
        && !MatrixIsNaN(outInverse)
        && !MatrixIsInfinite(outInverse)
    ;
}

bool BuildLocalBindPoseMatrix(
    const SIMDMatrix& globalBindPose,
    const SIMDMatrix* parentGlobalBindPose,
    SIMDMatrix& outLocalBindPose
){
    if(!parentGlobalBindPose){
        outLocalBindPose = globalBindPose;
        return true;
    }

    SIMDMatrix parentInverse;
    if(!InvertJointMatrix(*parentGlobalBindPose, parentInverse))
        return false;

    outLocalBindPose = MatrixMultiply(parentInverse, globalBindPose);
    if(MatrixIsNaN(outLocalBindPose) || MatrixIsInfinite(outLocalBindPose))
        return false;

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ValidateSplitSkinSource(
    const SourceMeshStreams& mesh,
    const UtilityVector<ufbx_node*>& skeletonJoints,
    const UtilityVector<JointMatrix>& skeletonBindPoseMatrices,
    const UtilityVector<JointMatrix>& inverseBindMatrices
){
    if(mesh.skin.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB model: skinned model requires source skin influences"));
        return false;
    }
    if(skeletonJoints.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB model: skinned model requires skeleton joints"));
        return false;
    }
    if(skeletonBindPoseMatrices.size() != skeletonJoints.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB model: skeleton bind-pose matrix count must match joint count"));
        return false;
    }
    if(inverseBindMatrices.size() != skeletonJoints.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB model: skin inverse bind matrix count must match joint count"));
        return false;
    }
    return true;
}


AString NodeName(const ufbx_node* node, const usize fallbackIndex){
    AString name;
    if(node && node->name.data && node->name.length != 0u)
        name.assign(node->name.data, node->name.length);
    if(!name.empty())
        return name;

    AStringStream out;
    out << "joint_" << fallbackIndex;
    return out.str();
}

bool BuildSkeletonOutputData(
    const UtilityVector<ufbx_node*>& joints,
    const UtilityVector<JointMatrix>& bindPoseMatrices,
    const UtilityVector<JointMatrix>& inverseBindMatrices,
    SkeletonOutputData& outData
){
    outData = {};

    if(joints.size() != bindPoseMatrices.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB skeleton: joint count must match bind-pose matrix count"));
        return false;
    }
    if(!inverseBindMatrices.empty() && inverseBindMatrices.size() != joints.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB skeleton: joint count must match inverse bind matrix count"));
        return false;
    }
    if(joints.size() > s_MaxSkeletonJointCount){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB skeleton: skeleton has more than {} joints"), s_MaxSkeletonJointCount);
        return false;
    }

    HashMap<const ufbx_node*, usize> sourceJointLookup;
    sourceJointLookup.reserve(joints.size());
    UtilityVector<AString> sortNames;
    sortNames.reserve(joints.size());
    for(usize jointIndex = 0u; jointIndex < joints.size(); ++jointIndex){
        ufbx_node* joint = joints[jointIndex];
        if(!joint){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB skeleton: skeleton joint {} is null"), jointIndex);
            return false;
        }
        if(sourceJointLookup.find(joint) != sourceJointLookup.end()){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB skeleton: skeleton contains duplicate joint node at index {}"), jointIndex);
            return false;
        }
        sourceJointLookup.emplace(joint, jointIndex);
        sortNames.push_back(NodeName(joint, jointIndex));
    }

    UtilityVector<u32> parentIndices;
    parentIndices.resize(joints.size(), s_MissingSourceStreamIndex);
    for(usize jointIndex = 0u; jointIndex < joints.size(); ++jointIndex){
        const ufbx_node* parent = joints[jointIndex]->parent;
        if(!parent)
            continue;

        const auto foundParent = sourceJointLookup.find(parent);
        if(foundParent != sourceJointLookup.end())
            parentIndices[jointIndex] = static_cast<u32>(foundParent.value());
    }

    UtilityVector<u32> parentDepths;
    parentDepths.resize(joints.size(), 0u);
    for(usize jointIndex = 0u; jointIndex < joints.size(); ++jointIndex){
        u32 depth = 0u;
        u32 parentIndex = parentIndices[jointIndex];
        for(usize guard = 0u; parentIndex != s_MissingSourceStreamIndex; ++guard){
            if(guard >= joints.size()){
                NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB skeleton: skeleton hierarchy contains a cycle"));
                return false;
            }
            ++depth;
            parentIndex = parentIndices[parentIndex];
        }
        parentDepths[jointIndex] = depth;
    }

    UtilityVector<usize> sortedJointIndices;
    sortedJointIndices.reserve(joints.size());
    for(usize jointIndex = 0u; jointIndex < joints.size(); ++jointIndex)
        sortedJointIndices.push_back(jointIndex);
    Sort(
        sortedJointIndices.begin(),
        sortedJointIndices.end(),
        [&parentDepths, &sortNames](const usize lhs, const usize rhs){
            if(parentDepths[lhs] != parentDepths[rhs])
                return parentDepths[lhs] < parentDepths[rhs];
            if(sortNames[lhs] != sortNames[rhs])
                return sortNames[lhs] < sortNames[rhs];
            return lhs < rhs;
        }
    );

    outData.oldToNewJointIndices.resize(joints.size(), 0u);
    outData.joints.reserve(joints.size());
    outData.bindPoseMatrices.reserve(bindPoseMatrices.size());
    outData.inverseBindMatrices.reserve(inverseBindMatrices.size());

    for(const usize oldJointIndex : sortedJointIndices){
        const u32 parentIndex = parentIndices[oldJointIndex];
        const JointMatrix* parentGlobalBindPose = parentIndex != s_MissingSourceStreamIndex
            ? &bindPoseMatrices[parentIndex]
            : nullptr
        ;
        const SIMDMatrix globalBindPoseMatrix = LoadFloat(bindPoseMatrices[oldJointIndex]);

        SIMDMatrix parentGlobalBindPoseMatrix{};
        const SIMDMatrix* parentGlobalBindPoseMatrixPtr = nullptr;
        if(parentGlobalBindPose){
            parentGlobalBindPoseMatrix = LoadFloat(*parentGlobalBindPose);
            parentGlobalBindPoseMatrixPtr = &parentGlobalBindPoseMatrix;
        }

        SIMDMatrix localBindPoseMatrix;
        if(!BuildLocalBindPoseMatrix(globalBindPoseMatrix, parentGlobalBindPoseMatrixPtr, localBindPoseMatrix)){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB skeleton: failed to build local bind pose for joint '{}'"), StringConvert(sortNames[oldJointIndex]));
            return false;
        }
        JointMatrix localBindPose{};
        StoreFloat(localBindPoseMatrix, localBindPose);

        outData.oldToNewJointIndices[oldJointIndex] = static_cast<u16>(outData.joints.size());
        outData.joints.push_back(joints[oldJointIndex]);
        outData.bindPoseMatrices.push_back(localBindPose);
        if(!inverseBindMatrices.empty())
            outData.inverseBindMatrices.push_back(inverseBindMatrices[oldJointIndex]);
    }

    return true;
}

bool RemapSkinInfluences(
    UtilityVector<MeshSkinInfluence>& inOutInfluences,
    const UtilityVector<u16>& oldToNewJointIndices
){
    for(MeshSkinInfluence& influence : inOutInfluences){
        for(usize slot = 0u; slot < s_MeshSkinInfluenceCount; ++slot){
            const u16 oldJointIndex = influence.joint[slot];
            if(oldJointIndex >= oldToNewJointIndices.size()){
                NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB skin: skin influence references out-of-range joint {}"), oldJointIndex);
                return false;
            }
            influence.joint[slot] = oldToNewJointIndices[oldJointIndex];
        }
    }
    return true;
}

u64 PositionSkinKey(const u32 position, const u32 skin){
    return (static_cast<u64>(position) << s_PositionSkinKeySkinShiftBits) | static_cast<u64>(skin);
}

bool BuildPositionAlignedSkinnedMesh(
    const SourceMeshStreams& sourceMesh,
    SourceMeshStreams& outMesh,
    UtilityVector<MeshSkinInfluence>& outPositionSkin
){
    outMesh = SourceMeshStreams{};
    outPositionSkin.clear();

    if(!ValidateSkinnedModelSourceMesh(sourceMesh))
        return false;

    outMesh.normals = sourceMesh.normals;
    outMesh.tangents = sourceMesh.tangents;
    outMesh.uv0 = sourceMesh.uv0;
    outMesh.colors = sourceMesh.colors;
    outMesh.indices = sourceMesh.indices;
    outMesh.vertexRefs.reserve(sourceMesh.vertexRefs.size());
    outMesh.positions.reserve(sourceMesh.vertexRefs.size());
    outPositionSkin.reserve(sourceMesh.vertexRefs.size());

    HashMap<u64, u32> positionSkinLookup;
    positionSkinLookup.reserve(sourceMesh.vertexRefs.size());
    for(const SourceVertexRef& sourceRef : sourceMesh.vertexRefs){
        const u64 key = PositionSkinKey(sourceRef.position, sourceRef.skin);
        u32 positionIndex = 0u;
        const auto foundPosition = positionSkinLookup.find(key);
        if(foundPosition != positionSkinLookup.end()){
            positionIndex = foundPosition.value();
        }
        else{
            if(outMesh.positions.size() > static_cast<usize>(Limit<u32>::s_Max)){
                NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB model: split skinned mesh has too many positions"));
                return false;
            }

            positionIndex = static_cast<u32>(outMesh.positions.size());
            outMesh.positions.push_back(sourceMesh.positions[sourceRef.position]);
            outPositionSkin.push_back(sourceMesh.skin[sourceRef.skin]);
            positionSkinLookup.emplace(key, positionIndex);
        }

        SourceVertexRef ref = sourceRef;
        ref.position = positionIndex;
        ref.skin = s_MissingSourceStreamIndex;
        outMesh.vertexRefs.push_back(ref);
    }

    return true;
}

bool ValidateSkinnedModelSourceMesh(const SourceMeshStreams& mesh){
    static constexpr AStringView s_Context = "Failed to write NWB model";
    if(!ValidateMeshGeometry(mesh, s_Context))
        return false;
    if(mesh.skin.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB model: skinned model source mesh requires skin influences"));
        return false;
    }
    for(const SourceVertexRef& ref : mesh.vertexRefs){
        if(!ValidateStreamIndex(ref.skin, mesh.skin.size(), "skin", s_Context))
            return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

