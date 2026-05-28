// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "module.h"

#include <core/mesh/classification.h>


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

AString NormalizeMeshClassText(AString value){
    return ToLower(Trim(Move(value)));
}

AStringView MeshClassText(const u32 meshClass){
    return NWB::Core::Mesh::MeshClassText(meshClass);
}

AString MeshClassOptionsText(){
    return AString("mesh or skinned_mesh");
}

AString MeshClassErrorText(){
    AString error = "Asset type must be ";
    error += MeshClassOptionsText();
    return error;
}

bool ParseNormalizedMeshClassText(const AStringView value, u32& outMeshClass){
    if(value == "mesh"){
        outMeshClass = NWB::Core::Mesh::MeshClass::Static;
        return true;
    }
    if(value == "skinned_mesh"){
        outMeshClass = NWB::Core::Mesh::MeshClass::Skinned;
        return true;
    }

    outMeshClass = NWB::Core::Mesh::MeshClass::Invalid;
    return false;
}

bool ParseMeshClassText(const AString& value, u32& outMeshClass){
    const AString normalized = NormalizeMeshClassText(value);
    return ParseNormalizedMeshClassText(normalized, outMeshClass);
}

bool MeshClassUsesSkinning(const u32 meshClass){
    return NWB::Core::Mesh::MeshClassUsesSkinning(meshClass);
}

bool IsNormalizedSkinnedMeshClass(const AStringView value){
    u32 meshClass = NWB::Core::Mesh::MeshClass::Invalid;
    return ParseNormalizedMeshClassText(value, meshClass) && MeshClassUsesSkinning(meshClass);
}

bool IsSkinnedMeshClass(const AString& value){
    const AString normalized = NormalizeMeshClassText(value);
    return IsNormalizedSkinnedMeshClass(normalized);
}

bool ValidateMeshClassText(AString& inOutValue, AString& outError){
    inOutValue = NormalizeMeshClassText(Move(inOutValue));
    u32 meshClass = NWB::Core::Mesh::MeshClass::Invalid;
    if(ParseNormalizedMeshClassText(inOutValue, meshClass))
        return true;

    outError = MeshClassErrorText();
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

AStringView SourceTangentModeText(const SourceTangentMode::Enum mode){
    switch(mode){
    case SourceTangentMode::Imported:
        return "imported";
    case SourceTangentMode::GeneratedUv:
        return "generated_uv";
    case SourceTangentMode::GeneratedFallback:
        return "generated_fallback";
    default:
        return {};
    }
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
    const SIMDVector valueVector = LoadFloat(value);
    const SIMDVector lengthSquaredVector = Vector3LengthSq(valueVector);
    const f32 lengthSquared = VectorGetX(lengthSquaredVector);
    if(!IsFinite(lengthSquared) || lengthSquared <= 0.0f)
        return false;

    const SIMDVector normalizedVector = VectorMultiply(valueVector, VectorReciprocalSqrt(lengthSquaredVector));
    if(Vector3IsNaN(normalizedVector) || Vector3IsInfinite(normalizedVector))
        return false;

    StoreFloat(normalizedVector, &value);
    return true;
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

