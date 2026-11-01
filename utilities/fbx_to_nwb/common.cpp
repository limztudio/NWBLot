// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "module.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_common{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Array<OutputAssetType::Enum, 5u> s_OutputAssetTypeValues = { OutputAssetType::Bunch, OutputAssetType::Mesh, OutputAssetType::Model, OutputAssetType::Skeleton, OutputAssetType::Skin };
inline constexpr Array<NormalMode::Enum, 3u> s_NormalModeValues = { NormalMode::Imported, NormalMode::Smooth, NormalMode::Regenerate };
inline constexpr AStringView s_OptionSeparatorText = ", ";
inline constexpr AStringView s_OptionLastSeparatorText = ", or ";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename EnumT, typename TextFunction>
[[nodiscard]] ScratchString BuildOptionTexts(const Span<const EnumT> values, TextFunction textFunction, Core::Alloc::ScratchArena& scratchArena){
    usize capacity = 0u;
    for(usize i = 0u; i < values.size(); ++i){
        if(i > 0u)
            capacity = AddSize(capacity, i + 1u == values.size() ? s_OptionLastSeparatorText.size() : s_OptionSeparatorText.size());
        capacity = AddSize(capacity, textFunction(values[i]).size());
    }
    ScratchString text(scratchArena);
    text.reserve(capacity);
    for(usize i = 0u; i < values.size(); ++i){
        if(i > 0u)
            text += i + 1u == values.size() ? s_OptionLastSeparatorText : s_OptionSeparatorText;
        text += textFunction(values[i]);
    }
    return text;
}

template<typename EnumT, typename TextFunction>
[[nodiscard]] Expected<EnumT> ParseOptionText(const AStringView value, const Span<const EnumT> values, TextFunction textFunction)noexcept{
    const AStringView trimmed = TrimView(value);
    for(usize i = 0u; i < values.size(); ++i){
        if(EqualsAsciiIgnoreCase(trimmed, textFunction(values[i])))
            return values[i];
    }
    return MakeUnexpected(Failure{});
}

template<typename TextFunction, typename ParseFunction>
b8 ValidateOptionText(TextFunction textFunction, ParseFunction parseValue, AString& inOutValue){
    const auto parsed = parseValue(AStringView(inOutValue));
    if(parsed){
        inOutValue.assign(textFunction(*parsed));
        return true;
    }

    inOutValue = NormalizeOptionText(Move(inOutValue));
    return false;
}

[[nodiscard]] constexpr AStringView OutputAssetTypeText(const OutputAssetType::Enum assetType)noexcept{
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

[[nodiscard]] constexpr AStringView NormalModeText(const NormalMode::Enum normalMode)noexcept{
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ScratchString OutputAssetTypeOptionsText(Core::Alloc::ScratchArena& scratchArena){
    return __hidden_common::BuildOptionTexts(Span<const OutputAssetType::Enum>(__hidden_common::s_OutputAssetTypeValues), __hidden_common::OutputAssetTypeText, scratchArena);
}

ScratchString OutputAssetTypeErrorText(Core::Alloc::ScratchArena& scratchArena){
    const ScratchString options = OutputAssetTypeOptionsText(scratchArena);
    ScratchString message("Asset type must be ", scratchArena);
    message.reserve(AddSize(message.size(), options.size()));
    message += options;
    return message;
}

Expected<OutputAssetType::Enum> ParseAssetTypeText(const AStringView value)noexcept{
    return __hidden_common::ParseOptionText(value, Span<const OutputAssetType::Enum>(__hidden_common::s_OutputAssetTypeValues), __hidden_common::OutputAssetTypeText);
}

b8 ValidateAssetTypeText(AString& inOutValue){
    return __hidden_common::ValidateOptionText(__hidden_common::OutputAssetTypeText, ParseAssetTypeText, inOutValue);
}

ScratchString NormalModeOptionsText(Core::Alloc::ScratchArena& scratchArena){
    return __hidden_common::BuildOptionTexts(Span<const NormalMode::Enum>(__hidden_common::s_NormalModeValues), __hidden_common::NormalModeText, scratchArena);
}

ScratchString NormalModeErrorText(Core::Alloc::ScratchArena& scratchArena){
    const ScratchString options = NormalModeOptionsText(scratchArena);
    ScratchString message("normal mode must be ", scratchArena);
    message.reserve(AddSize(message.size(), options.size()));
    message += options;
    return message;
}

Expected<NormalMode::Enum> ParseNormalModeText(const AStringView value)noexcept{
    return __hidden_common::ParseOptionText(value, Span<const NormalMode::Enum>(__hidden_common::s_NormalModeValues), __hidden_common::NormalModeText);
}

b8 ValidateNormalModeText(AString& inOutValue){
    return __hidden_common::ValidateOptionText(__hidden_common::NormalModeText, ParseNormalModeText, inOutValue);
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
    outputPath.replaceExtension(Core::Assets::s_NwbExtension);
    return outputPath;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

