// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset_writer_skeleton.h"
#include "source_mesh_streams.h"

#include <impl/assets_mesh/cook_source_streams.h>

#include <core/common/log.h>

#include <global/simdmath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AssetWriterSkeletonDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<SIMDMatrix> InvertJointMatrix(const SIMDMatrix& matrix)noexcept{
    SIMDVectorSingle determinant;
    const SIMDMatrix inverse = MatrixInverse(&determinant, matrix);
    if(!VectorIsFinite(determinant, VectorComponentMask::s_XYZW) || !Vector4Greater(VectorAbs(determinant), VectorReplicate(s_InvertibleJointDeterminantEpsilon)) || MatrixIsNaN(inverse) || MatrixIsInfinite(inverse))
        return MakeUnexpected(Failure{});
    return inverse;
}

Expected<SIMDMatrix> BuildLocalBindPoseMatrix(const SIMDMatrix& globalBindPose, const SIMDMatrix* parentGlobalBindPose)noexcept{
    if(MatrixIsNaN(globalBindPose) || MatrixIsInfinite(globalBindPose))
        return MakeUnexpected(Failure{});
    if(!parentGlobalBindPose)
        return globalBindPose;

    const auto parentInverse = InvertJointMatrix(*parentGlobalBindPose);
    if(!parentInverse)
        return MakeUnexpected(Failure{});

    const SIMDMatrix localBindPose = MatrixMultiply(*parentInverse, globalBindPose);
    if(MatrixIsNaN(localBindPose) || MatrixIsInfinite(localBindPose))
        return MakeUnexpected(Failure{});
    return localBindPose;
}

