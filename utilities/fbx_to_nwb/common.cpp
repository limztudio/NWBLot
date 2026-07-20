// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "module.h"

#include <core/common/log.h>
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static AString NormalizeAssetTypeText(AString value){
    return ToAsciiLowerCopy(TrimCopy(Move(value)));
}

static AStringView OutputAssetTypeText(const OutputAssetType::Enum assetType){
    switch(assetType){
    case OutputAssetType::Bunch:
        return "bunch";
    case OutputAssetType::Mesh:
        return "mesh";
    case OutputAssetType::Model:
        return "model";
    case OutputAssetType::Skeleton:
        return "skeleton";
    case OutputAssetType::Skin:
        return "skin";
    default:
        return {};
    }
}

AString OutputAssetTypeOptionsText(){
    AString text;
    text += OutputAssetTypeText(OutputAssetType::Bunch);
    text += ", ";
    text += OutputAssetTypeText(OutputAssetType::Mesh);
    text += ", ";
    text += OutputAssetTypeText(OutputAssetType::Model);
    text += ", ";
    text += OutputAssetTypeText(OutputAssetType::Skeleton);
    text += ", or ";
    text += OutputAssetTypeText(OutputAssetType::Skin);
    return text;
}

AString OutputAssetTypeErrorText(){
    AString message = "Asset type must be ";
    message += OutputAssetTypeOptionsText();
    return message;
}

static bool ParseNormalizedAssetTypeText(const AStringView value, OutputAssetType::Enum& outAssetType){
    if(value == OutputAssetTypeText(OutputAssetType::Bunch) || value == "asset_bunch" || value == "asset-bunch"){
        outAssetType = OutputAssetType::Bunch;
        return true;
    }
    if(value == OutputAssetTypeText(OutputAssetType::Mesh)){
        outAssetType = OutputAssetType::Mesh;
        return true;
    }
    if(value == OutputAssetTypeText(OutputAssetType::Model)){
        outAssetType = OutputAssetType::Model;
        return true;
    }
    if(value == OutputAssetTypeText(OutputAssetType::Skeleton)){
        outAssetType = OutputAssetType::Skeleton;
        return true;
    }
    if(value == OutputAssetTypeText(OutputAssetType::Skin)){
        outAssetType = OutputAssetType::Skin;
        return true;
    }

    outAssetType = OutputAssetType::Bunch;
    return false;
}

bool ParseAssetTypeText(const AString& value, OutputAssetType::Enum& outAssetType){
    const AString normalized = NormalizeAssetTypeText(value);
    return ParseNormalizedAssetTypeText(normalized, outAssetType);
}

bool ValidateAssetTypeText(AString& inOutValue){
    inOutValue = NormalizeAssetTypeText(Move(inOutValue));
    OutputAssetType::Enum assetType = OutputAssetType::Mesh;
    if(ParseNormalizedAssetTypeText(inOutValue, assetType)){
        inOutValue = AString(OutputAssetTypeText(assetType));
        return true;
    }

    NWB_LOGGER_WARNING(StringConvert(OutputAssetTypeErrorText()));
    return false;
}

static AString NormalizeNormalModeText(AString value){
    return ToAsciiLowerCopy(TrimCopy(Move(value)));
}

static AStringView NormalModeText(const NormalMode::Enum normalMode){
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
    AString message = "normal mode must be ";
    message += NormalModeOptionsText();
    return message;
}

static bool ParseNormalizedNormalModeText(const AStringView value, NormalMode::Enum& outNormalMode){
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

bool ValidateNormalModeText(AString& inOutValue){
    inOutValue = NormalizeNormalModeText(Move(inOutValue));
    NormalMode::Enum normalMode = NormalMode::Imported;
    if(ParseNormalizedNormalModeText(inOutValue, normalMode))
        return true;

    NWB_LOGGER_WARNING(StringConvert(NormalModeErrorText()));
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

    f32 red = 0.0f;
    f32 green = 0.0f;
    f32 blue = 0.0f;
    f32 alpha = 0.0f;
    if(!(in >> red >> green >> blue >> alpha))
        return false;

    AString trailing;
    if(in >> trailing)
        return false;

    const SIMDVector color = VectorSet(red, green, blue, alpha);
    if(!VectorIsFinite(color, 0xFu))
        return false;

    StoreFloat(color, &outColor);
    return true;
}

bool Normalize(const SIMDVector value, SIMDVector& outValue){
    const SIMDVector lengthSquaredVector = Vector3LengthSq(value);
    if(!VectorIsFinite(lengthSquaredVector, 0xFu) || !Vector3Greater(lengthSquaredVector, VectorZero()))
        return false;

    const SIMDVector normalized = VectorDivide(value, VectorSqrt(lengthSquaredVector));
    NWB_ASSERT(Vector3IsFinite(normalized));
    outValue = normalized;
    return true;
}

Path PathFromUtf8(const AString& value){
#if defined(NWB_PLATFORM_WINDOWS) && defined(NWB_UNICODE)
    return Path(UtilityDetail::Arena(), StringConvert(UtilityDetail::Arena(), AStringView(value.data(), value.size())));
#else
    return Path(UtilityDetail::Arena(), value);
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

