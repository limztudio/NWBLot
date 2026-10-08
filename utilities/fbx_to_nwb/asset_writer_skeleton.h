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
[[nodiscard]] bool ValidateSplitSkinSource(const SourceMeshStreams& mesh, const UtilityVector<ufbx_node*>& skeletonJoints, const UtilityVector<JointMatrix>& skeletonBindPoseMatrices, const UtilityVector<JointMatrix>& inverseBindMatrices);
[[nodiscard]] Expected<SkeletonOutputData> BuildSkeletonOutputData(const UtilityVector<ufbx_node*>& joints, const UtilityVector<JointMatrix>& bindPoseMatrices, const UtilityVector<JointMatrix>& inverseBindMatrices);
[[nodiscard]] bool RemapSkinInfluences(UtilityVector<MeshSkinInfluence>& inOutInfluences, const UtilityVector<u16>& oldToNewJointIndices);
[[nodiscard]] Expected<PositionAlignedSkinnedMesh> BuildPositionAlignedSkinnedMesh(const SourceMeshStreams& sourceMesh);
[[nodiscard]] bool ValidateSkinnedModelSourceMesh(const SourceMeshStreams& mesh);
[[nodiscard]] bool ValidateStreamIndex(const u32 index, const usize count, const AStringView fieldName, const AStringView context);
[[nodiscard]] bool ValidateMeshGeometry(const SourceMeshStreams& mesh, const AStringView context);
[[nodiscard]] AString NodeName(const ufbx_node* node, const usize fallbackIndex);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

