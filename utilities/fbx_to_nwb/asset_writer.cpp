// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "module.h"
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

static constexpr f32 s_InvertibleJointDeterminantEpsilon = 0.000000000001f;
static constexpr usize s_PositionSkinKeySkinShiftBits = 32u;

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

bool InvertJointMatrix(const SIMDMatrix& matrix, SIMDMatrix& outInverse){
    SIMDVector determinant;
    outInverse = MatrixInverse(&determinant, matrix);

    return VectorIsFinite(determinant, VectorComponentMask::s_XYZW)
        && Vector4Greater(VectorAbs(determinant), VectorReplicate(s_InvertibleJointDeterminantEpsilon))
        && !MatrixIsNaN(outInverse)
        && !MatrixIsInfinite(outInverse)
    ;
}

bool BuildLocalBindPoseMatrix(
    const SIMDMatrix& globalBindPose,
    const SIMDMatrix* parentGlobalBindPose,
    SIMDMatrix& outLocalBindPose
){
    if(!parentGlobalBindPose){
        outLocalBindPose = globalBindPose;
        return true;
    }

    SIMDMatrix parentInverse;
    if(!InvertJointMatrix(*parentGlobalBindPose, parentInverse))
        return false;

    outLocalBindPose = MatrixMultiply(parentInverse, globalBindPose);
    if(MatrixIsNaN(outLocalBindPose) || MatrixIsInfinite(outLocalBindPose))
        return false;

    return true;
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
    if((mesh.indices.size() % s_TriangleIndexCount) != 0u){
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

struct SkeletonOutputData{
    UtilityVector<ufbx_node*> joints;
    UtilityVector<JointMatrix> bindPoseMatrices;
    UtilityVector<JointMatrix> inverseBindMatrices;
    UtilityVector<u16> oldToNewJointIndices;
};

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

bool BuildSkeletonOutputData(
    const UtilityVector<ufbx_node*>& joints,
    const UtilityVector<JointMatrix>& bindPoseMatrices,
    const UtilityVector<JointMatrix>& inverseBindMatrices,
    SkeletonOutputData& outData
){
    outData = {};

    if(joints.size() != bindPoseMatrices.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB skeleton: joint count must match bind-pose matrix count"));
        return false;
    }
    if(!inverseBindMatrices.empty() && inverseBindMatrices.size() != joints.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB skeleton: joint count must match inverse bind matrix count"));
        return false;
    }
    if(joints.size() > s_MaxSkeletonJointCount){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB skeleton: skeleton has more than {} joints"), s_MaxSkeletonJointCount);
        return false;
    }

    HashMap<const ufbx_node*, usize> sourceJointLookup;
    sourceJointLookup.reserve(joints.size());
    UtilityVector<AString> sortNames;
    sortNames.reserve(joints.size());
    for(usize jointIndex = 0u; jointIndex < joints.size(); ++jointIndex){
        ufbx_node* joint = joints[jointIndex];
        if(!joint){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB skeleton: skeleton joint {} is null"), jointIndex);
            return false;
        }
        if(sourceJointLookup.find(joint) != sourceJointLookup.end()){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB skeleton: skeleton contains duplicate joint node at index {}"), jointIndex);
            return false;
        }
        sourceJointLookup.emplace(joint, jointIndex);
        sortNames.push_back(NodeName(joint, jointIndex));
    }

    UtilityVector<u32> parentIndices;
    parentIndices.resize(joints.size(), s_MissingSourceStreamIndex);
    for(usize jointIndex = 0u; jointIndex < joints.size(); ++jointIndex){
        const ufbx_node* parent = joints[jointIndex]->parent;
        if(!parent)
            continue;

        const auto foundParent = sourceJointLookup.find(parent);
        if(foundParent != sourceJointLookup.end())
            parentIndices[jointIndex] = static_cast<u32>(foundParent.value());
    }

    UtilityVector<u32> parentDepths;
    parentDepths.resize(joints.size(), 0u);
    for(usize jointIndex = 0u; jointIndex < joints.size(); ++jointIndex){
        u32 depth = 0u;
        u32 parentIndex = parentIndices[jointIndex];
        for(usize guard = 0u; parentIndex != s_MissingSourceStreamIndex; ++guard){
            if(guard >= joints.size()){
                NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB skeleton: skeleton hierarchy contains a cycle"));
                return false;
            }
            ++depth;
            parentIndex = parentIndices[parentIndex];
        }
        parentDepths[jointIndex] = depth;
    }

    UtilityVector<usize> sortedJointIndices;
    sortedJointIndices.reserve(joints.size());
    for(usize jointIndex = 0u; jointIndex < joints.size(); ++jointIndex)
        sortedJointIndices.push_back(jointIndex);
    Sort(
        sortedJointIndices.begin(),
        sortedJointIndices.end(),
        [&parentDepths, &sortNames](const usize lhs, const usize rhs){
            if(parentDepths[lhs] != parentDepths[rhs])
                return parentDepths[lhs] < parentDepths[rhs];
            if(sortNames[lhs] != sortNames[rhs])
                return sortNames[lhs] < sortNames[rhs];
            return lhs < rhs;
        }
    );

    outData.oldToNewJointIndices.resize(joints.size(), 0u);
    outData.joints.reserve(joints.size());
    outData.bindPoseMatrices.reserve(bindPoseMatrices.size());
    outData.inverseBindMatrices.reserve(inverseBindMatrices.size());

    for(const usize oldJointIndex : sortedJointIndices){
        const u32 parentIndex = parentIndices[oldJointIndex];
        const JointMatrix* parentGlobalBindPose = parentIndex != s_MissingSourceStreamIndex
            ? &bindPoseMatrices[parentIndex]
            : nullptr
        ;
        SIMDMatrix globalBindPoseMatrix{};
        globalBindPoseMatrix.v[0u] = LoadFloat(bindPoseMatrices[oldJointIndex].rows[0u]);
        globalBindPoseMatrix.v[1u] = LoadFloat(bindPoseMatrices[oldJointIndex].rows[1u]);
        globalBindPoseMatrix.v[2u] = LoadFloat(bindPoseMatrices[oldJointIndex].rows[2u]);
        globalBindPoseMatrix.v[3u] = s_SIMDIdentityR3;

        SIMDMatrix parentGlobalBindPoseMatrix{};
        const SIMDMatrix* parentGlobalBindPoseMatrixPtr = nullptr;
        if(parentGlobalBindPose){
            parentGlobalBindPoseMatrix.v[0u] = LoadFloat(parentGlobalBindPose->rows[0u]);
            parentGlobalBindPoseMatrix.v[1u] = LoadFloat(parentGlobalBindPose->rows[1u]);
            parentGlobalBindPoseMatrix.v[2u] = LoadFloat(parentGlobalBindPose->rows[2u]);
            parentGlobalBindPoseMatrix.v[3u] = s_SIMDIdentityR3;
            parentGlobalBindPoseMatrixPtr = &parentGlobalBindPoseMatrix;
        }

        SIMDMatrix localBindPoseMatrix;
        if(!BuildLocalBindPoseMatrix(globalBindPoseMatrix, parentGlobalBindPoseMatrixPtr, localBindPoseMatrix)){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB skeleton: failed to build local bind pose for joint '{}'"), StringConvert(sortNames[oldJointIndex]));
            return false;
        }
        JointMatrix localBindPose;
        StoreFloat(localBindPoseMatrix.v[0u], &localBindPose.rows[0u]);
        StoreFloat(localBindPoseMatrix.v[1u], &localBindPose.rows[1u]);
        StoreFloat(localBindPoseMatrix.v[2u], &localBindPose.rows[2u]);

        outData.oldToNewJointIndices[oldJointIndex] = static_cast<u16>(outData.joints.size());
        outData.joints.push_back(joints[oldJointIndex]);
        outData.bindPoseMatrices.push_back(localBindPose);
        if(!inverseBindMatrices.empty())
            outData.inverseBindMatrices.push_back(inverseBindMatrices[oldJointIndex]);
    }

    return true;
}

bool RemapSkinInfluences(
    UtilityVector<MeshSkinInfluence>& inOutInfluences,
    const UtilityVector<u16>& oldToNewJointIndices
){
    for(MeshSkinInfluence& influence : inOutInfluences){
        for(usize slot = 0u; slot < s_MeshSkinInfluenceCount; ++slot){
            const u16 oldJointIndex = influence.joint[slot];
            if(oldJointIndex >= oldToNewJointIndices.size()){
                NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB skin: skin influence references out-of-range joint {}"), oldJointIndex);
                return false;
            }
            influence.joint[slot] = oldToNewJointIndices[oldJointIndex];
        }
    }
    return true;
}

u64 PositionSkinKey(const u32 position, const u32 skin){
    return (static_cast<u64>(position) << s_PositionSkinKeySkinShiftBits) | static_cast<u64>(skin);
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

    BasicOutputFileStream<char> file(outputPath, s_FileOpenBinary | s_FileOpenTruncate);
    if(!file){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB mesh: failed to open output file '{}'"), PathToString<tchar>(outputPath));
        return false;
    }
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
    const AString baseName = NodeName(node, fallbackIndex);
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
        const AString partText = ToAsciiLowerCopy(PathToUtf8(part));
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

    BasicOutputFileStream<char> file(outputPath, s_FileOpenBinary | s_FileOpenTruncate);
    if(!file){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB skeleton: failed to open output file '{}'"), PathToString<tchar>(outputPath));
        return false;
    }
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

    BasicOutputFileStream<char> file(outputPath, s_FileOpenBinary | s_FileOpenTruncate);
    if(!file){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB skin: failed to open output file '{}'"), PathToString<tchar>(outputPath));
        return false;
    }
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

    BasicOutputFileStream<char> file(outputPath, s_FileOpenBinary | s_FileOpenTruncate);
    if(!file){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB model: failed to open output file '{}'"), PathToString<tchar>(outputPath));
        return false;
    }
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

    BasicOutputFileStream<char> file(outputPath, s_FileOpenBinary | s_FileOpenTruncate);
    if(!file){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to write NWB asset bunch: failed to open output file '{}'"), PathToString<tchar>(outputPath));
        return false;
    }
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

    if(!__hidden_asset_writer::ValidateSplitSkinSource(mesh, skeletonJoints, skeletonBindPoseMatrices, inverseBindMatrices))
        return false;

    __hidden_asset_writer::SkeletonOutputData skeletonOutput;
    if(!__hidden_asset_writer::BuildSkeletonOutputData(
        skeletonJoints,
        skeletonBindPoseMatrices,
        inverseBindMatrices,
        skeletonOutput
    ))
        return false;

    SourceMeshStreams splitMesh;
    UtilityVector<MeshSkinInfluence> positionSkin;
    if(!__hidden_asset_writer::BuildPositionAlignedSkinnedMesh(mesh, splitMesh, positionSkin))
        return false;
    if(!__hidden_asset_writer::RemapSkinInfluences(positionSkin, skeletonOutput.oldToNewJointIndices))
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

