// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset_writer_format.h"
#include "asset_writer_skeleton.h"
#include "source_mesh_streams.h"
#include "text_write.h"

#include <core/common/log.h>

#include <global/algorithm.h>
#include <global/allocation_size.h>
#include <global/json_text.h>
#include <global/filesystem/crlf_output_stream.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_asset_writer_format{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TextWrite::WriteFloat;
using TextWrite::WriteVec2;
using TextWrite::WriteVec3;
using TextWrite::WriteVec4;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename Stream>
void WriteSkinJoints(const MeshSkinInfluence& skin, Stream& out)noexcept(noexcept(out << "[") && noexcept(out << ", ") && noexcept(out << skin.joint[DeclVal<usize>()]) && noexcept(out << "]")){
    out << "[";
    for(usize i = 0u; i < Impl::s_SkinInfluenceJointCount; ++i){
        if(i != 0u)
            out << ", ";
        out << skin.joint[i];
    }
    out << "]";
}

template<typename Stream>
void WriteSkinWeights(const MeshSkinInfluence& skin, Stream& out)noexcept(noexcept(out << "[") && noexcept(out << ", ") && noexcept(WriteFloat(skin.weight.raw[DeclVal<usize>()], out)) && noexcept(out << "]")){
    out << "[";
    for(usize i = 0u; i < Impl::s_SkinInfluenceJointCount; ++i){
        if(i != 0u)
            out << ", ";
        WriteFloat(skin.weight.raw[i], out);
    }
    out << "]";
}

template<typename Stream>
void WriteJointMatrix(const JointMatrix& matrix, const AStringView indent, Stream& out)noexcept(
    noexcept(out << s_ListOpenText)
    && noexcept(out << indent << "    ")
    && noexcept(WriteVec4(DeclVal<const Vec4&>(), out))
    && noexcept(out << s_EntrySuffixText)
    && noexcept(out << indent << "]")
){
    out << s_ListOpenText;
    for(const Vec4& row : matrix.rows){
        out << indent << "    ";
        WriteVec4(row, out);
        out << s_EntrySuffixText;
    }
    out << indent << "]";
}

b8 ValidatePlainMeshAsset(const SourceMeshStreams& mesh){
    if(!FbxSourceMeshStreams::ValidateMeshGeometry(mesh, s_WriteNwbMeshContext))
        return false;
    return ValidatePlainMeshSourceSkin(mesh.skin.size(), Span<const SourceVertexRef>(mesh.vertexRefs.data(), mesh.vertexRefs.size()));
}

template<typename Stream>
void WriteVertexRef(const SourceVertexRef& ref, Stream& out)noexcept(noexcept(out << "[" << ref.position << ", " << ref.normal << ", " << ref.tangent << ", " << ref.uv0 << ", " << ref.color << "]")){
    out << "[" << ref.position << ", " << ref.normal << ", " << ref.tangent << ", " << ref.uv0 << ", " << ref.color << "]";
}

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

