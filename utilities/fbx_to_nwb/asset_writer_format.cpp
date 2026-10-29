// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset_writer_format.h"
#include "asset_writer_skeleton.h"
#include "text_write.h"

#include <core/common/log.h>

#include <global/allocation_size.h>
#include <global/json_text.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_asset_writer_format{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TextWrite::WriteFloat;
using TextWrite::WriteVec2;
using TextWrite::WriteVec3;
using TextWrite::WriteVec4;
using TextWrite::s_OutputFloatPrecision;

inline constexpr AStringView s_MeshAssetKindText = "mesh";
inline constexpr AStringView s_ModelAssetKindText = "model";
inline constexpr AStringView s_SkeletonAssetKindText = "skeleton";
inline constexpr AStringView s_SkinAssetKindText = "skin";
inline constexpr AStringView s_AssetVariableName = "asset";
inline constexpr AStringView s_AssetBunchDisplayName = "asset bunch";
inline constexpr AStringView s_LineBreakText = "\r\n";
inline constexpr AStringView s_ListOpenText = "[\n";
inline constexpr AStringView s_EntrySuffixText = ",\n";
inline constexpr AStringView s_RowSuffixText = "],\n";
inline constexpr AStringView s_ListCloseText = "];\n";
inline constexpr AStringView s_ListCloseSectionText = "];\n\n";
inline constexpr AStringView s_FieldAccessText = ".";
inline constexpr AStringView s_ListAssignOpenText = " = [\n";
inline constexpr AStringView s_StatementEndText = ";\n";
inline constexpr AStringView s_StatementEndSectionText = ";\n\n";
inline constexpr AStringView s_TableEntryCloseText = "    },\n";
inline constexpr AStringView s_TableCloseText = "};\n";
inline constexpr AStringView s_TableCloseSectionText = "};\n\n";
inline constexpr AStringView s_SkeletonPropertyText = "        \"skeleton\": ";
inline constexpr AStringView s_AssetSectionSeparatorText = "\n\n";
// '_' plus the decimal digits of a u32 suffix.
inline constexpr usize s_UniqueNameSuffixReserve = 11u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Normalize streamed metadata to CRLF without buffering another full copy of large mesh payloads.
class NwbTextOutputStream final : NoCopy{
public:
    explicit NwbTextOutputStream(BasicOutputFileStream<char>& stream)noexcept
        : m_stream(stream)
    {}


public:
    template<usize Length>
    NwbTextOutputStream& operator<<(const char (&text)[Length]){
        writeText(AStringView(text, Length - 1u));
        return *this;
    }

    NwbTextOutputStream& operator<<(const AStringView text){
        writeText(text);
        return *this;
    }

    template<typename T>
        requires(!IsConvertible_V<const T&, AStringView>)
    NwbTextOutputStream& operator<<(const T& value){
        m_stream << value;
        return *this;
    }


public:
    [[nodiscard]] explicit operator b8()const noexcept{ return static_cast<b8>(m_stream); }

    [[nodiscard]] b8 operator!()const noexcept{ return !m_stream; }


public:
    void precision(const StreamSize precision)noexcept{ m_stream.precision(precision); }


private:
    void writeText(const AStringView text){
        usize chunkBegin = 0u;
        for(usize i = 0u; i < text.size(); ++i){
            const char character = text[i];
            if(character != '\r' && character != '\n')
                continue;

            if(i > chunkBegin)
                m_stream.write(text.data() + chunkBegin, static_cast<StreamSize>(i - chunkBegin));

            if(character == '\r' && i + 1u < text.size() && text[i + 1u] == '\n')
                ++i;
            m_stream.write(s_LineBreakText.data(), static_cast<StreamSize>(s_LineBreakText.size()));
            chunkBegin = i + 1u;
        }

        if(chunkBegin < text.size())
            m_stream.write(text.data() + chunkBegin, static_cast<StreamSize>(text.size() - chunkBegin));
    }


private:
    BasicOutputFileStream<char>& m_stream;
};


