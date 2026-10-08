// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "module.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename TextFunction, typename ParseFunction>
static bool ValidateOptionText(
    AString& inOutValue,
    TextFunction textFunction,
    ParseFunction parseValue
){
    inOutValue = NormalizeOptionText(Move(inOutValue));
    const auto parsed = parseValue(inOutValue);
    if(parsed){
        inOutValue = AString(textFunction(*parsed));
        return true;
    }

    return false;
}

static constexpr OutputAssetType::Enum s_OutputAssetTypeValues[] = {
    OutputAssetType::Bunch,
    OutputAssetType::Mesh,
    OutputAssetType::Model,
    OutputAssetType::Skeleton,
    OutputAssetType::Skin,
};

static constexpr NormalMode::Enum s_NormalModeValues[] = {
    NormalMode::Imported,
    NormalMode::Smooth,
    NormalMode::Regenerate,
};

static AStringView OutputAssetTypeText(const OutputAssetType::Enum assetType){
    switch(assetType){
    case OutputAssetType::Bunch:
        return s_DefaultOutputAssetTypeText;
    case OutputAssetType::Mesh:
        return s_MeshAssetTypeText;
    case OutputAssetType::Model:
        return s_ModelAssetTypeText;
    case OutputAssetType::Skeleton:
        return s_SkeletonAssetTypeText;
    case OutputAssetType::Skin:
        return s_SkinAssetTypeText;
    default:
        return {};
    }
}

AString OutputAssetTypeOptionsText(){
    return BuildEnumOptionTexts<AString, OutputAssetType::Enum>(
        OutputAssetTypeText,
        s_OutputAssetTypeValues,
        LengthOf(s_OutputAssetTypeValues)
    );
}

AString OutputAssetTypeErrorText(){
    return MakeOptionsErrorText<AString>("Asset type must be ", OutputAssetTypeOptionsText());
}

static Expected<OutputAssetType::Enum> ParseNormalizedAssetTypeText(const AStringView value){
    return ::ParseNormalizedEnumText<OutputAssetType::Enum, AStringView (*)(OutputAssetType::Enum)>(
        value,
        OutputAssetTypeText,
        s_OutputAssetTypeValues,
        LengthOf(s_OutputAssetTypeValues)
    );
}

Expected<OutputAssetType::Enum> ParseAssetTypeText(const AStringView value){
    const AString normalized = NormalizeOptionText(AString(value));
    return ParseNormalizedAssetTypeText(AStringView(normalized));
}

bool ValidateAssetTypeText(AString& inOutValue){
    return ValidateOptionText(inOutValue, OutputAssetTypeText, ParseNormalizedAssetTypeText);
}

static AStringView NormalModeText(const NormalMode::Enum normalMode){
    switch(normalMode){
    case NormalMode::Imported:
        return s_DefaultNormalModeText;
    case NormalMode::Smooth:
        return s_SmoothNormalModeText;
    case NormalMode::Regenerate:
        return s_RegenerateNormalModeText;
    default:
        return {};
    }
}

AString NormalModeOptionsText(){
    return BuildEnumOptionTexts<AString, NormalMode::Enum>(
        NormalModeText,
        s_NormalModeValues,
        LengthOf(s_NormalModeValues)
    );
}

AString NormalModeErrorText(){
    return MakeOptionsErrorText<AString>("normal mode must be ", NormalModeOptionsText());
}

static Expected<NormalMode::Enum> ParseNormalizedNormalModeText(const AStringView value){
    return ::ParseNormalizedEnumText<NormalMode::Enum, AStringView (*)(NormalMode::Enum)>(
        value,
        NormalModeText,
        s_NormalModeValues,
        LengthOf(s_NormalModeValues)
    );
}

Expected<NormalMode::Enum> ParseNormalModeText(const AStringView value){
    const AString normalized = NormalizeOptionText(AString(value));
    return ParseNormalizedNormalModeText(AStringView(normalized));
}

bool ValidateNormalModeText(AString& inOutValue){
    return ValidateOptionText(inOutValue, NormalModeText, ParseNormalizedNormalModeText);
}

AStringView SourceTangentModeText(const SourceTangentMode::Enum mode){
    switch(mode){
    case SourceTangentMode::Imported:
        return s_DefaultNormalModeText;
    case SourceTangentMode::GeneratedUv:
        return s_GeneratedUvTangentModeText;
    case SourceTangentMode::GeneratedFallback:
        return s_GeneratedFallbackTangentModeText;
    default:
        return {};
    }
}

Expected<Vec4> ParseColorText(const AStringView text){
    AString normalized(text);
    Replace(normalized.begin(), normalized.end(), ',', ' ');
    AStringStream in(normalized);

    f32 red = 0.0f;
    f32 green = 0.0f;
    f32 blue = 0.0f;
    f32 alpha = 0.0f;
    if(!(in >> red >> green >> blue >> alpha))
        return MakeUnexpected(Failure{});

    AString trailing;
    if(in >> trailing)
        return MakeUnexpected(Failure{});

    const SIMDVector color = VectorSet(red, green, blue, alpha);
    if(!VectorIsFinite(color, VectorComponentMask::s_XYZW))
        return MakeUnexpected(Failure{});

    Vec4 result;
    StoreFloat(color, result);
    return result;
}

Path DefaultOutputPath(const AStringView inputPath){
    Path outputPath(UtilityDetail::Arena(), inputPath);
    outputPath.replaceExtension(s_NwbOutputExtension);
    return outputPath;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

