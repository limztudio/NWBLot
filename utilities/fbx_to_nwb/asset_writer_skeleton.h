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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool InvertJointMatrix(const SIMDMatrix& matrix, SIMDMatrix& outInverse);
[[nodiscard]] bool BuildLocalBindPoseMatrix(const SIMDMatrix& globalBindPose, const SIMDMatrix* parentGlobalBindPose, SIMDMatrix& outLocalBindPose);
[[nodiscard]] bool ValidateSplitSkinSource(const SourceMeshStreams& mesh, const UtilityVector<ufbx_node*>& skeletonJoints, const UtilityVector<JointMatrix>& skeletonBindPoseMatrices, const UtilityVector<JointMatrix>& inverseBindMatrices);
[[nodiscard]] bool BuildSkeletonOutputData(const UtilityVector<ufbx_node*>& joints, const UtilityVector<JointMatrix>& bindPoseMatrices, const UtilityVector<JointMatrix>& inverseBindMatrices, SkeletonOutputData& outData);
[[nodiscard]] bool RemapSkinInfluences(UtilityVector<MeshSkinInfluence>& inOutInfluences, const UtilityVector<u16>& oldToNewJointIndices);
[[nodiscard]] bool BuildPositionAlignedSkinnedMesh(const SourceMeshStreams& sourceMesh, SourceMeshStreams& outMesh, UtilityVector<MeshSkinInfluence>& outPositionSkin);
[[nodiscard]] bool ValidateSkinnedModelSourceMesh(const SourceMeshStreams& mesh);
[[nodiscard]] bool ValidateStreamIndex(const u32 index, const usize count, const char* fieldName, const AStringView context);
[[nodiscard]] bool ValidateMeshGeometry(const SourceMeshStreams& mesh, const AStringView context);
[[nodiscard]] AString NodeName(const ufbx_node* node, const usize fallbackIndex);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

