// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "fbx_to_nwb.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_nwb_mesh_writer{


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
void WriteJointMatrix(Stream& out, const MeshJointMatrix& matrix){
    out << "[\n";
    for(const Vec4& column : matrix.columns){
        out << "        ";
        WriteVec4(out, column);
        out << ",\n";
    }
    out << "    ]";
}

bool ValidateStreamIndex(const u32 index, const usize count, const char* fieldName, AString& outError){
    if(index < count)
        return true;

    outError = "vertex_ref ";
    outError += fieldName;
    outError += " index is out of range";
    return false;
}

bool ValidateSourceMesh(const SourceMeshStreams& mesh, const bool writeSkinnedMesh, AString& outError){
    if(mesh.positions.empty() || mesh.normals.empty() || mesh.uv0.empty() || mesh.colors.empty() || mesh.vertexRefs.empty() || mesh.indices.empty()){
        outError = "mesh payload is incomplete";
        return false;
    }
    if(mesh.tangents.empty()){
        outError = "mesh tangent stream is required";
        return false;
    }
    if((mesh.indices.size() % 3u) != 0u){
        outError = "mesh index stream must contain whole triangles";
        return false;
    }
    if(writeSkinnedMesh && mesh.skin.empty()){
        outError = "skinned mesh requires a skin stream";
        return false;
    }
    if(!writeSkinnedMesh && !mesh.skin.empty()){
        outError = "static mesh cannot write a skin stream";
        return false;
    }

    for(const SourceVertexRef& ref : mesh.vertexRefs){
        if(!ValidateStreamIndex(ref.position, mesh.positions.size(), "position", outError))
            return false;
        if(!ValidateStreamIndex(ref.normal, mesh.normals.size(), "normal", outError))
            return false;
        if(ref.tangent == s_MissingSourceStreamIndex){
            outError = "mesh vertex_ref tangent is missing";
            return false;
        }
        if(!ValidateStreamIndex(ref.tangent, mesh.tangents.size(), "tangent", outError))
            return false;
        if(!ValidateStreamIndex(ref.uv0, mesh.uv0.size(), "uv0", outError))
            return false;
        if(!ValidateStreamIndex(ref.color, mesh.colors.size(), "color", outError))
            return false;
        if(writeSkinnedMesh){
            if(!ValidateStreamIndex(ref.skin, mesh.skin.size(), "skin", outError))
                return false;
        }
        else if(ref.skin != s_MissingSourceStreamIndex){
            outError = "static mesh vertex_ref cannot contain a skin index";
            return false;
        }
    }

    for(const u32 index : mesh.indices){
        if(index >= mesh.vertexRefs.size()){
            outError = "mesh triangle index references an out-of-range vertex_ref";
            return false;
        }
    }
    return true;
}