b8 ValidateSkinnedModelSourceMesh(const SourceMeshStreams& mesh){
    if(!FbxSourceMeshStreams::ValidateMeshGeometry(mesh, s_WriteNwbModelContext))
        return false;
    return ValidateSkinnedModelSourceMeshRefs(Span<const SourceVertexRef>(mesh.vertexRefs.data(), mesh.vertexRefs.size()), mesh.skin.size());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ValidateSplitSkinSource(const SourceMeshStreams& mesh, const Span<ufbx_node* const> skeletonJoints, const Span<const JointMatrix> skeletonBindPoseMatrices, const Span<const JointMatrix> inverseBindMatrices){
    return ValidateSplitSkinSourceCounts(mesh.skin.size(), skeletonJoints.size(), skeletonBindPoseMatrices.size(), inverseBindMatrices.size());
}

ScratchString NodeName(const ufbx_node* node, const usize fallbackIndex, Core::Alloc::ScratchArena& scratchArena){
    const AStringView nodeName = node ? UfbxStringView(node->name) : AStringView{};
    if(!nodeName.empty())
        return ScratchString(nodeName, scratchArena);

    ScratchString name(scratchArena);
    StringAppendFormat(name, "{}{}", s_JointFallbackNamePrefix, fallbackIndex);
    return name;
}

Expected<SkeletonOutputData> BuildSkeletonOutputData(const Span<ufbx_node* const> joints, const Span<const JointMatrix> bindPoseMatrices, const Span<const JointMatrix> inverseBindMatrices, Core::Alloc::ScratchArena& scratchArena){
    SkeletonOutputData outData;

    if(!ValidateSkeletonOutputInputCounts(joints.size(), bindPoseMatrices.size(), inverseBindMatrices.size()))
        return MakeUnexpected(Failure{});

    UtilityScratchMap<NotNull<const ufbx_node*>, usize> sourceJointLookup(scratchArena);
    sourceJointLookup.reserve(joints.size());
    UtilityScratchVector<ScratchString> sortNames(scratchArena);
    sortNames.reserve(joints.size());
    for(usize jointIndex = 0u; jointIndex < joints.size(); ++jointIndex){
        ufbx_node* joint = joints[jointIndex];
        if(!joint){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB skeleton: skeleton joint {} is null"), jointIndex);
            return MakeUnexpected(Failure{});
        }
        const auto inserted = sourceJointLookup.emplace(MakeNotNull<const ufbx_node*>(joint), jointIndex);
        if(!inserted.second){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB skeleton: skeleton contains duplicate joint node at index {}"), jointIndex);
            return MakeUnexpected(Failure{});
        }
        sortNames.push_back(NodeName(joint, jointIndex, scratchArena));
    }

    UtilityScratchVector<u32> parentIndices(scratchArena);
    parentIndices.resize(joints.size(), Impl::s_MeshMissingStreamIndex);
    for(usize jointIndex = 0u; jointIndex < joints.size(); ++jointIndex){
        const ufbx_node* parent = joints[jointIndex]->parent;
        if(!parent)
            continue;

        const auto foundParent = sourceJointLookup.find(MakeNotNull(parent));
        if(foundParent != sourceJointLookup.end())
            parentIndices[jointIndex] = static_cast<u32>(foundParent.value());
    }

    UtilityScratchVector<u32> parentDepths(scratchArena);
    parentDepths.resize(joints.size(), 0u);
    for(usize jointIndex = 0u; jointIndex < joints.size(); ++jointIndex){
        const auto depth = TryComputeSkeletonJointHierarchyDepth(parentIndices, jointIndex);
        if(!depth)
            return MakeUnexpected(Failure{});
        parentDepths[jointIndex] = *depth;
    }

    UtilityScratchVector<usize> sortedJointIndices(scratchArena);
    sortedJointIndices.reserve(joints.size());
    for(usize jointIndex = 0u; jointIndex < joints.size(); ++jointIndex)
        sortedJointIndices.push_back(jointIndex);
    Sort(sortedJointIndices.begin(), sortedJointIndices.end(), [&parentDepths, &sortNames](const usize lhs, const usize rhs)noexcept{
        return IsBeforeFbxSkeletonJointSortKey(parentDepths[lhs], parentDepths[rhs], AStringView(sortNames[lhs]), AStringView(sortNames[rhs]), lhs, rhs);
    });

    outData.oldToNewJointIndices.resize(joints.size(), 0u);
    outData.joints.reserve(joints.size());
    outData.bindPoseMatrices.reserve(bindPoseMatrices.size());
    outData.inverseBindMatrices.reserve(inverseBindMatrices.size());

    for(const usize oldJointIndex : sortedJointIndices){
        const u32 parentIndex = parentIndices[oldJointIndex];
        const JointMatrix* parentGlobalBindPose = parentIndex != Impl::s_MeshMissingStreamIndex ? &bindPoseMatrices[parentIndex] : nullptr;
        const SIMDMatrix globalBindPoseMatrix = LoadFloat(bindPoseMatrices[oldJointIndex]);
        const SIMDMatrix inverseBindMatrix = LoadFloat(inverseBindMatrices[oldJointIndex]);
        if(MatrixIsNaN(inverseBindMatrix) || MatrixIsInfinite(inverseBindMatrix)){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB skeleton: inverse bind matrix for joint '{}' is not finite"), ConvertString(sortNames[oldJointIndex]));
            return MakeUnexpected(Failure{});
        }

        SIMDMatrix parentGlobalBindPoseMatrix{};
        const SIMDMatrix* parentGlobalBindPoseMatrixPtr = nullptr;
        if(parentGlobalBindPose){
            parentGlobalBindPoseMatrix = LoadFloat(*parentGlobalBindPose);
            parentGlobalBindPoseMatrixPtr = &parentGlobalBindPoseMatrix;
        }

        const auto localBindPoseMatrix = BuildLocalBindPoseMatrix(globalBindPoseMatrix, parentGlobalBindPoseMatrixPtr);
        if(!localBindPoseMatrix){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB skeleton: failed to build local bind pose for joint '{}'"), ConvertString(sortNames[oldJointIndex]));
            return MakeUnexpected(Failure{});
        }
        JointMatrix localBindPose{};
        StoreFloat(*localBindPoseMatrix, localBindPose);

        outData.oldToNewJointIndices[oldJointIndex] = static_cast<u16>(outData.joints.size());
        outData.joints.push_back(joints[oldJointIndex]);
        outData.bindPoseMatrices.push_back(localBindPose);
        outData.inverseBindMatrices.push_back(inverseBindMatrices[oldJointIndex]);
    }

    return outData;
}

b8 RemapSkinInfluences(const Span<const u16> oldToNewJointIndices, const Span<MeshSkinInfluence> inOutInfluences){
    for(MeshSkinInfluence& influence : inOutInfluences){
        if(!RemapSkinInfluence(oldToNewJointIndices, influence))
            return false;
    }
    return true;
}

Expected<PositionAlignedSkinnedMesh> BuildPositionAlignedSkinnedMesh(const SourceMeshStreams& sourceMesh, Core::Alloc::ScratchArena& scratchArena){
    PositionAlignedSkinnedMesh result;
    SourceMeshStreams& outMesh = result.mesh;
    UtilityVector<MeshSkinInfluence>& outPositionSkin = result.skin;

    if(!ValidateSkinnedModelSourceMesh(sourceMesh))
        return MakeUnexpected(Failure{});

    outMesh.normals = sourceMesh.normals;
    outMesh.tangents = sourceMesh.tangents;
    outMesh.uv0 = sourceMesh.uv0;
    outMesh.colors = sourceMesh.colors;
    outMesh.indices = sourceMesh.indices;
    outMesh.vertexRefs.reserve(sourceMesh.vertexRefs.size());
    outMesh.positions.reserve(sourceMesh.vertexRefs.size());
    outPositionSkin.reserve(sourceMesh.vertexRefs.size());

    UtilityScratchMap<u64, u32> positionSkinLookup(scratchArena);
    positionSkinLookup.reserve(sourceMesh.vertexRefs.size());
    for(const SourceVertexRef& sourceRef : sourceMesh.vertexRefs){
        const u64 key = PositionSkinKey(sourceRef.position, sourceRef.skin);
        u32 positionIndex = 0u;
        const auto foundPosition = positionSkinLookup.find(key);
        if(foundPosition != positionSkinLookup.end())
            positionIndex = foundPosition.value();
        else{
            if(outMesh.positions.size() > static_cast<usize>(Limit<u32>::s_Max)){
                NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB model: split skinned mesh has too many positions"));
                return MakeUnexpected(Failure{});
            }

            positionIndex = static_cast<u32>(outMesh.positions.size());
            outMesh.positions.push_back(sourceMesh.positions[sourceRef.position]);
            outPositionSkin.push_back(sourceMesh.skin[sourceRef.skin]);
            const auto inserted = positionSkinLookup.emplace(key, positionIndex);
            if(!inserted.second){
                NWB_ASSERT(false);
                return MakeUnexpected(Failure{});
            }
        }

        SourceVertexRef ref = sourceRef;
        ref.position = positionIndex;
        ref.skin = Impl::s_MeshMissingStreamIndex;
        outMesh.vertexRefs.push_back(ref);
    }

    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

