// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "module.h"
#include "asset_writer_skeleton.h"
#include "text_write.h"

#include <core/common/log.h>

#include <global/allocation_size.h>
#include <global/json_text.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_asset_writer{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TextWrite::WriteFloat;
using TextWrite::WriteVec2;
using TextWrite::WriteVec3;
using TextWrite::WriteVec4;
using TextWrite::s_OutputFloatPrecision;

inline constexpr AStringView s_MeshSplitFileName = "mesh.nwb";
inline constexpr AStringView s_SkeletonSplitFileName = "skeleton.nwb";
inline constexpr AStringView s_SkinSplitFileName = "skin.nwb";
inline constexpr AStringView s_AssetsSourceDirectoryText = "assets";
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
inline constexpr AStringView s_MeshNameSuffix = "/mesh";
inline constexpr AStringView s_SkeletonNameSuffix = "/skeleton";
inline constexpr AStringView s_SkinNameSuffix = "/skin";
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
    [[nodiscard]] explicit operator bool()const noexcept{ return static_cast<bool>(m_stream); }

    [[nodiscard]] bool operator!()const noexcept{ return !m_stream; }


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


bool ValidatePlainMeshAsset(const SourceMeshStreams& mesh){
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

bool EnsureOutputDirectory(const ScratchPath& outputPath, const AStringView assetKind){
    const ScratchPath parentPath = outputPath.parentPath();
    if(parentPath.empty())
        return true;

    const auto created = EnsureDirectories(parentPath);
    if(created)
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB {}: failed to create output directory '{}': {}"), StringConvert(assetKind), PathToString<tchar>(parentPath), StringConvert(created.error().message()));
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

bool WriteMeshAsset(const ScratchPath& outputPath, const SourceMeshStreams& mesh){
    if(!ValidatePlainMeshAsset(mesh))
        return false;
    if(!EnsureOutputDirectory(outputPath, s_MeshAssetKindText))
        return false;

    BasicOutputFileStream<char> rawFile(outputPath, s_FileOpenBinary | s_FileOpenTruncate);
    if(!rawFile){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB mesh: failed to open output file '{}'"), PathToString<tchar>(outputPath));
        return false;
    }
    NwbTextOutputStream file(rawFile);
    file.precision(s_OutputFloatPrecision);

    file << "mesh asset;\n\n";
    WriteMeshAssetBody(file, mesh);

    rawFile.close();
    if(!file){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB mesh: failed while writing output file '{}'"), PathToString<tchar>(outputPath));
        return false;
    }
    return true;
}

bool NameUsed(const UtilityScratchVector<ScratchString>& names, const AStringView name)noexcept{
    for(const ScratchString& usedName : names){
        if(usedName == name)
            return true;
    }
    return false;
}

ScratchString UniqueNodeName(const ufbx_node* node, const usize fallbackIndex, const UtilityScratchVector<ScratchString>& usedNames, Core::Alloc::ScratchArena& scratchArena){
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

UtilityScratchVector<ScratchString> BuildUniqueJointNames(const UtilityVector<ufbx_node*>& joints, Core::Alloc::ScratchArena& scratchArena){
    UtilityScratchVector<ScratchString> names(scratchArena);
    names.reserve(joints.size());
    for(usize jointIndex = 0u; jointIndex < joints.size(); ++jointIndex)
        names.push_back(UniqueNodeName(joints[jointIndex], jointIndex, names, scratchArena));

    return names;
}

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

template<typename Stream>
void WriteReferenceValue(Stream& file, const AStringView value, const bool quote, Core::Alloc::ScratchArena& scratchArena){
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
bool WriteSkeletonAssetBody(Stream& file, const AStringView variableName, const UtilityVector<ufbx_node*>& joints, const UtilityVector<JointMatrix>& bindPoseMatrices, Core::Alloc::ScratchArena& scratchArena){
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

bool WriteSkeletonAsset(const ScratchPath& outputPath, const UtilityVector<ufbx_node*>& joints, const UtilityVector<JointMatrix>& bindPoseMatrices, Core::Alloc::ScratchArena& scratchArena){
    if(!EnsureOutputDirectory(outputPath, s_SkeletonAssetKindText))
        return false;

    BasicOutputFileStream<char> rawFile(outputPath, s_FileOpenBinary | s_FileOpenTruncate);
    if(!rawFile){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB skeleton: failed to open output file '{}'"), PathToString<tchar>(outputPath));
        return false;
    }
    NwbTextOutputStream file(rawFile);
    file.precision(s_OutputFloatPrecision);

    file << "skeleton asset;\n\n";
    if(!WriteSkeletonAssetBody(file, s_AssetVariableName, joints, bindPoseMatrices, scratchArena))
        return false;

    rawFile.close();
    if(!file){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB skeleton: failed while writing output file '{}'"), PathToString<tchar>(outputPath));
        return false;
    }
    return true;
}

template<typename Stream>
void WriteSkinAssetBody(
    Stream& file,
    const AStringView variableName,
    const AStringView meshName,
    const AStringView skeletonName,
    const UtilityVector<MeshSkinInfluence>& influences,
    const UtilityVector<JointMatrix>& inverseBindMatrices,
    Core::Alloc::ScratchArena& scratchArena,
    const bool quoteReferences = true
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

bool WriteSkinAsset(const ScratchPath& outputPath, const AStringView meshName, const AStringView skeletonName, const UtilityVector<MeshSkinInfluence>& influences, const UtilityVector<JointMatrix>& inverseBindMatrices, Core::Alloc::ScratchArena& scratchArena){
    if(influences.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB skin: no skin influences were produced"));
        return false;
    }
    if(!EnsureOutputDirectory(outputPath, s_SkinAssetKindText))
        return false;

    BasicOutputFileStream<char> rawFile(outputPath, s_FileOpenBinary | s_FileOpenTruncate);
    if(!rawFile){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB skin: failed to open output file '{}'"), PathToString<tchar>(outputPath));
        return false;
    }
    NwbTextOutputStream file(rawFile);
    file.precision(s_OutputFloatPrecision);

    file << "skin asset;\n\n";
    WriteSkinAssetBody(file, s_AssetVariableName, meshName, skeletonName, influences, inverseBindMatrices, scratchArena);

    rawFile.close();
    if(!file){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB skin: failed while writing output file '{}'"), PathToString<tchar>(outputPath));
        return false;
    }
    return true;
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
    const bool quoteAssetReferences = true,
    const bool quoteSkinnedMeshSkeletonReference = true
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

bool WriteModelAsset(const ScratchPath& outputPath, const AStringView meshName, const Optional<AStringView> skinName, const Optional<AStringView> skeletonName, Core::Alloc::ScratchArena& scratchArena){
    if(!EnsureOutputDirectory(outputPath, s_ModelAssetKindText))
        return false;

    BasicOutputFileStream<char> rawFile(outputPath, s_FileOpenBinary | s_FileOpenTruncate);
    if(!rawFile){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB model: failed to open output file '{}'"), PathToString<tchar>(outputPath));
        return false;
    }
    NwbTextOutputStream file(rawFile);
    file.precision(s_OutputFloatPrecision);

    file << "model asset;\n\n";
    WriteModelAssetBody(file, s_AssetVariableName, meshName, skinName, skeletonName, scratchArena);

    rawFile.close();
    if(!file){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB model: failed while writing output file '{}'"), PathToString<tchar>(outputPath));
        return false;
    }
    return true;
}

bool WriteAssetBunch(
    const ScratchPath& outputPath,
    const SourceMeshStreams& mesh,
    const Optional<AStringView> skinName,
    const Optional<AStringView> skeletonName,
    const UtilityVector<ufbx_node*>& skeletonJoints,
    const UtilityVector<JointMatrix>& skeletonBindPoseMatrices,
    const UtilityVector<MeshSkinInfluence>* skinInfluences,
    const UtilityVector<JointMatrix>& inverseBindMatrices,
    Core::Alloc::ScratchArena& scratchArena
){
    if(!EnsureOutputDirectory(outputPath, s_AssetBunchDisplayName))
        return false;

    BasicOutputFileStream<char> rawFile(outputPath, s_FileOpenBinary | s_FileOpenTruncate);
    if(!rawFile){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB asset bunch: failed to open output file '{}'"), PathToString<tchar>(outputPath));
        return false;
    }
    NwbTextOutputStream file(rawFile);
    file.precision(s_OutputFloatPrecision);

    file << "mesh mesh;\n\n";
    WriteMeshAssetBody(file, mesh, s_MeshAssetKindText);

    if(skinName && skeletonName && skinInfluences){
        file << s_AssetSectionSeparatorText;
        file << "skeleton skeleton;\n\n";
        if(!WriteSkeletonAssetBody(file, s_SkeletonAssetKindText, skeletonJoints, skeletonBindPoseMatrices, scratchArena))
            return false;

        file << s_AssetSectionSeparatorText;
        file << "skin skin;\n\n";
        WriteSkinAssetBody(file, s_SkinAssetKindText, s_MeshAssetKindText, s_SkeletonAssetKindText, *skinInfluences, inverseBindMatrices, scratchArena, false);
    }

    const bool skinnedBunch = skinName && skeletonName && skinInfluences;
    file << s_AssetSectionSeparatorText;
    file << "model model;\n\n";
    if(skinnedBunch){
        WriteModelAssetBody(file, s_ModelAssetKindText, s_MeshAssetKindText, s_SkinAssetKindText, s_SkeletonAssetKindText, scratchArena, s_SkeletonAssetKindText, false, true);
    }
    else{
        WriteModelAssetBody(file, s_ModelAssetKindText, s_MeshAssetKindText, {}, {}, scratchArena, s_SkeletonAssetKindText, false, false);
    }

    file << s_AssetSectionSeparatorText;
    file << "asset_bunch bunch = [\n";
    file << "    mesh,\n";
    if(skinName && skeletonName && skinInfluences){
        file << "    skeleton,\n";
        file << "    skin,\n";
    }
    file << "    model,\n";
    file << s_ListCloseText;

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
        return __hidden_asset_writer::WriteMeshAsset(outputPath, mesh);
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
            return __hidden_asset_writer::WriteModelAsset(outputPath, meshName, AStringView(skinName), AStringView(skeletonName), scratchArena);
        }
        return __hidden_asset_writer::WriteModelAsset(outputPath, meshName, {}, {}, scratchArena);
    }

    if(!skinnedModel){
        if(assetType == OutputAssetType::Skeleton || assetType == OutputAssetType::Skin){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB asset: requested asset type requires a skinned source mesh"));
            return false;
        }
        if(assetType == OutputAssetType::Bunch && !separateAssets)
            return __hidden_asset_writer::WriteAssetBunch(outputPath, mesh, {}, {}, skeletonJoints, skeletonBindPoseMatrices, nullptr, inverseBindMatrices, scratchArena);

        if(!__hidden_asset_writer::WriteMeshAsset(meshPath, mesh))
            return false;
        return __hidden_asset_writer::WriteModelAsset(outputPath, meshName, {}, {}, scratchArena);
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
        return __hidden_asset_writer::WriteSkeletonAsset(outputPath, skeletonOutput.joints, skeletonOutput.bindPoseMatrices, scratchArena);
    if(assetType == OutputAssetType::Skin)
        return __hidden_asset_writer::WriteSkinAsset(outputPath, meshName, skeletonName, positionSkin, skeletonOutput.inverseBindMatrices, scratchArena);
    if(assetType == OutputAssetType::Bunch && !separateAssets)
        return __hidden_asset_writer::WriteAssetBunch(outputPath, splitMesh, AStringView(skinName), AStringView(skeletonName), skeletonOutput.joints, skeletonOutput.bindPoseMatrices, &positionSkin, skeletonOutput.inverseBindMatrices, scratchArena);

    return
        __hidden_asset_writer::WriteMeshAsset(meshPath, splitMesh)
        && __hidden_asset_writer::WriteSkeletonAsset(skeletonPath, skeletonOutput.joints, skeletonOutput.bindPoseMatrices, scratchArena)
        && __hidden_asset_writer::WriteSkinAsset(skinPath, meshName, skeletonName, positionSkin, skeletonOutput.inverseBindMatrices, scratchArena)
        && __hidden_asset_writer::WriteModelAsset(outputPath, meshName, AStringView(skinName), AStringView(skeletonName), scratchArena)
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

