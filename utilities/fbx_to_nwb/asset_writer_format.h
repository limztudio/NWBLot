// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "module.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AssetWriterFormatDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool WriteMeshAsset(const ScratchPath& outputPath, const SourceMeshStreams& mesh);
[[nodiscard]] bool WriteSkeletonAsset(const ScratchPath& outputPath, const UtilityVector<ufbx_node*>& joints, const UtilityVector<JointMatrix>& bindPoseMatrices, Core::Alloc::ScratchArena& scratchArena);
[[nodiscard]] bool WriteSkinAsset(const ScratchPath& outputPath, AStringView meshName, AStringView skeletonName, const UtilityVector<MeshSkinInfluence>& influences, const UtilityVector<JointMatrix>& inverseBindMatrices, Core::Alloc::ScratchArena& scratchArena);
[[nodiscard]] bool WriteModelAsset(const ScratchPath& outputPath, AStringView meshName, Optional<AStringView> skinName, Optional<AStringView> skeletonName, Core::Alloc::ScratchArena& scratchArena);
[[nodiscard]] bool WriteAssetBunch(
    const ScratchPath& outputPath,
    const SourceMeshStreams& mesh,
    Optional<AStringView> skinName,
    Optional<AStringView> skeletonName,
    const UtilityVector<ufbx_node*>& skeletonJoints,
    const UtilityVector<JointMatrix>& skeletonBindPoseMatrices,
    const UtilityVector<MeshSkinInfluence>* skinInfluences,
    const UtilityVector<JointMatrix>& inverseBindMatrices,
    Core::Alloc::ScratchArena& scratchArena
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

