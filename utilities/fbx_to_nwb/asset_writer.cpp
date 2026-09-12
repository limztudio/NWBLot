// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "module.h"
#include "asset_writer_skeleton.h"
#include <global/text_write.h>

#include <core/common/log.h>


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



// Keep every generated metadata file canonical even when the converter runs on a non-Windows host.  The writers below
// stream potentially large mesh payloads directly to disk, so normalize text at this one output boundary rather than
// accumulating a second full copy of each asset just to replace line endings afterwards.
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

    NwbTextOutputStream& operator<<(const char* text){
        if(text)
            writeText(AStringView(text));
        return *this;
    }

    NwbTextOutputStream& operator<<(const AStringView text){
        writeText(text);
        return *this;
    }

    NwbTextOutputStream& operator<<(const AString& text){
        writeText(AStringView(text.data(), text.size()));
        return *this;
    }

    template<typename T>
    NwbTextOutputStream& operator<<(const T& value){
        m_stream << value;
        return *this;
    }


public:
    void precision(const StreamSize precision){ m_stream.precision(precision); }

    [[nodiscard]] explicit operator bool()const{ return static_cast<bool>(m_stream); }
    [[nodiscard]] bool operator!()const{ return !m_stream; }


private:
    void writeText(const AStringView text){
        const char* const data = text.data();
        usize chunkBegin = 0u;
        for(usize i = 0u; i < text.size(); ++i){
            const char character = data[i];
            if(character != '\r' && character != '\n')
                continue;

            if(i > chunkBegin)
                m_stream.write(data + chunkBegin, static_cast<StreamSize>(i - chunkBegin));

            if(character == '\r' && i + 1u < text.size() && data[i + 1u] == '\n')
                ++i;
            m_stream.write("\r\n", 2);
            chunkBegin = i + 1u;
        }

        if(chunkBegin < text.size())
            m_stream.write(data + chunkBegin, static_cast<StreamSize>(text.size() - chunkBegin));
    }


private:
    BasicOutputFileStream<char>& m_stream;
};


template<typename Stream>
void WriteSkinJoints(Stream& out, const MeshSkinInfluence& skin){
    out << "[";
    for(usize i = 0u; i < s_MeshSkinInfluenceCount; ++i){
        if(i != 0u)
            out << ", ";
        out << skin.joint[i];
    }
    out << "]";
}

template<typename Stream>
void WriteSkinWeights(Stream& out, const MeshSkinInfluence& skin){
    out << "[";
    for(usize i = 0u; i < s_MeshSkinInfluenceCount; ++i){
        if(i != 0u)
            out << ", ";
        WriteFloat(out, skin.weight.raw[i]);
    }
    out << "]";
}