template<typename Stream>
void WriteMeshAssetBody(const SourceMeshStreams& mesh, const AStringView variableName, const AStringView indent, Stream& file)noexcept(
    noexcept(file << variableName << s_FieldAccessText << Impl::s_MeshPositionsFieldNameView << s_ListAssignOpenText)
    && noexcept(file << indent << "    ")
    && noexcept(WriteVec3(DeclVal<const Vec3&>(), file))
    && noexcept(file << s_EntrySuffixText)
    && noexcept(file << indent << s_ListCloseSectionText)
    && noexcept(file << variableName << s_FieldAccessText << Impl::s_MeshNormalsFieldNameView << s_ListAssignOpenText)
    && noexcept(file << variableName << s_FieldAccessText << Impl::s_MeshTangentsFieldNameView << s_ListAssignOpenText)
    && noexcept(WriteVec4(DeclVal<const Vec4&>(), file))
    && noexcept(file << variableName << s_FieldAccessText << Impl::s_MeshUv0FieldNameView << s_ListAssignOpenText)
    && noexcept(WriteVec2(DeclVal<const Vec2&>(), file))
    && noexcept(file << variableName << s_FieldAccessText << Impl::s_MeshColorsFieldNameView << s_ListAssignOpenText)
    && noexcept(file << variableName << s_FieldAccessText << Impl::s_MeshVertexRefsFieldNameView << s_ListAssignOpenText)
    && noexcept(WriteVertexRef(DeclVal<const SourceVertexRef&>(), file))
    && noexcept(file << variableName << s_FieldAccessText << Impl::s_MeshIndicesFieldNameView << s_ListAssignOpenText)
    && noexcept(file << indent << "    [" << mesh.indices[DeclVal<usize>() + 0u] << ", " << mesh.indices[DeclVal<usize>() + 1u] << ", " << mesh.indices[DeclVal<usize>() + 2u] << s_RowSuffixText)
    && noexcept(file << indent << s_FormatListCloseText)
){
    file << variableName << s_FieldAccessText << Impl::s_MeshPositionsFieldNameView << s_ListAssignOpenText;
    for(const Vec3& position : mesh.positions){
        file << indent << "    ";
        WriteVec3(position, file);
        file << s_EntrySuffixText;
    }
    file << indent << s_ListCloseSectionText;

    file << variableName << s_FieldAccessText << Impl::s_MeshNormalsFieldNameView << s_ListAssignOpenText;
    for(const Vec3& normal : mesh.normals){
        file << indent << "    ";
        WriteVec3(normal, file);
        file << s_EntrySuffixText;
    }
    file << indent << s_ListCloseSectionText;

    file << variableName << s_FieldAccessText << Impl::s_MeshTangentsFieldNameView << s_ListAssignOpenText;
    for(const Vec4& tangent : mesh.tangents){
        file << indent << "    ";
        WriteVec4(tangent, file);
        file << s_EntrySuffixText;
    }
    file << indent << s_ListCloseSectionText;

    file << variableName << s_FieldAccessText << Impl::s_MeshUv0FieldNameView << s_ListAssignOpenText;
    for(const Vec2& uv0 : mesh.uv0){
        file << indent << "    ";
        WriteVec2(uv0, file);
        file << s_EntrySuffixText;
    }
    file << indent << s_ListCloseSectionText;

    file << variableName << s_FieldAccessText << Impl::s_MeshColorsFieldNameView << s_ListAssignOpenText;
    for(const Vec4& color : mesh.colors){
        file << indent << "    ";
        WriteVec4(color, file);
        file << s_EntrySuffixText;
    }
    file << indent << s_ListCloseSectionText;

    file << variableName << s_FieldAccessText << Impl::s_MeshVertexRefsFieldNameView << s_ListAssignOpenText;
    for(const SourceVertexRef& ref : mesh.vertexRefs){
        file << indent << "    ";
        WriteVertexRef(ref, file);
        file << s_EntrySuffixText;
    }
    file << indent << s_ListCloseSectionText;

    file << variableName << s_FieldAccessText << Impl::s_MeshIndicesFieldNameView << s_ListAssignOpenText;
    for(usize i = 0u; i < mesh.indices.size(); i += Impl::s_MeshletTriangleIndexCount)
        file << indent << "    [" << mesh.indices[i + 0u] << ", " << mesh.indices[i + 1u] << ", " << mesh.indices[i + 2u] << s_RowSuffixText;
    file << indent << s_FormatListCloseText;
}

ScratchString UniqueNodeName(const ufbx_node* node, const usize fallbackIndex, const Span<const ScratchString> usedNames, Core::Alloc::ScratchArena& scratchArena){
    const ScratchString baseName = AssetWriterSkeletonDetail::NodeName(node, fallbackIndex, scratchArena);
    ScratchString name(baseName, scratchArena);
    name.reserve(AddSize(baseName.size(), s_UniqueNameSuffixReserve));

    u32 suffix = 1u;
    while(FindIf(usedNames.begin(), usedNames.end(), [&name](const ScratchString& usedName)noexcept{ return usedName == AStringView(name); }) != usedNames.end()){
        name.clear();
        StringAppendFormat(name, "{}_{}", baseName, suffix);
        ++suffix;
    }

    return name;
}