template<typename Stream>
void WriteVertexRef(Stream& out, const SourceVertexRef& ref, const bool writeSkinnedMesh){
    out << "[" << ref.position << ", " << ref.normal << ", " << ref.tangent << ", " << ref.uv0 << ", " << ref.color;
    if(writeSkinnedMesh)
        out << ", " << ref.skin;
    out << "]";
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool WriteNwbMesh(
    const Path& outputPath,
    const SourceMeshStreams& mesh,
    const u32 skeletonJointCount,
    const UtilityVector<MeshJointMatrix>& inverseBindMatrices,
    const AString& meshClassText,
    AString& outError
){
    outError.clear();
    u32 meshClass = 0u;
    if(!ParseMeshClassText(meshClassText, meshClass)){
        outError = MeshClassErrorText();
        return false;
    }
    const bool writeSkinnedMesh = MeshClassUsesSkinning(meshClass);
    if(!__hidden_nwb_mesh_writer::ValidateSourceMesh(mesh, writeSkinnedMesh, outError))
        return false;
    if(writeSkinnedMesh){
        if(skeletonJointCount == 0u){
            outError = "skinned mesh requires at least one skeleton joint";
            return false;
        }
        if(inverseBindMatrices.size() != static_cast<usize>(skeletonJointCount)){
            outError = "skinned mesh inverse bind matrix count must match skeleton_joint_count";
            return false;
        }
    }
    else if(skeletonJointCount != 0u || !inverseBindMatrices.empty()){
        outError = "static mesh cannot write skeleton/skin payload";
        return false;
    }

    ErrorCode errorCode;
    const Path parentPath = outputPath.parent_path();
    if(!parentPath.empty()){
        if(!EnsureDirectories(parentPath, errorCode)){
            outError = "failed to create output directory: " + errorCode.message();
            return false;
        }
    }

    BasicOutputFileStream<char> file(outputPath, s_FileOpenBinary | s_FileOpenTruncate);
    if(!file){
        outError = "failed to open output file";
        return false;
    }
    file.precision(9);

    file << (writeSkinnedMesh ? "skinned_mesh asset;\n\n" : "mesh asset;\n\n");

    file << "asset.positions = [\n";
    for(const Vec3& position : mesh.positions){
        file << "    ";
        __hidden_nwb_mesh_writer::WriteVec3(file, position);
        file << ",\n";
    }
    file << "];\n\n";

    file << "asset.normals = [\n";
    for(const Vec3& normal : mesh.normals){
        file << "    ";
        __hidden_nwb_mesh_writer::WriteVec3(file, normal);
        file << ",\n";
    }
    file << "];\n\n";

    file << "asset.tangents = [\n";
    for(const Vec4& tangent : mesh.tangents){
        file << "    ";
        __hidden_nwb_mesh_writer::WriteVec4(file, tangent);
        file << ",\n";
    }
    file << "];\n\n";

    file << "asset.uv0 = [\n";
    for(const Vec2& uv0 : mesh.uv0){
        file << "    ";
        __hidden_nwb_mesh_writer::WriteVec2(file, uv0);
        file << ",\n";
    }
    file << "];\n\n";

    file << "asset.colors = [\n";
    for(const Vec4& color : mesh.colors){
        file << "    ";
        __hidden_nwb_mesh_writer::WriteVec4(file, color);
        file << ",\n";
    }
    file << "];\n\n";

    if(writeSkinnedMesh){
        file << "asset.skin = {\n";
        file << "    \"joints0\": [\n";
        for(const MeshSkinInfluence& influence : mesh.skin){
            file << "        ";
            __hidden_nwb_mesh_writer::WriteSkinJoints(file, influence);
            file << ",\n";
        }
        file << "    ],\n";
        file << "    \"weights0\": [\n";
        for(const MeshSkinInfluence& influence : mesh.skin){
            file << "        ";
            __hidden_nwb_mesh_writer::WriteSkinWeights(file, influence);
            file << ",\n";
        }
        file << "    ],\n";
        file << "};\n\n";
    }

    file << "asset.vertex_refs = [\n";
    for(const SourceVertexRef& ref : mesh.vertexRefs){
        file << "    ";
        __hidden_nwb_mesh_writer::WriteVertexRef(file, ref, writeSkinnedMesh);
        file << ",\n";
    }
    file << "];\n\n";

    file << "asset.indices = [\n";
    for(usize i = 0u; i < mesh.indices.size(); i += 3u)
        file << "    [" << mesh.indices[i + 0u] << ", " << mesh.indices[i + 1u] << ", " << mesh.indices[i + 2u] << "],\n";
    file << "];\n";

    if(writeSkinnedMesh){
        file << "\nasset.skeleton_joint_count = " << skeletonJointCount << ";\n\n";

        file << "asset.inverse_bind_matrices = [\n";
        for(const MeshJointMatrix& matrix : inverseBindMatrices){
            file << "    ";
            __hidden_nwb_mesh_writer::WriteJointMatrix(file, matrix);
            file << ",\n";
        }
        file << "];\n\n";
    }

    if(!file){
        outError = "failed while writing output file";
        return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

