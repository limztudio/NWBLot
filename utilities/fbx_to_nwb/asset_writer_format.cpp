// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset_writer_format.h"
#include "skinned_asset_text_writer.h"
#include "mesh_asset_text_writer.h"
#include "model_asset_text_writer.h"
#include "source_mesh_streams.h"

#include <core/common/log.h>

#include <global/filesystem/crlf_output_stream.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_asset_writer_format{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 EnsureOutputDirectory(const ScratchPath& outputPath, const AStringView assetKind){
    const ScratchPath parentPath = outputPath.parentPath();
    if(parentPath.empty())
        return true;

    const auto created = EnsureDirectories(parentPath);
    if(created)
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB {}: failed to create output directory '{}': {}"), ConvertString(assetKind), PathToString<tchar>(parentPath), ConvertString(created.error().message()));
    return false;
}

template<typename WriteBodyFn>
[[nodiscard]] b8 WriteNwbTextFile(const ScratchPath& outputPath, const AStringView assetKind, WriteBodyFn&& writeBody){
    if(!EnsureOutputDirectory(outputPath, assetKind))
        return false;

    BasicOutputFileStream<char> rawFile(outputPath, s_FileOpenBinary | s_FileOpenTruncate);
    if(!rawFile){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB {}: failed to open output file '{}'"), ConvertString(assetKind), PathToString<tchar>(outputPath));
        return false;
    }
    CrlfOutputFileStream file(rawFile);
    file.precision(s_OutputFloatPrecision);
    if(!writeBody(file))
        return false;

    rawFile.close();
    if(!file){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB {}: failed while writing output file '{}'"), ConvertString(assetKind), PathToString<tchar>(outputPath));
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AssetWriterFormatDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 WriteMeshAsset(const ScratchPath& outputPath, const SourceMeshStreams& mesh){
    using namespace __hidden_asset_writer_format;

    if(!ValidatePlainMeshSourceSkin(CountSourceMeshStreams(mesh), mesh.vertexRefs, mesh.indices))
        return false;

    return WriteNwbTextFile(outputPath, s_MeshAssetTypeText, [&](CrlfOutputFileStream& file) -> b8{
        file << "mesh asset;\n\n";
        MeshAssetTextWriter::writeBody(mesh, s_AssetVariableName, "", file);
        return true;
    });
}

b8 WriteSkeletonAsset(const ScratchPath& outputPath, const Span<ufbx_node* const> joints, const Span<const JointMatrix> bindPoseMatrices, Core::Alloc::ScratchArena& scratchArena){
    using namespace __hidden_asset_writer_format;

    return WriteNwbTextFile(outputPath, s_SkeletonAssetTypeText, [&](CrlfOutputFileStream& file) -> b8{
        file << "skeleton asset;\n\n";
        return SkinnedAssetTextWriter::writeSkeletonBody(s_AssetVariableName, joints, bindPoseMatrices, scratchArena, file);
    });
}

b8 WriteSkinAsset(const ScratchPath& outputPath, const AStringView meshName, const AStringView skeletonName, const Span<const MeshSkinInfluence> influences, const Span<const JointMatrix> inverseBindMatrices, Core::Alloc::ScratchArena& scratchArena){
    using namespace __hidden_asset_writer_format;

    if(influences.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB skin: no skin influences were produced"));
        return false;
    }

    return WriteNwbTextFile(outputPath, s_SkinAssetTypeText, [&](CrlfOutputFileStream& file) -> b8{
        file << "skin asset;\n\n";
        SkinnedAssetTextWriter::writeSkinBody(s_AssetVariableName, meshName, skeletonName, influences, inverseBindMatrices, true, scratchArena, file);
        return true;
    });
}

b8 WriteModelAsset(const ScratchPath& outputPath, const AStringView meshName, const Optional<AStringView> skinName, const Optional<AStringView> skeletonName, Core::Alloc::ScratchArena& scratchArena){
    using namespace __hidden_asset_writer_format;

    return WriteNwbTextFile(outputPath, s_ModelAssetTypeText, [&](CrlfOutputFileStream& file) -> b8{
        file << "model asset;\n\n";
        ModelAssetTextWriter::writeBody(s_AssetVariableName, meshName, skinName, skeletonName, s_SkeletonAssetTypeText, true, true, scratchArena, file);
        return true;
    });
}

b8 WriteAssetBunch(
    const ScratchPath& outputPath,
    const SourceMeshStreams& mesh,
    const Optional<AStringView> skinName,
    const Optional<AStringView> skeletonName,
    const Span<ufbx_node* const> skeletonJoints,
    const Span<const JointMatrix> skeletonBindPoseMatrices,
    const Span<const MeshSkinInfluence> skinInfluences,
    const Span<const JointMatrix> inverseBindMatrices,
    Core::Alloc::ScratchArena& scratchArena
){
    using namespace __hidden_asset_writer_format;

    return WriteNwbTextFile(outputPath, s_AssetBunchDisplayName, [&](CrlfOutputFileStream& file) -> b8{
        file << "mesh mesh;\n\n";
        MeshAssetTextWriter::writeBody(mesh, s_MeshAssetTypeText, "", file);

        const b8 skinnedBunch = skinName && skeletonName && !skinInfluences.empty();
        if(skinnedBunch){
            file << s_AssetSectionSeparatorText;
            file << "skeleton skeleton;\n\n";
            if(!SkinnedAssetTextWriter::writeSkeletonBody(s_SkeletonAssetTypeText, skeletonJoints, skeletonBindPoseMatrices, scratchArena, file))
                return false;

            file << s_AssetSectionSeparatorText;
            file << "skin skin;\n\n";
            SkinnedAssetTextWriter::writeSkinBody(s_SkinAssetTypeText, s_MeshAssetTypeText, s_SkeletonAssetTypeText, skinInfluences, inverseBindMatrices, false, scratchArena, file);
        }

        file << s_AssetSectionSeparatorText;
        file << "model model;\n\n";
        if(skinnedBunch)
            ModelAssetTextWriter::writeBody(s_ModelAssetTypeText, s_MeshAssetTypeText, s_SkinAssetTypeText, s_SkeletonAssetTypeText, s_SkeletonAssetTypeText, false, true, scratchArena, file);
        else
            ModelAssetTextWriter::writeBody(s_ModelAssetTypeText, s_MeshAssetTypeText, {}, {}, s_SkeletonAssetTypeText, false, false, scratchArena, file);

        file << s_AssetSectionSeparatorText;
        file << "asset_bunch bunch = [\n";
        file << "    mesh,\n";
        if(skinnedBunch){
            file << "    skeleton,\n";
            file << "    skin,\n";
        }
        file << "    model,\n";
        file << s_FormatListCloseText;
        return true;
    });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

