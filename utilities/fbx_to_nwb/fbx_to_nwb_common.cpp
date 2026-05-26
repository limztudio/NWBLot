// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "fbx_to_nwb.h"

#include <core/geometry/geometry_class.h>


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

AString NormalizeGeometryClassText(AString value){
    value = ToLower(Trim(Move(value)));
    if(value == "geometry")
        return AString("static");
    if(value == "skinned_geometry")
        return AString("skinned");
    return value;
}

AStringView GeometryClassText(const u32 geometryClass){
    return NWB::Core::Geometry::GeometryClassText(geometryClass);
}

AString GeometryClassOptionsText(){
    return AString("geometry/static or skinned_geometry/skinned");
}

AString GeometryClassErrorText(){
    AString error = "Asset type must be ";
    error += GeometryClassOptionsText();
    return error;
}

bool ParseNormalizedGeometryClassText(const AStringView value, u32& outGeometryClass){
    outGeometryClass = NWB::Core::Geometry::GeometryClass::Invalid;
    return NWB::Core::Geometry::ParseGeometryClassText(value, outGeometryClass);
}

bool ParseGeometryClassText(const AString& value, u32& outGeometryClass){
    const AString normalized = NormalizeGeometryClassText(value);
    return ParseNormalizedGeometryClassText(normalized, outGeometryClass);
}

bool GeometryClassUsesSkinning(const u32 geometryClass){
    return NWB::Core::Geometry::GeometryClassUsesSkinning(geometryClass);
}

bool IsNormalizedSkinnedGeometryClass(const AStringView value){
    u32 geometryClass = NWB::Core::Geometry::GeometryClass::Invalid;
    return ParseNormalizedGeometryClassText(value, geometryClass) && GeometryClassUsesSkinning(geometryClass);
}

bool IsSkinnedGeometryClass(const AString& value){
    const AString normalized = NormalizeGeometryClassText(value);
    return IsNormalizedSkinnedGeometryClass(normalized);
}

bool ValidateGeometryClassText(AString& inOutValue, AString& outError){
    inOutValue = NormalizeGeometryClassText(Move(inOutValue));
    u32 geometryClass = NWB::Core::Geometry::GeometryClass::Invalid;
    if(ParseNormalizedGeometryClassText(inOutValue, geometryClass))
        return true;

    outError = GeometryClassErrorText();
    return false;
}

static AString NormalizeNormalModeText(AString value){
    return ToLower(Trim(Move(value)));
}

AStringView NormalModeText(const NormalMode::Enum normalMode){
    switch(normalMode){
    case NormalMode::Imported:
        return "imported";
    case NormalMode::Smooth:
        return "smooth";
    case NormalMode::Regenerate:
        return "regenerate";
    default:
        return {};
    }
}

AString NormalModeOptionsText(){
    AString text;
    text += NormalModeText(NormalMode::Imported);
    text += ", ";
    text += NormalModeText(NormalMode::Smooth);
    text += ", or ";
    text += NormalModeText(NormalMode::Regenerate);
    return text;
}

AString NormalModeErrorText(){
    AString error = "normal mode must be ";
    error += NormalModeOptionsText();
    return error;
}

bool ParseNormalizedNormalModeText(const AStringView value, NormalMode::Enum& outNormalMode){
    if(value == NormalModeText(NormalMode::Imported)){
        outNormalMode = NormalMode::Imported;
        return true;
    }
    if(value == NormalModeText(NormalMode::Smooth)){
        outNormalMode = NormalMode::Smooth;
        return true;
    }
    if(value == NormalModeText(NormalMode::Regenerate)){
        outNormalMode = NormalMode::Regenerate;
        return true;
    }

    outNormalMode = NormalMode::Imported;
    return false;
}

bool ParseNormalModeText(const AString& value, NormalMode::Enum& outNormalMode){
    const AString normalized = NormalizeNormalModeText(value);
    return ParseNormalizedNormalModeText(normalized, outNormalMode);
}

bool ValidateNormalModeText(AString& inOutValue, AString& outError){
    inOutValue = NormalizeNormalModeText(Move(inOutValue));
    NormalMode::Enum normalMode = NormalMode::Imported;
    if(ParseNormalizedNormalModeText(inOutValue, normalMode))
        return true;

    outError = NormalModeErrorText();
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

Path PathFromUtf8(const AString& value){
#if defined(NWB_PLATFORM_WINDOWS) && defined(NWB_UNICODE)
    return Path(StringConvert(UtilityDetail::Arena(), AStringView(value.data(), value.size())));
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

