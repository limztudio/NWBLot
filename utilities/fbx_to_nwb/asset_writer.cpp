// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "module.h"
#include "asset_writer_format.h"
#include "asset_writer_skeleton.h"

#include <core/common/log.h>

#include <global/allocation_size.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_asset_writer{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr AStringView s_MeshSplitFileName = "mesh.nwb";
inline constexpr AStringView s_SkeletonSplitFileName = "skeleton.nwb";
inline constexpr AStringView s_SkinSplitFileName = "skin.nwb";
inline constexpr AStringView s_AssetsSourceDirectoryText = "assets";
inline constexpr AStringView s_MeshNameSuffix = "/mesh";
inline constexpr AStringView s_SkeletonNameSuffix = "/skeleton";
inline constexpr AStringView s_SkinNameSuffix = "/skin";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ScratchString BuildVirtualBasePath(const ScratchPath& outputPath, const AStringView virtualRoot){
    Core::Alloc::ScratchArena& scratchArena = outputPath.arena();
    const AStringView trimmedVirtualRoot = TrimView(virtualRoot);

    ScratchPath noExtension = outputPath;
    noExtension.replaceExtension();

    BasicString<ScratchPath::value_type, Core::Alloc::ScratchArena> relativeNative(scratchArena);
    relativeNative.reserve(noExtension.native().size());
    bool foundAssets = false;
    for(const ScratchPath& part : noExtension){
        const auto partText = part.native();
        bool assetsDirectory = partText.size() == s_AssetsSourceDirectoryText.size();
        for(usize i = 0u; assetsDirectory && i < partText.size(); ++i)
            assetsDirectory = ToAsciiLower(partText[i]) == static_cast<ScratchPath::value_type>(s_AssetsSourceDirectoryText[i]);
        if(assetsDirectory){
            foundAssets = true;
            relativeNative.clear();
            continue;
        }
        if(foundAssets){
            if(!relativeNative.empty())
                relativeNative.push_back(static_cast<ScratchPath::value_type>('/'));
            relativeNative.append(partText.data(), partText.size());
        }
    }
    ScratchPath relativePath(scratchArena);
    if(relativeNative.empty())
        relativePath = noExtension.filename();
    else
        relativePath = BasicStringView<ScratchPath::value_type>(relativeNative);

    auto relativeText = PathToGenericString(scratchArena, relativePath);
    while(!relativeText.empty() && (relativeText.front() == '/' || relativeText.front() == '\\'))
        relativeText.erase(relativeText.begin());
    ScratchString virtualPath(trimmedVirtualRoot.empty() ? s_DefaultVirtualRootText : trimmedVirtualRoot, scratchArena);
    virtualPath.reserve(AddSize(AddSize(virtualPath.size(), relativeText.size()), 1u));
    if(!virtualPath.empty() && virtualPath.back() != '/')
        virtualPath.push_back('/');
    virtualPath += relativeText;
    return virtualPath;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool WriteNwbAsset(
    const ScratchPath& outputPath,
    const SourceMeshStreams& mesh,
    const AStringView assetTypeText,
    const AStringView virtualRoot,
    const bool separateAssets,
    const UtilityVector<ufbx_node*>& skeletonJoints,
    const UtilityVector<JointMatrix>& skeletonBindPoseMatrices,
    const UtilityVector<JointMatrix>& inverseBindMatrices,
    Core::Alloc::ScratchArena& scratchArena
){
    const auto assetTypeResult = ParseAssetTypeText(assetTypeText, scratchArena);
    if(!assetTypeResult){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB asset: {}"), StringConvert(OutputAssetTypeErrorText(scratchArena)));
        return false;
    }

    const OutputAssetType::Enum assetType = *assetTypeResult;
    if(separateAssets && assetType != OutputAssetType::Bunch){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB asset: --separate-assets is only valid with asset type 'bunch'"));
        return false;
    }

    if(assetType == OutputAssetType::Mesh){
        if(!skeletonJoints.empty() || !skeletonBindPoseMatrices.empty() || !inverseBindMatrices.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB mesh: mesh output cannot write split skeleton/skin payload"));
            return false;
        }
        return AssetWriterFormatDetail::WriteMeshAsset(outputPath, mesh);
    }

    const bool skinnedModel = !mesh.skin.empty() || !skeletonJoints.empty() || !inverseBindMatrices.empty();
    const ScratchPath packageDirectory = outputPath.parentPath() / outputPath.stem();
    const ScratchPath meshPath = packageDirectory / __hidden_asset_writer::s_MeshSplitFileName;
    const ScratchString virtualBase = __hidden_asset_writer::BuildVirtualBasePath(outputPath, virtualRoot);
    const ScratchString meshName = virtualBase + __hidden_asset_writer::s_MeshNameSuffix.data();

    if(assetType == OutputAssetType::Model){
        if(skinnedModel){
            const ScratchString skeletonName = virtualBase + __hidden_asset_writer::s_SkeletonNameSuffix.data();
            const ScratchString skinName = virtualBase + __hidden_asset_writer::s_SkinNameSuffix.data();
            return AssetWriterFormatDetail::WriteModelAsset(outputPath, meshName, AStringView(skinName), AStringView(skeletonName), scratchArena);
        }
        return AssetWriterFormatDetail::WriteModelAsset(outputPath, meshName, {}, {}, scratchArena);
    }

    if(!skinnedModel){
        if(assetType == OutputAssetType::Skeleton || assetType == OutputAssetType::Skin){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB asset: requested asset type requires a skinned source mesh"));
            return false;
        }
        if(assetType == OutputAssetType::Bunch && !separateAssets)
            return AssetWriterFormatDetail::WriteAssetBunch(outputPath, mesh, {}, {}, skeletonJoints, skeletonBindPoseMatrices, nullptr, inverseBindMatrices, scratchArena);

        if(!AssetWriterFormatDetail::WriteMeshAsset(meshPath, mesh))
            return false;
        return AssetWriterFormatDetail::WriteModelAsset(outputPath, meshName, {}, {}, scratchArena);
    }

    if(!AssetWriterSkeletonDetail::ValidateSplitSkinSource(mesh, skeletonJoints, skeletonBindPoseMatrices, inverseBindMatrices))
        return false;

    auto skeletonResult = AssetWriterSkeletonDetail::BuildSkeletonOutputData(skeletonJoints, skeletonBindPoseMatrices, inverseBindMatrices, scratchArena);
    if(!skeletonResult)
        return false;
    AssetWriterSkeletonDetail::SkeletonOutputData& skeletonOutput = *skeletonResult;

    auto alignedMesh = AssetWriterSkeletonDetail::BuildPositionAlignedSkinnedMesh(mesh, scratchArena);
    if(!alignedMesh)
        return false;
    SourceMeshStreams& splitMesh = alignedMesh->mesh;
    UtilityVector<MeshSkinInfluence>& positionSkin = alignedMesh->skin;
    if(!AssetWriterSkeletonDetail::RemapSkinInfluences(positionSkin, skeletonOutput.oldToNewJointIndices))
        return false;

    const ScratchPath skeletonPath = packageDirectory / __hidden_asset_writer::s_SkeletonSplitFileName;
    const ScratchPath skinPath = packageDirectory / __hidden_asset_writer::s_SkinSplitFileName;
    const ScratchString skeletonName = virtualBase + __hidden_asset_writer::s_SkeletonNameSuffix.data();
    const ScratchString skinName = virtualBase + __hidden_asset_writer::s_SkinNameSuffix.data();

    if(assetType == OutputAssetType::Skeleton)
        return AssetWriterFormatDetail::WriteSkeletonAsset(outputPath, skeletonOutput.joints, skeletonOutput.bindPoseMatrices, scratchArena);
    if(assetType == OutputAssetType::Skin)
        return AssetWriterFormatDetail::WriteSkinAsset(outputPath, meshName, skeletonName, positionSkin, skeletonOutput.inverseBindMatrices, scratchArena);
    if(assetType == OutputAssetType::Bunch && !separateAssets)
        return AssetWriterFormatDetail::WriteAssetBunch(outputPath, splitMesh, AStringView(skinName), AStringView(skeletonName), skeletonOutput.joints, skeletonOutput.bindPoseMatrices, &positionSkin, skeletonOutput.inverseBindMatrices, scratchArena);

    return
        AssetWriterFormatDetail::WriteMeshAsset(meshPath, splitMesh)
        && AssetWriterFormatDetail::WriteSkeletonAsset(skeletonPath, skeletonOutput.joints, skeletonOutput.bindPoseMatrices, scratchArena)
        && AssetWriterFormatDetail::WriteSkinAsset(skinPath, meshName, skeletonName, positionSkin, skeletonOutput.inverseBindMatrices, scratchArena)
        && AssetWriterFormatDetail::WriteModelAsset(outputPath, meshName, AStringView(skinName), AStringView(skeletonName), scratchArena)
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

