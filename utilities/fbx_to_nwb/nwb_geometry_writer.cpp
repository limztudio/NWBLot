// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "fbx_to_nwb.h"

#include <cmath>
#include <fstream>
#include <ostream>
#include <system_error>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_nwb_geometry_writer{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


f32 CleanFloat(const f32 value){
    if(std::fabs(value) < 0.00000001f)
        return 0.0f;
    return value;
}

void WriteFloat(std::ostream& out, const f32 value){
    out << CleanFloat(value);
}

void WriteVec2(std::ostream& out, const Vec2& value){
    out << "[";
    WriteFloat(out, value.x);
    out << ", ";
    WriteFloat(out, value.y);
    out << "]";
}

void WriteVec3(std::ostream& out, const Vec3& value){
    out << "[";
    WriteFloat(out, value.x);
    out << ", ";
    WriteFloat(out, value.y);
    out << ", ";
    WriteFloat(out, value.z);
    out << "]";
}

void WriteVec4(std::ostream& out, const Vec4& value){
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
    const AString& requestedIndexType,
    const AString& assetKind,
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

    ErrorCode errorCode;
    const Path parentPath = outputPath.parent_path();
    if(!parentPath.empty()){
        if(!EnsureDirectories(parentPath, errorCode)){
            outError = "failed to create output directory: " + errorCode.message();
            return false;
        }
    }

    BasicOutputFileStream<char> file(outputPath, std::ios::binary | std::ios::trunc);
    if(!file){
        outError = "failed to open output file";
        return false;
    }
    file.precision(9);

    const bool writeDeformableGeometry = IsDeformableGeometryKind(assetKind);
    file << (writeDeformableGeometry ? "deformable_geometry asset;\n\n" : "geometry asset;\n\n");
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

    if(writeDeformableGeometry){
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
    for(usize i = 0u; i < indices.size(); i += 3u){
        file << "    [" << indices[i + 0u] << ", " << indices[i + 1u] << ", " << indices[i + 2u] << "],\n";
    }
    file << "];\n";

    if(writeDeformableGeometry){
        file << "\nasset.skin = {};\n";
        file << "asset.morphs = {};\n";
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

