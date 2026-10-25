// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "module.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AssetWriterSkeletonDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SkeletonOutputData{
    UtilityVector<ufbx_node*> joints;
    UtilityVector<JointMatrix> bindPoseMatrices;
    UtilityVector<JointMatrix> inverseBindMatrices;
    UtilityVector<u16> oldToNewJointIndices;
};

struct PositionAlignedSkinnedMesh{
    SourceMeshStreams mesh;
    UtilityVector<MeshSkinInfluence> skin;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Expected<SIMDMatrix> InvertJointMatrix(const SIMDMatrix& matrix)noexcept;
[[nodiscard]] Expected<SIMDMatrix> BuildLocalBindPoseMatrix(const SIMDMatrix& globalBindPose, const SIMDMatrix* parentGlobalBindPose)noexcept;
[[nodiscard]] bool ValidateSplitSkinSource(const SourceMeshStreams& mesh, Span<ufbx_node* const> skeletonJoints, Span<const JointMatrix> skeletonBindPoseMatrices, Span<const JointMatrix> inverseBindMatrices);
[[nodiscard]] Expected<SkeletonOutputData> BuildSkeletonOutputData(Span<ufbx_node* const> joints, Span<const JointMatrix> bindPoseMatrices, Span<const JointMatrix> inverseBindMatrices, Core::Alloc::ScratchArena& scratchArena);
[[nodiscard]] bool RemapSkinInfluences(Span<MeshSkinInfluence> inOutInfluences, Span<const u16> oldToNewJointIndices);
[[nodiscard]] Expected<PositionAlignedSkinnedMesh> BuildPositionAlignedSkinnedMesh(const SourceMeshStreams& sourceMesh, Core::Alloc::ScratchArena& scratchArena);
[[nodiscard]] bool ValidateSkinnedModelSourceMesh(const SourceMeshStreams& mesh);
[[nodiscard]] bool ValidateStreamIndex(const u32 index, const usize count, const AStringView fieldName, const AStringView context);
[[nodiscard]] bool ValidateMeshGeometry(const SourceMeshStreams& mesh, const AStringView context);
[[nodiscard]] ScratchString NodeName(const ufbx_node* node, const usize fallbackIndex, Core::Alloc::ScratchArena& scratchArena);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

