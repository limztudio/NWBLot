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

bool ValidateStreamIndex(const u32 index, const usize count, const char* fieldName, AString& outError){
    if(index < count)
        return true;

    outError = "vertex_ref ";
    outError += fieldName;
    outError += " index is out of range";
    return false;
}

bool ValidateSourceGeometry(const SourceGeometryStreams& geometry, const bool writeSkinnedGeometry, AString& outError){
    if(geometry.positions.empty() || geometry.normals.empty() || geometry.uv0.empty() || geometry.colors.empty() || geometry.vertexRefs.empty() || geometry.indices.empty()){
        outError = "geometry payload is incomplete";
        return false;
    }
    if((geometry.indices.size() % 3u) != 0u){
        outError = "geometry index stream must contain whole triangles";
        return false;
    }
    if(writeSkinnedGeometry && geometry.skin.empty()){
        outError = "skinned geometry requires a skin stream";
        return false;
    }
    if(!writeSkinnedGeometry && !geometry.skin.empty()){
        outError = "static geometry cannot write a skin stream";
        return false;
    }

    for(const SourceVertexRef& ref : geometry.vertexRefs){
        if(!ValidateStreamIndex(ref.position, geometry.positions.size(), "position", outError))
            return false;
        if(!ValidateStreamIndex(ref.normal, geometry.normals.size(), "normal", outError))
            return false;
        if(!geometry.tangents.empty()){
            if(!ValidateStreamIndex(ref.tangent, geometry.tangents.size(), "tangent", outError))
                return false;
        }
        else if(ref.tangent != s_MissingSourceStreamIndex){
            outError = "geometry vertex_ref tangent must be UINT32_MAX when tangents are omitted";
            return false;
        }
        if(!ValidateStreamIndex(ref.uv0, geometry.uv0.size(), "uv0", outError))
            return false;
        if(!ValidateStreamIndex(ref.color, geometry.colors.size(), "color", outError))
            return false;
        if(writeSkinnedGeometry){
            if(!ValidateStreamIndex(ref.skin, geometry.skin.size(), "skin", outError))
                return false;
        }
        else if(ref.skin != s_MissingSourceStreamIndex){
            outError = "static geometry vertex_ref cannot contain a skin index";
            return false;
        }
    }

    for(const u32 index : geometry.indices){
        if(index >= geometry.vertexRefs.size()){
            outError = "geometry triangle index references an out-of-range vertex_ref";
            return false;
        }
    }
    return true;
}

template<typename Stream>
void WriteVertexRef(Stream& out, const SourceVertexRef& ref, const bool writeSkinnedGeometry){
    out << "[" << ref.position << ", " << ref.normal << ", " << ref.tangent << ", " << ref.uv0 << ", " << ref.color;
    if(writeSkinnedGeometry)
        out << ", " << ref.skin;
    out << "]";
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool WriteNwbGeometry(
    const Path& outputPath,
    const SourceGeometryStreams& geometry,
    const u32 skeletonJointCount,
    const UtilityVector<GeometryJointMatrix>& inverseBindMatrices,
    const AString& geometryClassText,
    AString& outError
){
    outError.clear();
    u32 geometryClass = 0u;
    if(!ParseGeometryClassText(geometryClassText, geometryClass)){
        outError = GeometryClassErrorText();
        return false;
    }
    const bool writeSkinnedGeometry = GeometryClassUsesSkinning(geometryClass);
    if(!__hidden_nwb_geometry_writer::ValidateSourceGeometry(geometry, writeSkinnedGeometry, outError))
        return false;
    if(writeSkinnedGeometry){
        if(skeletonJointCount == 0u){
            outError = "skinned geometry requires at least one skeleton joint";
            return false;
        }
        if(inverseBindMatrices.size() != static_cast<usize>(skeletonJointCount)){
            outError = "skinned geometry inverse bind matrix count must match skeleton_joint_count";
            return false;
        }
    }
    else if(skeletonJointCount != 0u || !inverseBindMatrices.empty()){
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

    file << (writeSkinnedGeometry ? "skinned_geometry asset;\n\n" : "geometry asset;\n\n");

    file << "asset.positions = [\n";
    for(const Vec3& position : geometry.positions){
        file << "    ";
        __hidden_nwb_geometry_writer::WriteVec3(file, position);
        file << ",\n";
    }
    file << "];\n\n";

    file << "asset.normals = [\n";
    for(const Vec3& normal : geometry.normals){
        file << "    ";
        __hidden_nwb_geometry_writer::WriteVec3(file, normal);
        file << ",\n";
    }
    file << "];\n\n";

    if(!geometry.tangents.empty()){
        file << "asset.tangents = [\n";
        for(const Vec4& tangent : geometry.tangents){
            file << "    ";
            __hidden_nwb_geometry_writer::WriteVec4(file, tangent);
            file << ",\n";
        }
        file << "];\n\n";
    }

    file << "asset.uv0 = [\n";
    for(const Vec2& uv0 : geometry.uv0){
        file << "    ";
        __hidden_nwb_geometry_writer::WriteVec2(file, uv0);
        file << ",\n";
    }
    file << "];\n\n";

    file << "asset.colors = [\n";
    for(const Vec4& color : geometry.colors){
        file << "    ";
        __hidden_nwb_geometry_writer::WriteVec4(file, color);
        file << ",\n";
    }
    file << "];\n\n";

    if(writeSkinnedGeometry){
        file << "asset.skin = {\n";
        file << "    \"joints0\": [\n";
        for(const GeometrySkinInfluence& influence : geometry.skin){
            file << "        ";
            __hidden_nwb_geometry_writer::WriteSkinJoints(file, influence);
            file << ",\n";
        }
        file << "    ],\n";
        file << "    \"weights0\": [\n";
        for(const GeometrySkinInfluence& influence : geometry.skin){
            file << "        ";
            __hidden_nwb_geometry_writer::WriteSkinWeights(file, influence);
            file << ",\n";
        }
        file << "    ],\n";
        file << "};\n\n";
    }

    file << "asset.vertex_refs = [\n";
    for(const SourceVertexRef& ref : geometry.vertexRefs){
        file << "    ";
        __hidden_nwb_geometry_writer::WriteVertexRef(file, ref, writeSkinnedGeometry);
        file << ",\n";
    }
    file << "];\n\n";

    file << "asset.indices = [\n";
    for(usize i = 0u; i < geometry.indices.size(); i += 3u)
        file << "    [" << geometry.indices[i + 0u] << ", " << geometry.indices[i + 1u] << ", " << geometry.indices[i + 2u] << "],\n";
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

