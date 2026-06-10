// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "module.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_asset_writer{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


f32 CleanFloat(const f32 value){
    if(Abs(value) < 0.00000001f)
        return 0.0f;
    return value;
}

template<typename Stream>
void WriteFloat(Stream& out, const f32 value){
    out << CleanFloat(value);
}

template<typename Stream>
void WriteVec2(Stream& out, const Vec2& value){
    out << "[";
    WriteFloat(out, value.x);
    out << ", ";
    WriteFloat(out, value.y);
    out << "]";
}

template<typename Stream>
void WriteVec3(Stream& out, const Vec3& value){
    out << "[";
    WriteFloat(out, value.x);
    out << ", ";
    WriteFloat(out, value.y);
    out << ", ";
    WriteFloat(out, value.z);
    out << "]";
}

template<typename Stream>
void WriteVec4(Stream& out, const Vec4& value){
    out << "[";
    WriteFloat(out, value.x);
    out << ", ";
    WriteFloat(out, value.y);
    out << ", ";
    WriteFloat(out, value.z);
    out << ", ";
    WriteFloat(out, value.w);
    out << "]";
}

template<typename Stream>
void WriteSkinJoints(Stream& out, const MeshSkinInfluence& skin){
    out << "["
        << skin.joint[0u] << ", "
        << skin.joint[1u] << ", "
        << skin.joint[2u] << ", "
        << skin.joint[3u] << "]";
}

template<typename Stream>
void WriteSkinWeights(Stream& out, const MeshSkinInfluence& skin){
    out << "[";
    WriteFloat(out, skin.weight[0u]);
    out << ", ";
    WriteFloat(out, skin.weight[1u]);
    out << ", ";
    WriteFloat(out, skin.weight[2u]);
    out << ", ";
    WriteFloat(out, skin.weight[3u]);
    out << "]";
}

template<typename Stream>
void WriteJointMatrix(Stream& out, const JointMatrix& matrix){
    out << "[\n";
    for(const Vec4& row : matrix.rows){
        out << "        ";
        WriteVec4(out, row);
        out << ",\n";
    }
    out << "    ]";
}

bool ValidateStreamIndex(const u32 index, const usize count, const char* fieldName, const AStringView context){
    if(index < count)
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("{}: vertex_ref {} index is out of range"), StringConvert(context), StringConvert(fieldName));
    return false;
}

bool ValidateMeshGeometry(const SourceMeshStreams& mesh, const AStringView context){
    if(mesh.positions.empty() || mesh.normals.empty() || mesh.uv0.empty() || mesh.colors.empty() || mesh.vertexRefs.empty() || mesh.indices.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: mesh payload is incomplete"), StringConvert(context));
        return false;
    }
    if(mesh.tangents.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: mesh tangent stream is required"), StringConvert(context));
        return false;
    }
    if((mesh.indices.size() % 3u) != 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: mesh index stream must contain whole triangles"), StringConvert(context));
        return false;
    }

    for(const SourceVertexRef& ref : mesh.vertexRefs){
        if(!ValidateStreamIndex(ref.position, mesh.positions.size(), "position", context))
            return false;
        if(!ValidateStreamIndex(ref.normal, mesh.normals.size(), "normal", context))
            return false;
        if(ref.tangent == s_MissingSourceStreamIndex){
            NWB_LOGGER_ERROR(NWB_TEXT("{}: mesh vertex_ref tangent is missing"), StringConvert(context));
            return false;
        }
        if(!ValidateStreamIndex(ref.tangent, mesh.tangents.size(), "tangent", context))
            return false;
        if(!ValidateStreamIndex(ref.uv0, mesh.uv0.size(), "uv0", context))
            return false;
        if(!ValidateStreamIndex(ref.color, mesh.colors.size(), "color", context))
            return false;
    }

    for(const u32 index : mesh.indices){
        if(index >= mesh.vertexRefs.size()){
            NWB_LOGGER_ERROR(NWB_TEXT("{}: mesh triangle index references an out-of-range vertex_ref"), StringConvert(context));
            return false;
        }
    }
    return true;
}