UtilityScratchVector<ScratchString> BuildUniqueJointNames(const Span<ufbx_node* const> joints, Core::Alloc::ScratchArena& scratchArena){
    UtilityScratchVector<ScratchString> names(scratchArena);
    names.reserve(joints.size());
    for(usize jointIndex = 0u; jointIndex < joints.size(); ++jointIndex)
        names.push_back(UniqueNodeName(joints[jointIndex], jointIndex, names, scratchArena));

    return names;
}

template<typename Stream>
void WriteReferenceValue(const AStringView value, const b8 quote, Core::Alloc::ScratchArena& scratchArena, Stream& file){
    if(quote){
        ScratchString escaped(scratchArena);
        escaped.reserve(SizeOf<s_EscapedTextReserveFactor>(value.size()));
        AppendJsonEscapedText(escaped, value);
        file << "\"" << escaped << "\"";
    }
    else
        file << value;
}

template<typename Stream>
b8 WriteSkeletonAssetBody(const AStringView variableName, const Span<ufbx_node* const> joints, const Span<const JointMatrix> bindPoseMatrices, Core::Alloc::ScratchArena& scratchArena, Stream& file){
    file << variableName << ".joints = [\n";
    const auto jointNames = BuildUniqueJointNames(joints, scratchArena);

    UtilityScratchMap<const ufbx_node*, usize> jointLookup(scratchArena);
    jointLookup.reserve(joints.size());
    for(usize jointIndex = 0u; jointIndex < joints.size(); ++jointIndex){
        const auto inserted = jointLookup.emplace(joints[jointIndex], jointIndex);
        if(!inserted.second){
            NWB_ASSERT(false);
            return false;
        }
    }

    for(usize jointIndex = 0u; jointIndex < joints.size(); ++jointIndex){
        file << "    {\n";
        file << "        \"name\": ";
        WriteReferenceValue(jointNames[jointIndex], true, scratchArena, file);
        file << s_EntrySuffixText;
        if(joints[jointIndex] && joints[jointIndex]->parent){
            const auto foundParent = jointLookup.find(joints[jointIndex]->parent);
            if(foundParent != jointLookup.end()){
                const ScratchString& parentName = jointNames[foundParent.value()];
                file << "        \"parent\": ";
                WriteReferenceValue(parentName, true, scratchArena, file);
                file << s_EntrySuffixText;
            }
        }
        file << "        \"local_bind_pose\": ";
        WriteJointMatrix(bindPoseMatrices[jointIndex], "        ", file);
        file << s_EntrySuffixText;
        file << s_TableEntryCloseText;
    }
    file << s_FormatListCloseText;
    return true;
}

template<typename Stream>
void WriteSkinAssetBody(
    const AStringView variableName,
    const AStringView meshName,
    const AStringView skeletonName,
    const Span<const MeshSkinInfluence> influences,
    const Span<const JointMatrix> inverseBindMatrices,
    const b8 quoteReferences,
    Core::Alloc::ScratchArena& scratchArena,
    Stream& file
){
    file << variableName << ".mesh = ";
    WriteReferenceValue(meshName, quoteReferences, scratchArena, file);
    file << s_StatementEndText;
    file << variableName << ".skeleton = ";
    WriteReferenceValue(skeletonName, quoteReferences, scratchArena, file);
    file << s_StatementEndSectionText;

    file << variableName << ".influences = [\n";
    for(const MeshSkinInfluence& influence : influences){
        file << "    { \"joints\": ";
        WriteSkinJoints(influence, file);
        file << ", \"weights\": ";
        WriteSkinWeights(influence, file);
        file << " },\n";
    }
    file << s_ListCloseSectionText;

    file << variableName << ".inverse_bind_matrices = [\n";
    for(const JointMatrix& matrix : inverseBindMatrices){
        file << "    ";
        WriteJointMatrix(matrix, "    ", file);
        file << s_EntrySuffixText;
    }
    file << s_FormatListCloseText;
}

