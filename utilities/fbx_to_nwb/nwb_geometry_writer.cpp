// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "fbx_to_nwb.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_nwb_geometry_writer{


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
void WriteSkinJoints(Stream& out, const GeometrySkinInfluence& skin){
    out << "["
        << skin.joint[0u] << ", "
        << skin.joint[1u] << ", "
        << skin.joint[2u] << ", "
        << skin.joint[3u] << "]";
}

template<typename Stream>
void WriteSkinWeights(Stream& out, const GeometrySkinInfluence& skin){
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
void WriteJointMatrix(Stream& out, const GeometryJointMatrix& matrix){
    out << "[\n";
    for(const Vec4& column : matrix.columns){
        out << "        ";
        WriteVec4(out, column);
        out << ",\n";
    }
    out << "    ]";
}

bool ChooseIndexType(const AString& requested, const usize vertexCount, AString& outIndexType, AString& outError){
    const AString normalized = ToLower(Trim(requested));
    if(normalized == "auto"){
        outIndexType = vertexCount <= static_cast<usize>(Limit<u16>::s_Max)
            ? "u16"
            : "u32";
        return true;
    }
    if(normalized == "u16"){
        if(vertexCount > static_cast<usize>(Limit<u16>::s_Max)){
            outError = "u16 index type requested but geometry has more than 65535 vertices";
            return false;
        }
        outIndexType = "u16";
        return true;
    }
    if(normalized == "u32"){
        outIndexType = "u32";
        return true;
    }

    outError = "index type must be auto, u16, or u32";
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool WriteNwbGeometry(
    const Path& outputPath,
    const UtilityVector<GeometryVertex>& vertices,
    const UtilityVector<u32>& indices,
    const UtilityVector<GeometrySkinInfluence>& skin,
    const u32 skeletonJointCount,
    const UtilityVector<GeometryJointMatrix>& inverseBindMatrices,
    const AString& requestedIndexType,
    const AString& geometryClassText,
    AString& outIndexType,
    AString& outError
){
    outError.clear();
    if(vertices.empty() || indices.empty() || (indices.size() % 3u) != 0u){
        outError = "geometry payload is incomplete";
        return false;
    }
    if(!__hidden_nwb_geometry_writer::ChooseIndexType(requestedIndexType, vertices.size(), outIndexType, outError))
        return false;

    u32 geometryClass = 0u;
    if(!ParseGeometryClassText(geometryClassText, geometryClass)){
        outError = GeometryClassErrorText();
        return false;
    }
    const bool writeSkinnedGeometry = GeometryClassUsesSkinning(geometryClass);
    if(writeSkinnedGeometry){
        if(skin.size() != vertices.size()){
            outError = "skinned geometry skin stream must match vertex count";
            return false;
        }
        if(skeletonJointCount == 0u){
            outError = "skinned geometry requires at least one skeleton joint";
            return false;
        }
        if(inverseBindMatrices.size() != static_cast<usize>(skeletonJointCount)){
            outError = "skinned geometry inverse bind matrix count must match skeleton_joint_count";
            return false;
        }
    }
    else if(!skin.empty() || skeletonJointCount != 0u || !inverseBindMatrices.empty()){
        outError = "static geometry cannot write skeleton/skin payload";
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

    file << "geometry asset;\n\n";
    file << "asset.geometry_class = \"" << GeometryClassText(geometryClass) << "\";\n\n";
    file << "asset.index_type = \"" << outIndexType << "\";\n\n";

    file << "asset.positions = [\n";
    for(const GeometryVertex& vertex : vertices){
        file << "    ";
        __hidden_nwb_geometry_writer::WriteVec3(file, vertex.position);
        file << ",\n";
    }
    file << "];\n\n";

    file << "asset.normals = [\n";
    for(const GeometryVertex& vertex : vertices){
        file << "    ";
        __hidden_nwb_geometry_writer::WriteVec3(file, vertex.normal);
        file << ",\n";
    }
    file << "];\n\n";

    if(writeSkinnedGeometry){
        file << "asset.tangents = [\n";
        for(const GeometryVertex& vertex : vertices){
            file << "    ";
            __hidden_nwb_geometry_writer::WriteVec4(file, BuildFallbackTangent(vertex.normal));
            file << ",\n";
        }
        file << "];\n\n";

        file << "asset.uv0 = [\n";
        for(const GeometryVertex& vertex : vertices){
            file << "    ";
            __hidden_nwb_geometry_writer::WriteVec2(file, vertex.uv0);
            file << ",\n";
        }
        file << "];\n\n";
    }

    file << "asset.colors = [\n";
    for(const GeometryVertex& vertex : vertices){
        file << "    ";
        __hidden_nwb_geometry_writer::WriteVec4(file, vertex.color);
        file << ",\n";
    }
    file << "];\n\n";

    file << "asset.indices = [\n";
    for(usize i = 0u; i < indices.size(); i += 3u)
        file << "    [" << indices[i + 0u] << ", " << indices[i + 1u] << ", " << indices[i + 2u] << "],\n";
    file << "];\n";

    if(writeSkinnedGeometry){
        file << "\nasset.skeleton_joint_count = " << skeletonJointCount << ";\n\n";

        file << "asset.inverse_bind_matrices = [\n";
        for(const GeometryJointMatrix& matrix : inverseBindMatrices){
            file << "    ";
            __hidden_nwb_geometry_writer::WriteJointMatrix(file, matrix);
            file << ",\n";
        }
        file << "];\n\n";

        file << "asset.skin = {\n";
        file << "    \"joints0\": [\n";
        for(const GeometrySkinInfluence& influence : skin){
            file << "        ";
            __hidden_nwb_geometry_writer::WriteSkinJoints(file, influence);
            file << ",\n";
        }
        file << "    ],\n";
        file << "    \"weights0\": [\n";
        for(const GeometrySkinInfluence& influence : skin){
            file << "        ";
            __hidden_nwb_geometry_writer::WriteSkinWeights(file, influence);
            file << ",\n";
        }
        file << "    ],\n";
        file << "};\n\n";
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