bool ValidatePlainMeshAsset(const SourceMeshStreams& mesh){
    static constexpr AStringView s_Context = "Failed to write NWB mesh";
    if(!ValidateMeshGeometry(mesh, s_Context))
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

bool ValidateSkinnedModelSourceMesh(const SourceMeshStreams& mesh){
    static constexpr AStringView s_Context = "Failed to write NWB model";
    if(!ValidateMeshGeometry(mesh, s_Context))
        return false;
    if(mesh.skin.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB model: skinned model source mesh requires skin influences"));
        return false;
    }
    for(const SourceVertexRef& ref : mesh.vertexRefs){
        if(!ValidateStreamIndex(ref.skin, mesh.skin.size(), "skin", s_Context))
            return false;
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

bool ValidateSplitSkinSource(
    const SourceMeshStreams& mesh,
    const UtilityVector<ufbx_node*>& skeletonJoints,
    const UtilityVector<JointMatrix>& skeletonBindPoseMatrices,
    const UtilityVector<JointMatrix>& inverseBindMatrices
){
    if(mesh.skin.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB model: skinned model requires source skin influences"));
        return false;
    }
    if(skeletonJoints.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB model: skinned model requires skeleton joints"));
        return false;
    }
    if(skeletonBindPoseMatrices.size() != skeletonJoints.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB model: skeleton bind-pose matrix count must match joint count"));
        return false;
    }
    if(inverseBindMatrices.size() != skeletonJoints.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB model: skin inverse bind matrix count must match joint count"));
        return false;
    }
    return true;
}

u64 PositionSkinKey(const u32 position, const u32 skin){
    return (static_cast<u64>(position) << 32u) | static_cast<u64>(skin);
}

bool BuildPositionAlignedSkinnedMesh(
    const SourceMeshStreams& sourceMesh,
    SourceMeshStreams& outMesh,
    UtilityVector<MeshSkinInfluence>& outPositionSkin
){
    outMesh = SourceMeshStreams{};
    outPositionSkin.clear();

    if(!ValidateSkinnedModelSourceMesh(sourceMesh))
        return false;

    outMesh.normals = sourceMesh.normals;
    outMesh.tangents = sourceMesh.tangents;
    outMesh.uv0 = sourceMesh.uv0;
    outMesh.colors = sourceMesh.colors;
    outMesh.indices = sourceMesh.indices;
    outMesh.vertexRefs.reserve(sourceMesh.vertexRefs.size());
    outMesh.positions.reserve(sourceMesh.vertexRefs.size());
    outPositionSkin.reserve(sourceMesh.vertexRefs.size());

    HashMap<u64, u32> positionSkinLookup;
    positionSkinLookup.reserve(sourceMesh.vertexRefs.size());
    for(const SourceVertexRef& sourceRef : sourceMesh.vertexRefs){
        const u64 key = PositionSkinKey(sourceRef.position, sourceRef.skin);
        u32 positionIndex = 0u;
        const auto foundPosition = positionSkinLookup.find(key);
        if(foundPosition != positionSkinLookup.end()){
            positionIndex = foundPosition.value();
        }
        else{
            if(outMesh.positions.size() > static_cast<usize>(Limit<u32>::s_Max)){
                NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB model: split skinned mesh has too many positions"));
                return false;
            }

            positionIndex = static_cast<u32>(outMesh.positions.size());
            outMesh.positions.push_back(sourceMesh.positions[sourceRef.position]);
            outPositionSkin.push_back(sourceMesh.skin[sourceRef.skin]);
            positionSkinLookup.emplace(key, positionIndex);
        }

        SourceVertexRef ref = sourceRef;
        ref.position = positionIndex;
        ref.skin = s_MissingSourceStreamIndex;
        outMesh.vertexRefs.push_back(ref);
    }

    return true;
}

bool WriteMeshAsset(const Path& outputPath, const SourceMeshStreams& mesh){
    if(!ValidatePlainMeshAsset(mesh))
        return false;
    if(!EnsureOutputDirectory(outputPath, "mesh"))
        return false;

    BasicOutputFileStream<char> file(outputPath, s_FileOpenBinary | s_FileOpenTruncate);
    if(!file){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB mesh: failed to open output file '{}'"), PathToString<tchar>(outputPath));
        return false;
    }
    file.precision(9);

    file << "mesh asset;\n\n";

    file << "asset.positions = [\n";
    for(const Vec3& position : mesh.positions){
        file << "    ";
        WriteVec3(file, position);
        file << ",\n";
    }
    file << "];\n\n";

    file << "asset.normals = [\n";
    for(const Vec3& normal : mesh.normals){
        file << "    ";
        WriteVec3(file, normal);
        file << ",\n";
    }
    file << "];\n\n";

    file << "asset.tangents = [\n";
    for(const Vec4& tangent : mesh.tangents){
        file << "    ";
        WriteVec4(file, tangent);
        file << ",\n";
    }
    file << "];\n\n";

    file << "asset.uv0 = [\n";
    for(const Vec2& uv0 : mesh.uv0){
        file << "    ";
        WriteVec2(file, uv0);
        file << ",\n";
    }
    file << "];\n\n";

    file << "asset.colors = [\n";
    for(const Vec4& color : mesh.colors){
        file << "    ";
        WriteVec4(file, color);
        file << ",\n";
    }
    file << "];\n\n";

    file << "asset.vertex_refs = [\n";
    for(const SourceVertexRef& ref : mesh.vertexRefs){
        file << "    ";
        WriteVertexRef(file, ref);
        file << ",\n";
    }
    file << "];\n\n";

    file << "asset.indices = [\n";
    for(usize i = 0u; i < mesh.indices.size(); i += 3u)
        file << "    [" << mesh.indices[i + 0u] << ", " << mesh.indices[i + 1u] << ", " << mesh.indices[i + 2u] << "],\n";
    file << "];\n";

    if(!file){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB mesh: failed while writing output file '{}'"), PathToString<tchar>(outputPath));
        return false;
    }
    return true;
}

AString EscapeMetadataString(AStringView text){
    AString escaped;
    escaped.reserve(text.size());
    for(const char c : text){
        if(c == '\\' || c == '"')
            escaped.push_back('\\');
        escaped.push_back(c);
    }
    return escaped;
}

AString NodeName(const ufbx_node* node, const usize fallbackIndex){
    AString name;
    if(node && node->name.data && node->name.length != 0u)
        name.assign(node->name.data, node->name.length);
    if(!name.empty())
        return name;

    AStringStream out;
    out << "joint_" << fallbackIndex;
    return out.str();
}

AString BuildVirtualBasePath(const Path& outputPath, AString virtualRoot){
    virtualRoot = Trim(Move(virtualRoot));
    if(virtualRoot.empty())
        virtualRoot = "project";

    Path noExtension = outputPath;
    noExtension.replace_extension();

    Path relativePath;
    bool foundAssets = false;
    for(const Path& part : noExtension){
        const AString partText = ToLower(PathToUtf8(part));
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

    AString relativeText = PathToUtf8(relativePath);
    while(!relativeText.empty() && (relativeText.front() == '/' || relativeText.front() == '\\'))
        relativeText.erase(relativeText.begin());
    virtualPath += relativeText;
    return virtualPath;
}

bool WriteSkeletonAsset(
    const Path& outputPath,
    const UtilityVector<ufbx_node*>& joints,
    const UtilityVector<JointMatrix>& bindPoseMatrices
){
    if(!EnsureOutputDirectory(outputPath, "skeleton"))
        return false;

    BasicOutputFileStream<char> file(outputPath, s_FileOpenBinary | s_FileOpenTruncate);
    if(!file){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB skeleton: failed to open output file '{}'"), PathToString<tchar>(outputPath));
        return false;
    }
    file.precision(9);

    file << "skeleton asset;\n\n";
    file << "asset.joints = [\n";
    for(usize jointIndex = 0u; jointIndex < joints.size(); ++jointIndex){
        file << "    {\n";
        file << "        \"name\": \"" << EscapeMetadataString(NodeName(joints[jointIndex], jointIndex)) << "\",\n";
        file << "        \"local_bind_pose\": ";
        WriteJointMatrix(file, bindPoseMatrices[jointIndex]);
        file << ",\n";
        file << "    },\n";
    }
    file << "];\n";

    if(!file){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB skeleton: failed while writing output file '{}'"), PathToString<tchar>(outputPath));
        return false;
    }
    return true;
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

    BasicOutputFileStream<char> file(outputPath, s_FileOpenBinary | s_FileOpenTruncate);
    if(!file){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB skin: failed to open output file '{}'"), PathToString<tchar>(outputPath));
        return false;
    }
    file.precision(9);

    file << "skin asset;\n\n";
    file << "asset.mesh = \"" << EscapeMetadataString(meshName) << "\";\n";
    file << "asset.skeleton = \"" << EscapeMetadataString(skeletonName) << "\";\n\n";

    file << "asset.influences = [\n";
    for(const MeshSkinInfluence& influence : influences){
        file << "    { \"joints\": ";
        WriteSkinJoints(file, influence);
        file << ", \"weights\": ";
        WriteSkinWeights(file, influence);
        file << " },\n";
    }
    file << "];\n\n";

    file << "asset.inverse_bind_matrices = [\n";
    for(const JointMatrix& matrix : inverseBindMatrices){
        file << "    ";
        WriteJointMatrix(file, matrix);
        file << ",\n";
    }
    file << "];\n";

    if(!file){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB skin: failed while writing output file '{}'"), PathToString<tchar>(outputPath));
        return false;
    }
    return true;
}

bool WriteModelAsset(
    const Path& outputPath,
    const AString& meshName,
    const AString* skinName,
    const AString* skeletonName
){
    if(!EnsureOutputDirectory(outputPath, "model"))
        return false;

    BasicOutputFileStream<char> file(outputPath, s_FileOpenBinary | s_FileOpenTruncate);
    if(!file){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB model: failed to open output file '{}'"), PathToString<tchar>(outputPath));
        return false;
    }
    file.precision(9);

    file << "model asset;\n\n";
    if(skeletonName){
        file << "asset.skeletons = [\n";
        file << "    { \"name\": \"skeleton\", \"skeleton\": \"" << EscapeMetadataString(*skeletonName) << "\" },\n";
        file << "];\n\n";

        file << "asset.skinned_meshes = [\n";
        file << "    { \"name\": \"mesh\", \"mesh\": \"" << EscapeMetadataString(meshName) << "\", \"skin\": \""
            << EscapeMetadataString(*skinName) << "\", \"skeleton\": \"skeleton\" },\n";
        file << "];\n";
    }
    else{
        file << "asset.static_meshes = [\n";
        file << "    { \"name\": \"mesh\", \"mesh\": \"" << EscapeMetadataString(meshName) << "\" },\n";
        file << "];\n";
    }

    if(!file){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB model: failed while writing output file '{}'"), PathToString<tchar>(outputPath));
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
    const UtilityVector<ufbx_node*>& skeletonJoints,
    const UtilityVector<JointMatrix>& skeletonBindPoseMatrices,
    const UtilityVector<JointMatrix>& inverseBindMatrices
){
    OutputAssetType::Enum assetType = OutputAssetType::Mesh;
    if(!ParseAssetTypeText(assetTypeText, assetType)){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB asset: {}"), StringConvert(OutputAssetTypeErrorText()));
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

    if(!skinnedModel){
        if(!__hidden_asset_writer::WriteMeshAsset(meshPath, mesh))
            return false;
        return __hidden_asset_writer::WriteModelAsset(outputPath, meshName, nullptr, nullptr);
    }

    if(!__hidden_asset_writer::ValidateSplitSkinSource(mesh, skeletonJoints, skeletonBindPoseMatrices, inverseBindMatrices))
        return false;

    SourceMeshStreams splitMesh;
    UtilityVector<MeshSkinInfluence> positionSkin;
    if(!__hidden_asset_writer::BuildPositionAlignedSkinnedMesh(mesh, splitMesh, positionSkin))
        return false;

    const Path skeletonPath = packageDirectory / "skeleton.nwb";
    const Path skinPath = packageDirectory / "skin.nwb";
    const AString skeletonName = virtualBase + "/skeleton";
    const AString skinName = virtualBase + "/skin";
    return __hidden_asset_writer::WriteMeshAsset(meshPath, splitMesh)
        && __hidden_asset_writer::WriteSkeletonAsset(skeletonPath, skeletonJoints, skeletonBindPoseMatrices)
        && __hidden_asset_writer::WriteSkinAsset(skinPath, meshName, skeletonName, positionSkin, inverseBindMatrices)
        && __hidden_asset_writer::WriteModelAsset(outputPath, meshName, &skinName, &skeletonName)
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

