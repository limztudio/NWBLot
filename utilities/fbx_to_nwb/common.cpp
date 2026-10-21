// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "module.h"

#include <global/allocation_size.h>
#include <global/command_line.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_common{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr OutputAssetType::Enum s_OutputAssetTypeValues[] = { OutputAssetType::Bunch, OutputAssetType::Mesh, OutputAssetType::Model, OutputAssetType::Skeleton, OutputAssetType::Skin, };
static constexpr NormalMode::Enum s_NormalModeValues[] = { NormalMode::Imported, NormalMode::Smooth, NormalMode::Regenerate, };
static constexpr AStringView s_OptionSeparatorText = ", ";
static constexpr AStringView s_OptionLastSeparatorText = ", or ";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename EnumT, usize Count, typename TextFunction>
static ScratchString BuildOptionTexts(Core::Alloc::ScratchArena& scratchArena, const EnumT (&values)[Count], TextFunction textFunction){
    usize capacity = 0u;
    for(usize i = 0u; i < Count; ++i){
        if(i > 0u)
            capacity = AddSize(capacity, i + 1u == Count ? s_OptionLastSeparatorText.size() : s_OptionSeparatorText.size());
        capacity = AddSize(capacity, textFunction(values[i]).size());
    }
    ScratchString text(scratchArena);
    text.reserve(capacity);
    for(usize i = 0u; i < Count; ++i){
        if(i > 0u)
            text += i + 1u == Count ? s_OptionLastSeparatorText : s_OptionSeparatorText;
        text += textFunction(values[i]);
    }
    return text;
}

template<typename TextFunction, typename ParseFunction>
static bool ValidateOptionText(AString& inOutValue, TextFunction textFunction, ParseFunction parseValue){
    inOutValue = NormalizeOptionText(Move(inOutValue));
    const auto parsed = parseValue(inOutValue);
    if(parsed){
        inOutValue.assign(textFunction(*parsed));
        return true;
    }

    return false;
}

static AStringView OutputAssetTypeText(const OutputAssetType::Enum assetType)noexcept{
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

static AStringView NormalModeText(const NormalMode::Enum normalMode)noexcept{
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

static Expected<OutputAssetType::Enum> ParseNormalizedAssetTypeText(const AStringView value)noexcept{
    return ::ParseNormalizedEnumText<OutputAssetType::Enum, AStringView (*)(OutputAssetType::Enum)noexcept>(value, OutputAssetTypeText, s_OutputAssetTypeValues, LengthOf(s_OutputAssetTypeValues));
}

static Expected<NormalMode::Enum> ParseNormalizedNormalModeText(const AStringView value)noexcept{
    return ::ParseNormalizedEnumText<NormalMode::Enum, AStringView (*)(NormalMode::Enum)noexcept>(value, NormalModeText, s_NormalModeValues, LengthOf(s_NormalModeValues));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ScratchString OutputAssetTypeOptionsText(Core::Alloc::ScratchArena& scratchArena){
    return __hidden_common::BuildOptionTexts(scratchArena, __hidden_common::s_OutputAssetTypeValues, __hidden_common::OutputAssetTypeText);
}

ScratchString OutputAssetTypeErrorText(Core::Alloc::ScratchArena& scratchArena){
    const ScratchString options = OutputAssetTypeOptionsText(scratchArena);
    ScratchString message("Asset type must be ", scratchArena);
    message.reserve(AddSize(message.size(), options.size()));
    message += options;
    return message;
}

Expected<OutputAssetType::Enum> ParseAssetTypeText(const AStringView value, Core::Alloc::ScratchArena& scratchArena){
    const ScratchString normalized = NormalizeOptionText(ScratchString(value, scratchArena));
    return __hidden_common::ParseNormalizedAssetTypeText(AStringView(normalized));
}

bool ValidateAssetTypeText(AString& inOutValue){
    return __hidden_common::ValidateOptionText(inOutValue, __hidden_common::OutputAssetTypeText, __hidden_common::ParseNormalizedAssetTypeText);
}

ScratchString NormalModeOptionsText(Core::Alloc::ScratchArena& scratchArena){
    return __hidden_common::BuildOptionTexts(scratchArena, __hidden_common::s_NormalModeValues, __hidden_common::NormalModeText);
}

ScratchString NormalModeErrorText(Core::Alloc::ScratchArena& scratchArena){
    const ScratchString options = NormalModeOptionsText(scratchArena);
    ScratchString message("normal mode must be ", scratchArena);
    message.reserve(AddSize(message.size(), options.size()));
    message += options;
    return message;
}

Expected<NormalMode::Enum> ParseNormalModeText(const AStringView value, Core::Alloc::ScratchArena& scratchArena){
    const ScratchString normalized = NormalizeOptionText(ScratchString(value, scratchArena));
    return __hidden_common::ParseNormalizedNormalModeText(AStringView(normalized));
}

bool ValidateNormalModeText(AString& inOutValue){
    return __hidden_common::ValidateOptionText(inOutValue, __hidden_common::NormalModeText, __hidden_common::ParseNormalizedNormalModeText);
}

AStringView SourceTangentModeText(const SourceTangentMode::Enum mode)noexcept{
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

Expected<Vec4> ParseColorText(const AStringView text, Core::Alloc::ScratchArena& scratchArena){
    ScratchString normalized(text, scratchArena);
    Replace(normalized.begin(), normalized.end(), ',', ' ');
    ScratchStringStream in(normalized, ScratchStringStream::in, scratchArena);

    f32 red = 0.0f;
    f32 green = 0.0f;
    f32 blue = 0.0f;
    f32 alpha = 0.0f;
    if(!(in >> red >> green >> blue >> alpha))
        return MakeUnexpected(Failure{});

    ScratchString trailing(scratchArena);
    if(in >> trailing)
        return MakeUnexpected(Failure{});

    const SIMDVectorSingle color = VectorSet(red, green, blue, alpha);
    if(!VectorIsFinite(color, VectorComponentMask::s_XYZW))
        return MakeUnexpected(Failure{});

    Vec4 result;
    StoreFloat(color, result);
    return result;
}

ScratchPath DefaultOutputPath(const AStringView inputPath, Core::Alloc::ScratchArena& scratchArena){
    ScratchPath outputPath(scratchArena, inputPath);
    outputPath.replaceExtension(s_NwbOutputExtension);
    return outputPath;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

