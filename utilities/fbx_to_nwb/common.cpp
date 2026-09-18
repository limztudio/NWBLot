// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "module.h"

#include <core/common/log.h>
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static AString MakeOptionsErrorText(const AStringView prefix, const AString& options){
    AString message(prefix);
    message += options;
    return message;
}

template<typename EnumT, typename TextFunction>
static AString BuildOptionTexts(TextFunction textFunction, const EnumT* values, const usize valueCount){
    AString text;
    for(usize i = 0u; i < valueCount; ++i){
        if(i > 0u)
            text += (i + 1u == valueCount) ? ", or " : ", ";
        text += textFunction(values[i]);
    }
    return text;
}

template<typename EnumT, typename ParseFunction>
static bool ParseOptionText(const AString& value, EnumT& outValue, ParseFunction parseValue){
    const AString normalized = NormalizeOptionText(value);
    return parseValue(normalized, outValue);
}

template<typename EnumT, typename TextFunction, typename ParseFunction, typename ErrorFunction>
static bool ValidateOptionText(
    AString& inOutValue,
    TextFunction textFunction,
    ParseFunction parseValue,
    ErrorFunction errorText,
    const EnumT initial
){
    inOutValue = NormalizeOptionText(Move(inOutValue));
    EnumT parsed = initial;
    if(parseValue(inOutValue, parsed)){
        inOutValue = AString(textFunction(parsed));
        return true;
    }

    NWB_LOGGER_WARNING(StringConvert(errorText()));
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
    return BuildOptionTexts<OutputAssetType::Enum>(
        OutputAssetTypeText,
        s_OutputAssetTypeValues,
        LengthOf(s_OutputAssetTypeValues)
    );
}

AString OutputAssetTypeErrorText(){
    return MakeOptionsErrorText("Asset type must be ", OutputAssetTypeOptionsText());
}

template<typename EnumT>
static bool ParseNormalizedEnumText(
    const AStringView value,
    EnumT& outValue,
    AStringView (*textFunction)(EnumT),
    const EnumT* values,
    const usize valueCount,
    const EnumT fallback,
    const Core::Assets::NamedEnumCase<EnumT>* aliases = nullptr,
    const usize aliasCount = 0u
){
    for(usize i = 0u; i < valueCount; ++i){
        if(value == textFunction(values[i])){
            outValue = values[i];
            return true;
        }
    }
    if(Core::Assets::ParseNamedEnumText<EnumT>(value, outValue, nullptr, 0u, aliases, aliasCount))
        return true;

    outValue = fallback;
    return false;
}

static constexpr Core::Assets::NamedEnumCase<OutputAssetType::Enum> s_OutputAssetTypeAliases[] = {
    { s_AssetBunchAliasUnderscore, OutputAssetType::Bunch },
    { s_AssetBunchAliasDash, OutputAssetType::Bunch },
};

static bool ParseNormalizedAssetTypeText(const AStringView value, OutputAssetType::Enum& outAssetType){
    return ParseNormalizedEnumText<OutputAssetType::Enum>(
        value,
        outAssetType,
        OutputAssetTypeText,
        s_OutputAssetTypeValues,
        LengthOf(s_OutputAssetTypeValues),
        OutputAssetType::Bunch,
        s_OutputAssetTypeAliases,
        LengthOf(s_OutputAssetTypeAliases)
    );
}

bool ParseAssetTypeText(const AString& value, OutputAssetType::Enum& outAssetType){
    return ParseOptionText<OutputAssetType::Enum>(value, outAssetType, ParseNormalizedAssetTypeText);
}

bool ValidateAssetTypeText(AString& inOutValue){
    return ValidateOptionText<OutputAssetType::Enum>(inOutValue, OutputAssetTypeText, ParseNormalizedAssetTypeText, OutputAssetTypeErrorText, OutputAssetType::Mesh);
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
    return BuildOptionTexts<NormalMode::Enum>(
        NormalModeText,
        s_NormalModeValues,
        LengthOf(s_NormalModeValues)
    );
}

AString NormalModeErrorText(){
    return MakeOptionsErrorText("normal mode must be ", NormalModeOptionsText());
}

static bool ParseNormalizedNormalModeText(const AStringView value, NormalMode::Enum& outNormalMode){
    return ParseNormalizedEnumText<NormalMode::Enum>(
        value,
        outNormalMode,
        NormalModeText,
        s_NormalModeValues,
        LengthOf(s_NormalModeValues),
        NormalMode::Imported
    );
}

bool ParseNormalModeText(const AString& value, NormalMode::Enum& outNormalMode){
    return ParseOptionText<NormalMode::Enum>(value, outNormalMode, ParseNormalizedNormalModeText);
}

bool ValidateNormalModeText(AString& inOutValue){
    return ValidateOptionText<NormalMode::Enum>(inOutValue, NormalModeText, ParseNormalizedNormalModeText, NormalModeErrorText, NormalMode::Imported);
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
    if(!VectorIsFinite(color, VectorComponentMask::s_XYZW))
        return false;

    StoreFloat(color, outColor);
    return true;
}

Path DefaultOutputPath(const AString& inputPath){
    Path outputPath(UtilityDetail::Arena(), inputPath);
    outputPath.replace_extension(s_NwbOutputExtension);
    return outputPath;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