template<typename Stream>
void WriteModelAssetBody(
    const AStringView variableName,
    const AStringView meshName,
    const Optional<AStringView> skinName,
    const Optional<AStringView> skeletonName,
    const AStringView skinnedMeshSkeletonName,
    const b8 quoteAssetReferences,
    const b8 quoteSkinnedMeshSkeletonReference,
    Core::Alloc::ScratchArena& scratchArena,
    Stream& file
){
    if(skeletonName){
        file << variableName << ".skeletons = {\n";
        file << "    \"skeleton\": {\n";
        file << s_SkeletonPropertyText;
        WriteReferenceValue(*skeletonName, quoteAssetReferences, scratchArena, file);
        file << s_EntrySuffixText;
        file << s_TableEntryCloseText;
        file << s_TableCloseSectionText;

        file << variableName << ".skinned_meshes = {\n";
        file << "    \"mesh\": {\n";
        file << "        \"skin\": ";
        WriteReferenceValue(*skinName, quoteAssetReferences, scratchArena, file);
        file << s_EntrySuffixText;
        file << s_SkeletonPropertyText;
        WriteReferenceValue(skinnedMeshSkeletonName, quoteSkinnedMeshSkeletonReference, scratchArena, file);
        file << s_EntrySuffixText;
        file << s_TableEntryCloseText;
        file << s_TableCloseText;
    }
    else{
        file << variableName << ".static_meshes = {\n";
        file << "    \"base\": {\n";
        file << "        \"mesh\": ";
        WriteReferenceValue(meshName, quoteAssetReferences, scratchArena, file);
        file << s_EntrySuffixText;
        file << s_TableEntryCloseText;
        file << s_TableCloseText;
    }
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

    if(!ValidatePlainMeshAsset(mesh))
        return false;

    return WriteNwbTextFile(outputPath, s_MeshAssetTypeText, [&](CrlfOutputFileStream& file) -> b8{
        file << "mesh asset;\n\n";
        WriteMeshAssetBody(mesh, s_AssetVariableName, "", file);
        return true;
    });
}

b8 WriteSkeletonAsset(const ScratchPath& outputPath, const Span<ufbx_node* const> joints, const Span<const JointMatrix> bindPoseMatrices, Core::Alloc::ScratchArena& scratchArena){
    using namespace __hidden_asset_writer_format;

    return WriteNwbTextFile(outputPath, s_SkeletonAssetTypeText, [&](CrlfOutputFileStream& file) -> b8{
        file << "skeleton asset;\n\n";
        return WriteSkeletonAssetBody(s_AssetVariableName, joints, bindPoseMatrices, scratchArena, file);
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
        WriteSkinAssetBody(s_AssetVariableName, meshName, skeletonName, influences, inverseBindMatrices, true, scratchArena, file);
        return true;
    });
}

b8 WriteModelAsset(const ScratchPath& outputPath, const AStringView meshName, const Optional<AStringView> skinName, const Optional<AStringView> skeletonName, Core::Alloc::ScratchArena& scratchArena){
    using namespace __hidden_asset_writer_format;

    return WriteNwbTextFile(outputPath, s_ModelAssetTypeText, [&](CrlfOutputFileStream& file) -> b8{
        file << "model asset;\n\n";
        WriteModelAssetBody(s_AssetVariableName, meshName, skinName, skeletonName, s_SkeletonAssetTypeText, true, true, scratchArena, file);
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
        WriteMeshAssetBody(mesh, s_MeshAssetTypeText, "", file);

        const b8 skinnedBunch = skinName && skeletonName && !skinInfluences.empty();
        if(skinnedBunch){
            file << s_AssetSectionSeparatorText;
            file << "skeleton skeleton;\n\n";
            if(!WriteSkeletonAssetBody(s_SkeletonAssetTypeText, skeletonJoints, skeletonBindPoseMatrices, scratchArena, file))
                return false;

            file << s_AssetSectionSeparatorText;
            file << "skin skin;\n\n";
            WriteSkinAssetBody(s_SkinAssetTypeText, s_MeshAssetTypeText, s_SkeletonAssetTypeText, skinInfluences, inverseBindMatrices, false, scratchArena, file);
        }

        file << s_AssetSectionSeparatorText;
        file << "model model;\n\n";
        if(skinnedBunch)
            WriteModelAssetBody(s_ModelAssetTypeText, s_MeshAssetTypeText, s_SkinAssetTypeText, s_SkeletonAssetTypeText, s_SkeletonAssetTypeText, false, true, scratchArena, file);
        else
            WriteModelAssetBody(s_ModelAssetTypeText, s_MeshAssetTypeText, {}, {}, s_SkeletonAssetTypeText, false, false, scratchArena, file);

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