template<typename Stream>
void WriteJointMatrix(Stream& out, const JointMatrix& matrix, const AStringView indent = ""){
    out << "[\n";
    for(const Vec4& row : matrix.rows){
        out << indent << "    ";
        WriteVec4(out, row);
        out << ",\n";
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
void WriteVertexRef(Stream& out, const SourceVertexRef& ref){
    out << "[" << ref.position << ", " << ref.normal << ", " << ref.tangent << ", " << ref.uv0 << ", " << ref.color << "]";
}

bool EnsureOutputDirectory(const Path& outputPath, const char* assetKind){
    ErrorCode errorCode;
    const Path parentPath = outputPath.parent_path();
    if(parentPath.empty())
        return true;

    if(EnsureDirectories(parentPath, errorCode))
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB {}: failed to create output directory '{}': {}")
        , StringConvert(assetKind)
        , PathToString<tchar>(parentPath)
        , StringConvert(errorCode.message())
    );
    return false;
}


template<typename Stream>
void WriteMeshAssetBody(Stream& file, const SourceMeshStreams& mesh, const AStringView variableName = "asset", const AStringView indent = ""){
    file << variableName << ".positions = [\n";
    for(const Vec3& position : mesh.positions){
        file << indent << "    ";
        WriteVec3(file, position);
        file << ",\n";
    }
    file << indent << "];\n\n";

    file << variableName << ".normals = [\n";
    for(const Vec3& normal : mesh.normals){
        file << indent << "    ";
        WriteVec3(file, normal);
        file << ",\n";
    }
    file << indent << "];\n\n";

    file << variableName << ".tangents = [\n";
    for(const Vec4& tangent : mesh.tangents){
        file << indent << "    ";
        WriteVec4(file, tangent);
        file << ",\n";
    }
    file << indent << "];\n\n";

    file << variableName << ".uv0 = [\n";
    for(const Vec2& uv0 : mesh.uv0){
        file << indent << "    ";
        WriteVec2(file, uv0);
        file << ",\n";
    }
    file << indent << "];\n\n";

    file << variableName << ".colors = [\n";
    for(const Vec4& color : mesh.colors){
        file << indent << "    ";
        WriteVec4(file, color);
        file << ",\n";
    }
    file << indent << "];\n\n";

    file << variableName << ".vertex_refs = [\n";
    for(const SourceVertexRef& ref : mesh.vertexRefs){
        file << indent << "    ";
        WriteVertexRef(file, ref);
        file << ",\n";
    }
    file << indent << "];\n\n";

    file << variableName << ".indices = [\n";
    for(usize i = 0u; i < mesh.indices.size(); i += s_TriangleIndexCount)
        file << indent << "    [" << mesh.indices[i + 0u] << ", " << mesh.indices[i + 1u] << ", " << mesh.indices[i + 2u] << "],\n";
    file << indent << "];\n";
}

bool WriteMeshAsset(const Path& outputPath, const SourceMeshStreams& mesh){
    if(!ValidatePlainMeshAsset(mesh))
        return false;
    if(!EnsureOutputDirectory(outputPath, "mesh"))
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

    if(!file){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB mesh: failed while writing output file '{}'"), PathToString<tchar>(outputPath));
        return false;
    }
    return true;
}

bool NameUsed(const UtilityVector<AString>& names, const AString& name){
    for(const AString& usedName : names){
        if(usedName == name)
            return true;
    }
    return false;
}

AString UniqueNodeName(const ufbx_node* node, const usize fallbackIndex, UtilityVector<AString>& usedNames){
    const AString baseName = AssetWriterSkeletonDetail::NodeName(node, fallbackIndex);
    AString name = baseName;

    u32 suffix = 1u;
    while(NameUsed(usedNames, name)){
        AStringStream out;
        out << baseName << "_" << suffix++;
        name = out.str();
    }

    usedNames.push_back(name);
    return name;
}

UtilityVector<AString> BuildUniqueJointNames(const UtilityVector<ufbx_node*>& joints){
    UtilityVector<AString> usedNames;
    usedNames.reserve(joints.size());

    UtilityVector<AString> names;
    names.reserve(joints.size());
    for(usize jointIndex = 0u; jointIndex < joints.size(); ++jointIndex)
        names.push_back(UniqueNodeName(joints[jointIndex], jointIndex, usedNames));

    return names;
}

AString BuildVirtualBasePath(const Path& outputPath, AString virtualRoot){
    virtualRoot = TrimCopy(Move(virtualRoot));
    if(virtualRoot.empty())
        virtualRoot = "project";

    Path noExtension = outputPath;
    noExtension.replace_extension();

    Path relativePath(outputPath.arena());
    bool foundAssets = false;
    for(const Path& part : noExtension){
        const AString partText = ToAsciiLowerCopy(PathToGenericString<AString>(part));
        if(partText == "assets"){
            foundAssets = true;
            relativePath.clear();
            continue;
        }
        if(foundAssets)
            relativePath /= part;
    }
    if(relativePath.empty())
        relativePath = noExtension.filename();

    AString virtualPath = Move(virtualRoot);
    if(!virtualPath.empty() && virtualPath.back() != '/')
        virtualPath.push_back('/');

    AString relativeText = PathToGenericString<AString>(relativePath);
    while(!relativeText.empty() && (relativeText.front() == '/' || relativeText.front() == '\\'))
        relativeText.erase(relativeText.begin());
    virtualPath += relativeText;
    return virtualPath;
}

template<typename Stream>
void WriteReferenceValue(Stream& file, const AStringView value, const bool quote){
    if(quote)
        file << "\"" << MakeJsonEscapedText<AString>(value) << "\"";
    else
        file << value;
}

template<typename Stream>
void WriteSkeletonAssetBody(
    Stream& file,
    const AStringView variableName,
    const UtilityVector<ufbx_node*>& joints,
    const UtilityVector<JointMatrix>& bindPoseMatrices
){
    file << variableName << ".joints = [\n";
    const UtilityVector<AString> jointNames = BuildUniqueJointNames(joints);

    HashMap<const ufbx_node*, usize> jointLookup;
    jointLookup.reserve(joints.size());
    for(usize jointIndex = 0u; jointIndex < joints.size(); ++jointIndex)
        jointLookup.emplace(joints[jointIndex], jointIndex);

    for(usize jointIndex = 0u; jointIndex < joints.size(); ++jointIndex){
        file << "    {\n";
        file << "        \"name\": \"" << MakeJsonEscapedText<AString>(AStringView(jointNames[jointIndex].data(), jointNames[jointIndex].size())) << "\",\n";
        if(joints[jointIndex] && joints[jointIndex]->parent){
            const auto foundParent = jointLookup.find(joints[jointIndex]->parent);
            if(foundParent != jointLookup.end()){
                const AString& parentName = jointNames[foundParent.value()];
                file << "        \"parent\": \"" << MakeJsonEscapedText<AString>(AStringView(parentName.data(), parentName.size())) << "\",\n";
            }
        }
        file << "        \"local_bind_pose\": ";
        WriteJointMatrix(file, bindPoseMatrices[jointIndex], "        ");
        file << ",\n";
        file << "    },\n";
    }
    file << "];\n";
}

bool WriteSkeletonAsset(
    const Path& outputPath,
    const UtilityVector<ufbx_node*>& joints,
    const UtilityVector<JointMatrix>& bindPoseMatrices
){
    if(!EnsureOutputDirectory(outputPath, "skeleton"))
        return false;

    BasicOutputFileStream<char> rawFile(outputPath, s_FileOpenBinary | s_FileOpenTruncate);
    if(!rawFile){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB skeleton: failed to open output file '{}'"), PathToString<tchar>(outputPath));
        return false;
    }
    NwbTextOutputStream file(rawFile);
    file.precision(s_OutputFloatPrecision);

    file << "skeleton asset;\n\n";
    WriteSkeletonAssetBody(file, "asset", joints, bindPoseMatrices);

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
    const AString& meshName,
    const AString& skeletonName,
    const UtilityVector<MeshSkinInfluence>& influences,
    const UtilityVector<JointMatrix>& inverseBindMatrices,
    const bool quoteReferences = true
){
    file << variableName << ".mesh = ";
    WriteReferenceValue(file, meshName, quoteReferences);
    file << ";\n";
    file << variableName << ".skeleton = ";
    WriteReferenceValue(file, skeletonName, quoteReferences);
    file << ";\n\n";

    file << variableName << ".influences = [\n";
    for(const MeshSkinInfluence& influence : influences){
        file << "    { \"joints\": ";
        WriteSkinJoints(file, influence);
        file << ", \"weights\": ";
        WriteSkinWeights(file, influence);
        file << " },\n";
    }
    file << "];\n\n";

    file << variableName << ".inverse_bind_matrices = [\n";
    for(const JointMatrix& matrix : inverseBindMatrices){
        file << "    ";
        WriteJointMatrix(file, matrix, "    ");
        file << ",\n";
    }
    file << "];\n";
}

bool WriteSkinAsset(
    const Path& outputPath,
    const AString& meshName,
    const AString& skeletonName,
    const UtilityVector<MeshSkinInfluence>& influences,
    const UtilityVector<JointMatrix>& inverseBindMatrices
){
    if(influences.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB skin: no skin influences were produced"));
        return false;
    }
    if(!EnsureOutputDirectory(outputPath, "skin"))
        return false;

    BasicOutputFileStream<char> rawFile(outputPath, s_FileOpenBinary | s_FileOpenTruncate);
    if(!rawFile){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB skin: failed to open output file '{}'"), PathToString<tchar>(outputPath));
        return false;
    }
    NwbTextOutputStream file(rawFile);
    file.precision(s_OutputFloatPrecision);

    file << "skin asset;\n\n";
    WriteSkinAssetBody(file, "asset", meshName, skeletonName, influences, inverseBindMatrices);

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
    const AString& meshName,
    const AString* skinName,
    const AString* skeletonName,
    const AStringView skinnedMeshSkeletonName = "skeleton",
    const bool quoteAssetReferences = true,
    const bool quoteSkinnedMeshSkeletonReference = true
){
    if(skeletonName){
        file << variableName << ".skeletons = {\n";
        file << "    \"skeleton\": {\n";
        file << "        \"skeleton\": ";
        WriteReferenceValue(file, *skeletonName, quoteAssetReferences);
        file << ",\n";
        file << "    },\n";
        file << "};\n\n";

        file << variableName << ".skinned_meshes = {\n";
        file << "    \"mesh\": {\n";
        file << "        \"mesh\": ";
        WriteReferenceValue(file, meshName, quoteAssetReferences);
        file << ",\n";
        file << "        \"skin\": ";
        WriteReferenceValue(file, *skinName, quoteAssetReferences);
        file << ",\n";
        file << "        \"skeleton\": ";
        WriteReferenceValue(file, skinnedMeshSkeletonName, quoteSkinnedMeshSkeletonReference);
        file << ",\n";
        file << "    },\n";
        file << "};\n";
    }
    else{
        file << variableName << ".static_meshes = {\n";
        file << "    \"base\": {\n";
        file << "        \"mesh\": ";
        WriteReferenceValue(file, meshName, quoteAssetReferences);
        file << ",\n";
        file << "    },\n";
        file << "};\n";
    }
}

bool WriteModelAsset(
    const Path& outputPath,
    const AString& meshName,
    const AString* skinName,
    const AString* skeletonName
){
    if(!EnsureOutputDirectory(outputPath, "model"))
        return false;

    BasicOutputFileStream<char> rawFile(outputPath, s_FileOpenBinary | s_FileOpenTruncate);
    if(!rawFile){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB model: failed to open output file '{}'"), PathToString<tchar>(outputPath));
        return false;
    }
    NwbTextOutputStream file(rawFile);
    file.precision(s_OutputFloatPrecision);

    file << "model asset;\n\n";
    WriteModelAssetBody(file, "asset", meshName, skinName, skeletonName);

    if(!file){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB model: failed while writing output file '{}'"), PathToString<tchar>(outputPath));
        return false;
    }
    return true;
}

bool WriteAssetBunch(
    const Path& outputPath,
    const SourceMeshStreams& mesh,
    const AString* skinName,
    const AString* skeletonName,
    const UtilityVector<ufbx_node*>& skeletonJoints,
    const UtilityVector<JointMatrix>& skeletonBindPoseMatrices,
    const UtilityVector<MeshSkinInfluence>* skinInfluences,
    const UtilityVector<JointMatrix>& inverseBindMatrices
){
    if(!EnsureOutputDirectory(outputPath, "asset bunch"))
        return false;

    BasicOutputFileStream<char> rawFile(outputPath, s_FileOpenBinary | s_FileOpenTruncate);
    if(!rawFile){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB asset bunch: failed to open output file '{}'"), PathToString<tchar>(outputPath));
        return false;
    }
    NwbTextOutputStream file(rawFile);
    file.precision(s_OutputFloatPrecision);

    file << "mesh mesh;\n\n";
    WriteMeshAssetBody(file, mesh, "mesh");

    if(skinName && skeletonName && skinInfluences){
        file << "\n\n";
        file << "skeleton skeleton;\n\n";
        WriteSkeletonAssetBody(file, "skeleton", skeletonJoints, skeletonBindPoseMatrices);

        file << "\n\n";
        file << "skin skin;\n\n";
        WriteSkinAssetBody(file, "skin", "mesh", "skeleton", *skinInfluences, inverseBindMatrices, false);
    }

    const bool skinnedBunch = skinName && skeletonName && skinInfluences;
    file << "\n\n";
    file << "model model;\n\n";
    if(skinnedBunch){
        const AString localMeshName("mesh");
        const AString localSkinName("skin");
        const AString localSkeletonName("skeleton");
        WriteModelAssetBody(file, "model", localMeshName, &localSkinName, &localSkeletonName, "skeleton", false, true);
    }
    else{
        WriteModelAssetBody(file, "model", "mesh", nullptr, nullptr, "skeleton", false, false);
    }

    file << "\n\n";
    file << "asset_bunch bunch = [\n";
    file << "    mesh,\n";
    if(skinName && skeletonName && skinInfluences){
        file << "    skeleton,\n";
        file << "    skin,\n";
    }
    file << "    model,\n";
    file << "];\n";

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
    const Path& outputPath,
    const SourceMeshStreams& mesh,
    const AString& assetTypeText,
    const AString& virtualRoot,
    const bool separateAssets,
    const UtilityVector<ufbx_node*>& skeletonJoints,
    const UtilityVector<JointMatrix>& skeletonBindPoseMatrices,
    const UtilityVector<JointMatrix>& inverseBindMatrices
){
    OutputAssetType::Enum assetType = OutputAssetType::Mesh;
    if(!ParseAssetTypeText(assetTypeText, assetType)){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB asset: {}"), StringConvert(OutputAssetTypeErrorText()));
        return false;
    }

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
    const Path packageDirectory = outputPath.parent_path() / outputPath.stem();
    const Path meshPath = packageDirectory / "mesh.nwb";
    const AString virtualBase = __hidden_asset_writer::BuildVirtualBasePath(outputPath, virtualRoot);
    const AString meshName = virtualBase + "/mesh";

    if(assetType == OutputAssetType::Model){
        if(skinnedModel){
            const AString skeletonName = virtualBase + "/skeleton";
            const AString skinName = virtualBase + "/skin";
            return __hidden_asset_writer::WriteModelAsset(outputPath, meshName, &skinName, &skeletonName);
        }
        return __hidden_asset_writer::WriteModelAsset(outputPath, meshName, nullptr, nullptr);
    }

    if(!skinnedModel){
        if(assetType == OutputAssetType::Skeleton || assetType == OutputAssetType::Skin){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB asset: requested asset type requires a skinned source mesh"));
            return false;
        }
        if(assetType == OutputAssetType::Bunch && !separateAssets)
            return __hidden_asset_writer::WriteAssetBunch(
                outputPath,
                mesh,
                nullptr,
                nullptr,
                skeletonJoints,
                skeletonBindPoseMatrices,
                nullptr,
                inverseBindMatrices
            );

        if(!__hidden_asset_writer::WriteMeshAsset(meshPath, mesh))
            return false;
        return __hidden_asset_writer::WriteModelAsset(outputPath, meshName, nullptr, nullptr);
    }

    if(!AssetWriterSkeletonDetail::ValidateSplitSkinSource(mesh, skeletonJoints, skeletonBindPoseMatrices, inverseBindMatrices))
        return false;

    AssetWriterSkeletonDetail::SkeletonOutputData skeletonOutput;
    if(!AssetWriterSkeletonDetail::BuildSkeletonOutputData(
        skeletonJoints,
        skeletonBindPoseMatrices,
        inverseBindMatrices,
        skeletonOutput
    ))
        return false;

    SourceMeshStreams splitMesh;
    UtilityVector<MeshSkinInfluence> positionSkin;
    if(!AssetWriterSkeletonDetail::BuildPositionAlignedSkinnedMesh(mesh, splitMesh, positionSkin))
        return false;
    if(!AssetWriterSkeletonDetail::RemapSkinInfluences(positionSkin, skeletonOutput.oldToNewJointIndices))
        return false;

    const Path skeletonPath = packageDirectory / "skeleton.nwb";
    const Path skinPath = packageDirectory / "skin.nwb";
    const AString skeletonName = virtualBase + "/skeleton";
    const AString skinName = virtualBase + "/skin";

    if(assetType == OutputAssetType::Skeleton)
        return __hidden_asset_writer::WriteSkeletonAsset(outputPath, skeletonOutput.joints, skeletonOutput.bindPoseMatrices);
    if(assetType == OutputAssetType::Skin)
        return __hidden_asset_writer::WriteSkinAsset(outputPath, meshName, skeletonName, positionSkin, skeletonOutput.inverseBindMatrices);
    if(assetType == OutputAssetType::Bunch && !separateAssets)
        return __hidden_asset_writer::WriteAssetBunch(
            outputPath,
            splitMesh,
            &skinName,
            &skeletonName,
            skeletonOutput.joints,
            skeletonOutput.bindPoseMatrices,
            &positionSkin,
            skeletonOutput.inverseBindMatrices
        );

    return __hidden_asset_writer::WriteMeshAsset(meshPath, splitMesh)
        && __hidden_asset_writer::WriteSkeletonAsset(skeletonPath, skeletonOutput.joints, skeletonOutput.bindPoseMatrices)
        && __hidden_asset_writer::WriteSkinAsset(skinPath, meshName, skeletonName, positionSkin, skeletonOutput.inverseBindMatrices)
        && __hidden_asset_writer::WriteModelAsset(outputPath, meshName, &skinName, &skeletonName)
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
