// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "fbx_to_nwb.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


AString Trim(AString value){
    const AStringView trimmed = TrimView(AStringView(value));
    if(trimmed.size() == value.size())
        return value;

    const usize trimBegin = static_cast<usize>(trimmed.data() - value.data());
    value.erase(trimBegin + trimmed.size());
    value.erase(0u, trimBegin);
    return value;
}

AString UnquotePath(AString value){
    value = Trim(Move(value));
    if(value.size() >= 2u){
        const char first = value.front();
        const char last = value.back();
        if((first == '"' && last == '"') || (first == '\'' && last == '\'')){
            value.erase(value.size() - 1u);
            value.erase(0u, 1u);
        }
    }
    return value;
}

AString ToLower(AString value){
    Transform(value.begin(), value.end(), value.begin(), [](const char c){
        if(c >= 'A' && c <= 'Z')
            return static_cast<char>(c - 'A' + 'a');
        return c;
    });
    return value;
}

AString NormalizeAssetKind(AString value){
    return ToLower(Trim(Move(value)));
}

AStringView GeometryKindText(const GeometryKind::Enum value){
    switch(value){
    case GeometryKind::Static:
        return "static";
    case GeometryKind::StaticDeform:
        return "static_deform";
    case GeometryKind::Skinned:
        return "skinned";
    case GeometryKind::SkinnedDeform:
        return "skinned_deform";
    default:
        return "invalid";
    }
}

bool ParseNormalizedAssetKind(const AStringView value, GeometryKind::Enum& outKind){
    if(value == "static"){
        outKind = GeometryKind::Static;
        return true;
    }
    if(value == "static_deform"){
        outKind = GeometryKind::StaticDeform;
        return true;
    }
    if(value == "skinned"){
        outKind = GeometryKind::Skinned;
        return true;
    }
    if(value == "skinned_deform"){
        outKind = GeometryKind::SkinnedDeform;
        return true;
    }

    outKind = GeometryKind::Invalid;
    return false;
}

bool ParseAssetKind(const AString& value, GeometryKind::Enum& outKind){
    const AString normalized = NormalizeAssetKind(value);
    return ParseNormalizedAssetKind(normalized, outKind);
}

bool GeometryKindUsesDeformableRuntime(const GeometryKind::Enum value){
    return value == GeometryKind::StaticDeform || value == GeometryKind::Skinned || value == GeometryKind::SkinnedDeform;
}

bool GeometryKindUsesSkinning(const GeometryKind::Enum value){
    return value == GeometryKind::Skinned || value == GeometryKind::SkinnedDeform;
}

bool IsNormalizedDeformableGeometryKind(const AStringView value){
    GeometryKind::Enum kind = GeometryKind::Invalid;
    return ParseNormalizedAssetKind(value, kind) && GeometryKindUsesDeformableRuntime(kind);
}

bool IsNormalizedSkinnedGeometryKind(const AStringView value){
    GeometryKind::Enum kind = GeometryKind::Invalid;
    return ParseNormalizedAssetKind(value, kind) && GeometryKindUsesSkinning(kind);
}

bool IsDeformableGeometryKind(const AString& value){
    const AString normalized = NormalizeAssetKind(value);
    return IsNormalizedDeformableGeometryKind(normalized);
}

bool IsSkinnedGeometryKind(const AString& value){
    const AString normalized = NormalizeAssetKind(value);
    return IsNormalizedSkinnedGeometryKind(normalized);
}

bool ValidateAssetKind(AString& inOutValue, AString& outError){
    inOutValue = NormalizeAssetKind(Move(inOutValue));
    GeometryKind::Enum kind = GeometryKind::Invalid;
    if(ParseNormalizedAssetKind(inOutValue, kind))
        return true;

    outError = "NWB geometry type must be static, static_deform, skinned, or skinned_deform";
    return false;
}

bool ParseColorText(const AString& text, Vec4& outColor){
    AString normalized = text;
    Replace(normalized.begin(), normalized.end(), ',', ' ');
    AStringStream in(normalized);

    Vec4 color;
    if(!(in >> color.x >> color.y >> color.z >> color.w))
        return false;

    AString trailing;
    if(in >> trailing)
        return false;

    const f32 values[] = { color.x, color.y, color.z, color.w };
    for(const f32 value : values){
        if(!IsFinite(value))
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
    if(!IsFinite(lengthSquared) || lengthSquared <= 0.0)
        return false;

    const f64 invLength = 1.0 / Sqrt(lengthSquared);
    value.x = static_cast<f32>(static_cast<f64>(value.x) * invLength);
    value.y = static_cast<f32>(static_cast<f64>(value.y) * invLength);
    value.z = static_cast<f32>(static_cast<f64>(value.z) * invLength);
    return IsFinite(value.x) && IsFinite(value.y) && IsFinite(value.z);
}

Vec4 BuildFallbackTangent(const Vec3& normal){
    Vec3 reference = Abs(normal.x) < 0.9f
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
        if(!IsFinite(value))
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
    return AString(reinterpret_cast<const char*>(text.data()), text.size());
}

Path DefaultOutputPath(const AString& inputPath){
    Path outputPath = PathFromUtf8(inputPath);
    outputPath.replace_extension(".nwb");
    return outputPath;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

