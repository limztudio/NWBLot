// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "fbx_to_nwb.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <utility>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


AString Trim(AString value){
    const auto isSpace = [](const unsigned char c){ return std::isspace(c) != 0; };
    while(!value.empty() && isSpace(static_cast<unsigned char>(value.front()))){
        value.erase(value.begin());
    }
    while(!value.empty() && isSpace(static_cast<unsigned char>(value.back()))){
        value.pop_back();
    }
    return value;
}

AString UnquotePath(AString value){
    value = Trim(std::move(value));
    if(value.size() >= 2u){
        const char first = value.front();
        const char last = value.back();
        if((first == '"' && last == '"') || (first == '\'' && last == '\'')){
            value = value.substr(1u, value.size() - 2u);
        }
    }
    return value;
}

AString ToLower(AString value){
    std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char c){
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

AString NormalizeAssetKind(AString value){
    value = ToLower(Trim(std::move(value)));
    std::replace(value.begin(), value.end(), '-', '_');
    if(value == "static" || value == "static_geometry"){
        return "geometry";
    }
    if(value == "deformable" || value == "deformable_mesh"){
        return "deformable_geometry";
    }
    return value;
}

bool IsStaticGeometryKind(const AString& value){
    return NormalizeAssetKind(value) == "geometry";
}

bool IsDeformableGeometryKind(const AString& value){
    return NormalizeAssetKind(value) == "deformable_geometry";
}

bool ValidateAssetKind(AString& inOutValue, AString& outError){
    inOutValue = NormalizeAssetKind(std::move(inOutValue));
    if(IsStaticGeometryKind(inOutValue) || IsDeformableGeometryKind(inOutValue))
        return true;

    outError = "NWB geometry type must be geometry or deformable_geometry";
    return false;
}

bool ParseColorText(const AString& text, Vec4& outColor){
    AString normalized = text;
    std::replace(normalized.begin(), normalized.end(), ',', ' ');
    std::istringstream in(normalized);

    Vec4 color;
    if(!(in >> color.x >> color.y >> color.z >> color.w))
        return false;

    AString trailing;
    if(in >> trailing)
        return false;

    const f32 values[] = { color.x, color.y, color.z, color.w };
    for(const f32 value : values){
        if(!std::isfinite(value))
            return false;
    }

    outColor = color;
    return true;
}

bool Normalize(Vec3& value){
    const f64 lengthSquared =
        static_cast<f64>(value.x) * static_cast<f64>(value.x)
        + static_cast<f64>(value.y) * static_cast<f64>(value.y)
        + static_cast<f64>(value.z) * static_cast<f64>(value.z);
    if(!std::isfinite(lengthSquared) || lengthSquared <= 0.0)
        return false;

    const f64 invLength = 1.0 / std::sqrt(lengthSquared);
    value.x = static_cast<f32>(static_cast<f64>(value.x) * invLength);
    value.y = static_cast<f32>(static_cast<f64>(value.y) * invLength);
    value.z = static_cast<f32>(static_cast<f64>(value.z) * invLength);
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

Vec4 BuildFallbackTangent(const Vec3& normal){
    Vec3 reference = std::fabs(normal.x) < 0.9f
        ? Vec3{ 1.0f, 0.0f, 0.0f }
        : Vec3{ 0.0f, 1.0f, 0.0f };

    const f32 dot =
        reference.x * normal.x
        + reference.y * normal.y
        + reference.z * normal.z;
    Vec3 tangent{
        reference.x - normal.x * dot,
        reference.y - normal.y * dot,
        reference.z - normal.z * dot,
    };
    if(!Normalize(tangent))
        tangent = Vec3{ 1.0f, 0.0f, 0.0f };

    return Vec4{ tangent.x, tangent.y, tangent.z, 1.0f };
}

bool IsFiniteVertex(const GeometryVertex& vertex){
    const f32 values[] = {
        vertex.position.x,
        vertex.position.y,
        vertex.position.z,
        vertex.normal.x,
        vertex.normal.y,
        vertex.normal.z,
        vertex.uv0.x,
        vertex.uv0.y,
        vertex.color.x,
        vertex.color.y,
        vertex.color.z,
        vertex.color.w,
    };
    for(const f32 value : values){
        if(!std::isfinite(value))
            return false;
    }
    return true;
}

Path PathFromUtf8(const AString& value){
#if defined(NWB_PLATFORM_WINDOWS) && defined(NWB_UNICODE)
    return Path(StringConvert(value));
#else
    return Path(value);
#endif
}

AString PathToUtf8(const Path& path){
    const auto text = path.generic_u8string();
    AString output;
    output.reserve(text.size());
    for(const auto ch : text){
        output.push_back(static_cast<char>(ch));
    }
    return output;
}

Path DefaultOutputPath(const AString& inputPath){
    Path outputPath = PathFromUtf8(inputPath);
    outputPath.replace_extension(".nwb");
    return outputPath;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