template<typename Stream>
void WriteSkinJoints(Stream& out, const MeshSkinInfluence& skin)noexcept(noexcept(out << "[") && noexcept(out << ", ") && noexcept(out << skin.joint[DeclVal<usize>()]) && noexcept(out << "]")){
    out << "[";
    for(usize i = 0u; i < s_MeshSkinInfluenceCount; ++i){
        if(i != 0u)
            out << ", ";
        out << skin.joint[i];
    }
    out << "]";
}

template<typename Stream>
void WriteSkinWeights(Stream& out, const MeshSkinInfluence& skin)noexcept(noexcept(out << "[") && noexcept(out << ", ") && noexcept(WriteFloat(out, skin.weight.raw[DeclVal<usize>()])) && noexcept(out << "]")){
    out << "[";
    for(usize i = 0u; i < s_MeshSkinInfluenceCount; ++i){
        if(i != 0u)
            out << ", ";
        WriteFloat(out, skin.weight.raw[i]);
    }
    out << "]";
}

template<typename Stream>
void WriteJointMatrix(Stream& out, const JointMatrix& matrix, const AStringView indent = "")noexcept(
    noexcept(out << s_ListOpenText)
    && noexcept(out << indent << "    ")
    && noexcept(WriteVec4(out, DeclVal<const Vec4&>()))
    && noexcept(out << s_EntrySuffixText)
    && noexcept(out << indent << "]")
){
    out << s_ListOpenText;
    for(const Vec4& row : matrix.rows){
        out << indent << "    ";
        WriteVec4(out, row);
        out << s_EntrySuffixText;
    }
    out << indent << "]";
}


b8 ValidatePlainMeshAsset(const SourceMeshStreams& mesh){
    static constexpr AStringView s_Context = "Failed to write NWB mesh";
    if(!AssetWriterSkeletonDetail::ValidateMeshGeometry(mesh, s_Context))
        return false;
    if(!mesh.skin.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB mesh: mesh asset cannot contain a source skin stream"));
        return false;
    }
    for(const SourceVertexRef& ref : mesh.vertexRefs){
        if(ref.skin != s_MissingSourceStreamIndex){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB mesh: mesh asset vertex_ref cannot contain a skin index"));
            return false;
        }
    }
    return true;
}


template<typename Stream>
void WriteVertexRef(Stream& out, const SourceVertexRef& ref)noexcept(noexcept(out << "[" << ref.position << ", " << ref.normal << ", " << ref.tangent << ", " << ref.uv0 << ", " << ref.color << "]")){
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
void WriteMeshAssetBody(Stream& file, const SourceMeshStreams& mesh, const AStringView variableName = s_AssetVariableName, const AStringView indent = "")noexcept(
    noexcept(file << variableName << s_FieldAccessText << s_PositionsStreamLabel << s_ListAssignOpenText)
    && noexcept(file << indent << "    ")
    && noexcept(WriteVec3(file, DeclVal<const Vec3&>()))
    && noexcept(file << s_EntrySuffixText)
    && noexcept(file << indent << s_ListCloseSectionText)
    && noexcept(file << variableName << s_FieldAccessText << s_NormalsStreamLabel << s_ListAssignOpenText)
    && noexcept(file << variableName << s_FieldAccessText << s_TangentsStreamLabel << s_ListAssignOpenText)
    && noexcept(WriteVec4(file, DeclVal<const Vec4&>()))
    && noexcept(file << variableName << s_FieldAccessText << s_Uv0StreamLabel << s_ListAssignOpenText)
    && noexcept(WriteVec2(file, DeclVal<const Vec2&>()))
    && noexcept(file << variableName << s_FieldAccessText << s_ColorsStreamLabel << s_ListAssignOpenText)
    && noexcept(file << variableName << s_FieldAccessText << s_VertexRefsStreamLabel << s_ListAssignOpenText)
    && noexcept(WriteVertexRef(file, DeclVal<const SourceVertexRef&>()))
    && noexcept(file << variableName << s_FieldAccessText << s_IndicesStreamLabel << s_ListAssignOpenText)
    && noexcept(file << indent << "    [" << mesh.indices[DeclVal<usize>() + 0u] << ", " << mesh.indices[DeclVal<usize>() + 1u] << ", " << mesh.indices[DeclVal<usize>() + 2u] << s_RowSuffixText)
    && noexcept(file << indent << s_ListCloseText)
){
    file << variableName << s_FieldAccessText << s_PositionsStreamLabel << s_ListAssignOpenText;
    for(const Vec3& position : mesh.positions){
        file << indent << "    ";
        WriteVec3(file, position);
        file << s_EntrySuffixText;
    }
    file << indent << s_ListCloseSectionText;

    file << variableName << s_FieldAccessText << s_NormalsStreamLabel << s_ListAssignOpenText;
    for(const Vec3& normal : mesh.normals){
        file << indent << "    ";
        WriteVec3(file, normal);
        file << s_EntrySuffixText;
    }
    file << indent << s_ListCloseSectionText;

    file << variableName << s_FieldAccessText << s_TangentsStreamLabel << s_ListAssignOpenText;
    for(const Vec4& tangent : mesh.tangents){
        file << indent << "    ";
        WriteVec4(file, tangent);
        file << s_EntrySuffixText;
    }
    file << indent << s_ListCloseSectionText;

    file << variableName << s_FieldAccessText << s_Uv0StreamLabel << s_ListAssignOpenText;
    for(const Vec2& uv0 : mesh.uv0){
        file << indent << "    ";
        WriteVec2(file, uv0);
        file << s_EntrySuffixText;
    }
    file << indent << s_ListCloseSectionText;

    file << variableName << s_FieldAccessText << s_ColorsStreamLabel << s_ListAssignOpenText;
    for(const Vec4& color : mesh.colors){
        file << indent << "    ";
        WriteVec4(file, color);
        file << s_EntrySuffixText;
    }
    file << indent << s_ListCloseSectionText;

    file << variableName << s_FieldAccessText << s_VertexRefsStreamLabel << s_ListAssignOpenText;
    for(const SourceVertexRef& ref : mesh.vertexRefs){
        file << indent << "    ";
        WriteVertexRef(file, ref);
        file << s_EntrySuffixText;
    }
    file << indent << s_ListCloseSectionText;

    file << variableName << s_FieldAccessText << s_IndicesStreamLabel << s_ListAssignOpenText;
    for(usize i = 0u; i < mesh.indices.size(); i += s_TriangleIndexCount)
        file << indent << "    [" << mesh.indices[i + 0u] << ", " << mesh.indices[i + 1u] << ", " << mesh.indices[i + 2u] << s_RowSuffixText;
    file << indent << s_ListCloseText;
}


b8 NameUsed(const Span<const ScratchString> names, const AStringView name)noexcept{
    for(const ScratchString& usedName : names){
        if(usedName == name)
            return true;
    }
    return false;
}

ScratchString UniqueNodeName(const ufbx_node* node, const usize fallbackIndex, const Span<const ScratchString> usedNames, Core::Alloc::ScratchArena& scratchArena){
    const ScratchString baseName = AssetWriterSkeletonDetail::NodeName(node, fallbackIndex, scratchArena);
    ScratchString name(baseName, scratchArena);
    name.reserve(AddSize(baseName.size(), s_UniqueNameSuffixReserve));

    u32 suffix = 1u;
    while(NameUsed(usedNames, name)){
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
void WriteReferenceValue(Stream& file, const AStringView value, const b8 quote, Core::Alloc::ScratchArena& scratchArena){
    constexpr usize s_EscapedTextReserveFactor = 2u;
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
b8 WriteSkeletonAssetBody(Stream& file, const AStringView variableName, const Span<ufbx_node* const> joints, const Span<const JointMatrix> bindPoseMatrices, Core::Alloc::ScratchArena& scratchArena){
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
        WriteReferenceValue(file, jointNames[jointIndex], true, scratchArena);
        file << s_EntrySuffixText;
        if(joints[jointIndex] && joints[jointIndex]->parent){
            const auto foundParent = jointLookup.find(joints[jointIndex]->parent);
            if(foundParent != jointLookup.end()){
                const ScratchString& parentName = jointNames[foundParent.value()];
                file << "        \"parent\": ";
                WriteReferenceValue(file, parentName, true, scratchArena);
                file << s_EntrySuffixText;
            }
        }
        file << "        \"local_bind_pose\": ";
        WriteJointMatrix(file, bindPoseMatrices[jointIndex], "        ");
        file << s_EntrySuffixText;
        file << s_TableEntryCloseText;
    }
    file << s_ListCloseText;
    return true;
}


template<typename Stream>
void WriteSkinAssetBody(
    Stream& file,
    const AStringView variableName,
    const AStringView meshName,
    const AStringView skeletonName,
    const Span<const MeshSkinInfluence> influences,
    const Span<const JointMatrix> inverseBindMatrices,
    Core::Alloc::ScratchArena& scratchArena,
    const b8 quoteReferences = true
){
    file << variableName << ".mesh = ";
    WriteReferenceValue(file, meshName, quoteReferences, scratchArena);
    file << s_StatementEndText;
    file << variableName << ".skeleton = ";
    WriteReferenceValue(file, skeletonName, quoteReferences, scratchArena);
    file << s_StatementEndSectionText;

    file << variableName << ".influences = [\n";
    for(const MeshSkinInfluence& influence : influences){
        file << "    { \"joints\": ";
        WriteSkinJoints(file, influence);
        file << ", \"weights\": ";
        WriteSkinWeights(file, influence);
        file << " },\n";
    }
    file << s_ListCloseSectionText;

    file << variableName << ".inverse_bind_matrices = [\n";
    for(const JointMatrix& matrix : inverseBindMatrices){
        file << "    ";
        WriteJointMatrix(file, matrix, "    ");
        file << s_EntrySuffixText;
    }
    file << s_ListCloseText;
}


template<typename Stream>
void WriteModelAssetBody(
    Stream& file,
    const AStringView variableName,
    const AStringView meshName,
    const Optional<AStringView> skinName,
    const Optional<AStringView> skeletonName,
    Core::Alloc::ScratchArena& scratchArena,
    const AStringView skinnedMeshSkeletonName = s_SkeletonAssetKindText,
    const b8 quoteAssetReferences = true,
    const b8 quoteSkinnedMeshSkeletonReference = true
){
    if(skeletonName){
        file << variableName << ".skeletons = {\n";
        file << "    \"skeleton\": {\n";
        file << s_SkeletonPropertyText;
        WriteReferenceValue(file, *skeletonName, quoteAssetReferences, scratchArena);
        file << s_EntrySuffixText;
        file << s_TableEntryCloseText;
        file << s_TableCloseSectionText;

        file << variableName << ".skinned_meshes = {\n";
        file << "    \"mesh\": {\n";
        file << "        \"skin\": ";
        WriteReferenceValue(file, *skinName, quoteAssetReferences, scratchArena);
        file << s_EntrySuffixText;
        file << s_SkeletonPropertyText;
        WriteReferenceValue(file, skinnedMeshSkeletonName, quoteSkinnedMeshSkeletonReference, scratchArena);
        file << s_EntrySuffixText;
        file << s_TableEntryCloseText;
        file << s_TableCloseText;
    }
    else{
        file << variableName << ".static_meshes = {\n";
        file << "    \"base\": {\n";
        file << "        \"mesh\": ";
        WriteReferenceValue(file, meshName, quoteAssetReferences, scratchArena);
        file << s_EntrySuffixText;
        file << s_TableEntryCloseText;
        file << s_TableCloseText;
    }
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AssetWriterFormatDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 WriteMeshAsset(const ScratchPath& outputPath, const SourceMeshStreams& mesh){
    if(!__hidden_asset_writer_format::ValidatePlainMeshAsset(mesh))
        return false;
    if(!__hidden_asset_writer_format::EnsureOutputDirectory(outputPath, __hidden_asset_writer_format::s_MeshAssetKindText))
        return false;

    BasicOutputFileStream<char> rawFile(outputPath, s_FileOpenBinary | s_FileOpenTruncate);
    if(!rawFile){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB mesh: failed to open output file '{}'"), PathToString<tchar>(outputPath));
        return false;
    }
    __hidden_asset_writer_format::NwbTextOutputStream file(rawFile);
    file.precision(__hidden_asset_writer_format::s_OutputFloatPrecision);

    file << "mesh asset;\n\n";
    __hidden_asset_writer_format::WriteMeshAssetBody(file, mesh);

    rawFile.close();
    if(!file){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB mesh: failed while writing output file '{}'"), PathToString<tchar>(outputPath));
        return false;
    }
    return true;
}


b8 WriteSkeletonAsset(const ScratchPath& outputPath, const Span<ufbx_node* const> joints, const Span<const JointMatrix> bindPoseMatrices, Core::Alloc::ScratchArena& scratchArena){
    if(!__hidden_asset_writer_format::EnsureOutputDirectory(outputPath, __hidden_asset_writer_format::s_SkeletonAssetKindText))
        return false;

    BasicOutputFileStream<char> rawFile(outputPath, s_FileOpenBinary | s_FileOpenTruncate);
    if(!rawFile){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB skeleton: failed to open output file '{}'"), PathToString<tchar>(outputPath));
        return false;
    }
    __hidden_asset_writer_format::NwbTextOutputStream file(rawFile);
    file.precision(__hidden_asset_writer_format::s_OutputFloatPrecision);

    file << "skeleton asset;\n\n";
    if(!__hidden_asset_writer_format::WriteSkeletonAssetBody(file, __hidden_asset_writer_format::s_AssetVariableName, joints, bindPoseMatrices, scratchArena))
        return false;

    rawFile.close();
    if(!file){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB skeleton: failed while writing output file '{}'"), PathToString<tchar>(outputPath));
        return false;
    }
    return true;
}


b8 WriteSkinAsset(const ScratchPath& outputPath, const AStringView meshName, const AStringView skeletonName, const Span<const MeshSkinInfluence> influences, const Span<const JointMatrix> inverseBindMatrices, Core::Alloc::ScratchArena& scratchArena){
    if(influences.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB skin: no skin influences were produced"));
        return false;
    }
    if(!__hidden_asset_writer_format::EnsureOutputDirectory(outputPath, __hidden_asset_writer_format::s_SkinAssetKindText))
        return false;

    BasicOutputFileStream<char> rawFile(outputPath, s_FileOpenBinary | s_FileOpenTruncate);
    if(!rawFile){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB skin: failed to open output file '{}'"), PathToString<tchar>(outputPath));
        return false;
    }
    __hidden_asset_writer_format::NwbTextOutputStream file(rawFile);
    file.precision(__hidden_asset_writer_format::s_OutputFloatPrecision);

    file << "skin asset;\n\n";
    __hidden_asset_writer_format::WriteSkinAssetBody(file, __hidden_asset_writer_format::s_AssetVariableName, meshName, skeletonName, influences, inverseBindMatrices, scratchArena);

    rawFile.close();
    if(!file){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB skin: failed while writing output file '{}'"), PathToString<tchar>(outputPath));
        return false;
    }
    return true;
}


b8 WriteModelAsset(const ScratchPath& outputPath, const AStringView meshName, const Optional<AStringView> skinName, const Optional<AStringView> skeletonName, Core::Alloc::ScratchArena& scratchArena){
    if(!__hidden_asset_writer_format::EnsureOutputDirectory(outputPath, __hidden_asset_writer_format::s_ModelAssetKindText))
        return false;

    BasicOutputFileStream<char> rawFile(outputPath, s_FileOpenBinary | s_FileOpenTruncate);
    if(!rawFile){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB model: failed to open output file '{}'"), PathToString<tchar>(outputPath));
        return false;
    }
    __hidden_asset_writer_format::NwbTextOutputStream file(rawFile);
    file.precision(__hidden_asset_writer_format::s_OutputFloatPrecision);

    file << "model asset;\n\n";
    __hidden_asset_writer_format::WriteModelAssetBody(file, __hidden_asset_writer_format::s_AssetVariableName, meshName, skinName, skeletonName, scratchArena);

    rawFile.close();
    if(!file){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB model: failed while writing output file '{}'"), PathToString<tchar>(outputPath));
        return false;
    }
    return true;
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
    if(!__hidden_asset_writer_format::EnsureOutputDirectory(outputPath, __hidden_asset_writer_format::s_AssetBunchDisplayName))
        return false;

    BasicOutputFileStream<char> rawFile(outputPath, s_FileOpenBinary | s_FileOpenTruncate);
    if(!rawFile){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB asset bunch: failed to open output file '{}'"), PathToString<tchar>(outputPath));
        return false;
    }
    __hidden_asset_writer_format::NwbTextOutputStream file(rawFile);
    file.precision(__hidden_asset_writer_format::s_OutputFloatPrecision);

    file << "mesh mesh;\n\n";
    __hidden_asset_writer_format::WriteMeshAssetBody(file, mesh, __hidden_asset_writer_format::s_MeshAssetKindText);

    if(skinName && skeletonName && !skinInfluences.empty()){
        file << __hidden_asset_writer_format::s_AssetSectionSeparatorText;
        file << "skeleton skeleton;\n\n";
        if(!__hidden_asset_writer_format::WriteSkeletonAssetBody(file, __hidden_asset_writer_format::s_SkeletonAssetKindText, skeletonJoints, skeletonBindPoseMatrices, scratchArena))
            return false;

        file << __hidden_asset_writer_format::s_AssetSectionSeparatorText;
        file << "skin skin;\n\n";
        __hidden_asset_writer_format::WriteSkinAssetBody(file, __hidden_asset_writer_format::s_SkinAssetKindText, __hidden_asset_writer_format::s_MeshAssetKindText, __hidden_asset_writer_format::s_SkeletonAssetKindText, skinInfluences, inverseBindMatrices, scratchArena, false);
    }

    const b8 skinnedBunch = skinName && skeletonName && !skinInfluences.empty();
    file << __hidden_asset_writer_format::s_AssetSectionSeparatorText;
    file << "model model;\n\n";
    if(skinnedBunch){
        __hidden_asset_writer_format::WriteModelAssetBody(file, __hidden_asset_writer_format::s_ModelAssetKindText, __hidden_asset_writer_format::s_MeshAssetKindText, __hidden_asset_writer_format::s_SkinAssetKindText, __hidden_asset_writer_format::s_SkeletonAssetKindText, scratchArena, __hidden_asset_writer_format::s_SkeletonAssetKindText, false, true);
    }
    else{
        __hidden_asset_writer_format::WriteModelAssetBody(file, __hidden_asset_writer_format::s_ModelAssetKindText, __hidden_asset_writer_format::s_MeshAssetKindText, {}, {}, scratchArena, __hidden_asset_writer_format::s_SkeletonAssetKindText, false, false);
    }

    file << __hidden_asset_writer_format::s_AssetSectionSeparatorText;
    file << "asset_bunch bunch = [\n";
    file << "    mesh,\n";
    if(skinName && skeletonName && !skinInfluences.empty()){
        file << "    skeleton,\n";
        file << "    skin,\n";
    }
    file << "    model,\n";
    file << __hidden_asset_writer_format::s_ListCloseText;

    rawFile.close();
    if(!file){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB asset bunch: failed while writing output file '{}'"), PathToString<tchar>(outputPath));
        return false;
    }
    return true;
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

