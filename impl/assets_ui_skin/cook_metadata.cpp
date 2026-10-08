// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"

#include <core/assets/paths.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_skin_cook_metadata{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Core::Metascript;

static constexpr AStringView s_DiagnosticPrefix = "UI skin meta";

static constexpr ::NamedEnumCase<UiSkinDrawMode::Enum> s_DrawModeCases[] = {
    { "sprite", UiSkinDrawMode::Sprite },
    { "nine_slice", UiSkinDrawMode::NineSlice },
};
static constexpr AStringView s_ColorNames[] = {
    "text.normal", "text.disabled", "text.tooltip", "edit.background", "edit.selection",
    "edit.inactive_selection", "edit.caret", "edit.preedit", "scrollbar.track", "scrollbar.thumb",
    "scrollbar.disabled", "popup.backdrop", "control.hover_tint", "control.pressed_tint",
    "control.disabled_tint", "progress.track_tint", "progress.fill_tint",
};
static_assert(LengthOf(s_ColorNames) == UiSkinColorRole::Count, "UI skin color names must match palette roles");

static constexpr AStringView s_NameField = "name";
static constexpr AStringView s_RectField = "rect";
static constexpr AStringView s_DrawModeField = "draw_mode";
static constexpr AStringView s_SliceField = "slice";
static constexpr AStringView s_PaddingField = "padding";
static constexpr AStringView s_MinimumSizeField = "minimum_size";
static constexpr AStringView s_ColorsField = "colors";
static constexpr AStringView s_RgbaField = "rgba";
static constexpr AStringView s_TypographyField = "typography";
static constexpr AStringView s_DefaultFontSizeField = "default_font_size";
static constexpr AStringView s_TextureField = "texture";
static constexpr AStringView s_AtlasExtentField = "atlas_extent";
static constexpr AStringView s_ReferenceDensityField = "reference_density";
static constexpr AStringView s_ToolkitContractField = "toolkit_contract";
static constexpr AStringView s_RegionsField = "regions";
static constexpr AStringView s_WidgetsContract = "widgets";
static constexpr AStringView s_DrawModeHint = "must be 'sprite' or 'nine_slice'";

static constexpr usize s_RectComponentCount = 4u;
static constexpr usize s_SliceComponentCount = 4u;
static constexpr usize s_PaddingComponentCount = 4u;
static constexpr usize s_MinimumSizeComponentCount = 2u;
static constexpr usize s_AtlasExtentComponentCount = 2u;
static constexpr usize s_RgbaComponentCount = 4u;
static constexpr usize s_AlphaComponentIndex = 3u;
static constexpr f32 s_PaletteChannelMin = 0.0f;
static constexpr f32 s_PaletteRgbMax = 16.0f;
static constexpr f32 s_PaletteAlphaMax = 1.0f;
static constexpr f32 s_MinDefaultFontSize = 1.0f / 64.0f;
static constexpr f32 s_MaxDefaultFontSize = 2048.0f;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Expected<u32> ReadU32Value(const Path& path, const Value& value, const AStringView fieldName){
    if(!value.isInteger() || !FitsU32(value.asInteger())){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' must contain integers in the u32 range")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(path)
            , StringConvert(fieldName)
        );
        return MakeUnexpected(Failure{});
    }
    return static_cast<u32>(value.asInteger());
}

template<usize Count>
[[nodiscard]] static Expected<Array<u32, Count>> ReadU32List(
    const Path& path,
    const Value& object,
    const AStringView fieldName,
    const bool required
){
    Array<u32, Count> values{};

    const Value* field = FindField(object, fieldName);
    if(!field && !required)
        return values;
    if(!field || !field->isList() || field->asList().size() != Count){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' must be a {}-component integer list")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(path)
            , StringConvert(fieldName)
            , Count
        );
        return MakeUnexpected(Failure{});
    }
    for(usize index = 0u; index < Count; ++index){
        auto valueResult = ReadU32Value(path, field->asList()[index], fieldName);
        if(!valueResult)
            return MakeUnexpected(Failure{});
        values[index] = *valueResult;
    }
    return values;
}

template<usize Count>
[[nodiscard]] static Expected<Array<f32, Count>> ReadLogicalList(const Path& path, const Value& object, const AStringView fieldName){
    Array<f32, Count> values{};

    const Value* field = FindField(object, fieldName);
    if(!field)
        return values;
    if(!field->isList() || field->asList().size() != Count){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' must be a {}-component numeric list")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(path)
            , StringConvert(fieldName)
            , Count
        );
        return MakeUnexpected(Failure{});
    }
    for(usize index = 0u; index < Count; ++index){
        auto indexResult = Core::Assets::ReadMetadataFiniteF32Value(path, field->asList()[index], s_DiagnosticPrefix, fieldName);
        if(!indexResult)
            return MakeUnexpected(Failure{});
        values[index] = *indexResult;
    }
    return values;
}

[[nodiscard]] static Expected<UiSkinRegion> ParseRegion(const Path& path, const Value& object){
    UiSkinRegion region{};

    if(!Core::Assets::CheckMetadataAssetMap(path, object, s_DiagnosticPrefix))
        return MakeUnexpected(Failure{});
    if(FindField(object, s_DrawModeField)){
        auto drawModeResult = Core::Assets::ParseNamedMetadataEnumField(path, object, s_DiagnosticPrefix, s_DrawModeField, s_DrawModeCases, LengthOf(s_DrawModeCases), s_DrawModeHint);
        if(!drawModeResult)
            return MakeUnexpected(Failure{});
        region.drawMode = *drawModeResult;
    }

    if(!Core::Assets::ValidateMetadataAssetFields(
        path,
        object,
        s_DiagnosticPrefix,
        [hasSlice = region.drawMode == UiSkinDrawMode::NineSlice](const AStringView field)noexcept{
            return
                field == s_NameField || field == s_RectField || field == s_DrawModeField
                || field == s_PaddingField || field == s_MinimumSizeField || (hasSlice && field == s_SliceField)
            ;
        }
    ))
        return MakeUnexpected(Failure{});
    auto nameResult = Core::Assets::ReadMetadataNameField(path, object, s_DiagnosticPrefix, s_NameField, true);
    if(!nameResult)
        return MakeUnexpected(Failure{});
    region.name = *nameResult;

    auto rectangleResult = ReadU32List<s_RectComponentCount>(path, object, s_RectField, true);
    if(!rectangleResult)
        return MakeUnexpected(Failure{});
    auto sliceResult = ReadU32List<s_SliceComponentCount>(path, object, s_SliceField, region.drawMode == UiSkinDrawMode::NineSlice);
    if(!sliceResult)
        return MakeUnexpected(Failure{});
    auto paddingResult = ReadLogicalList<s_PaddingComponentCount>(path, object, s_PaddingField);
    if(!paddingResult)
        return MakeUnexpected(Failure{});
    auto minimumSizeResult = ReadLogicalList<s_MinimumSizeComponentCount>(path, object, s_MinimumSizeField);
    if(!minimumSizeResult)
        return MakeUnexpected(Failure{});

    region.rectangle = { (*rectangleResult)[0u], (*rectangleResult)[1u], (*rectangleResult)[2u], (*rectangleResult)[3u] };
    region.sliceInsets = { (*sliceResult)[0u], (*sliceResult)[1u], (*sliceResult)[2u], (*sliceResult)[3u] };
    region.padding = { (*paddingResult)[0u], (*paddingResult)[1u], (*paddingResult)[2u], (*paddingResult)[3u] };
    region.minimumWidth = (*minimumSizeResult)[0u];
    region.minimumHeight = (*minimumSizeResult)[1u];
    return region;
}

[[nodiscard]] static Expected<UiSkinPalette> ParsePalette(const Path& path, const Value& asset){
    UiSkinPalette palette{};

    const Value* colors = Core::Assets::FindMetadataListField(path, asset, s_DiagnosticPrefix, s_ColorsField);
    if(!colors)
        return MakeUnexpected(Failure{});
    if(colors->asList().size() != UiSkinColorRole::Count){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': colors must contain exactly {} named RGBA roles")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(path)
            , static_cast<u32>(UiSkinColorRole::Count)
        );
        return MakeUnexpected(Failure{});
    }

    Array<bool, UiSkinColorRole::Count> seen{};
    for(const Value& value : colors->asList()){
        if(!Core::Assets::CheckMetadataAssetMap(path, value, s_DiagnosticPrefix)
            || !Core::Assets::ValidateMetadataAssetFields(path, value, s_DiagnosticPrefix, { s_NameField, s_RgbaField }))
            return MakeUnexpected(Failure{});
        AStringView name;
        auto nameResultValue = Core::Assets::ReadMetadataStringField(path, value, s_DiagnosticPrefix, s_NameField, true);
        if(!nameResultValue)
            return MakeUnexpected(Failure{});
        name = nameResultValue->text;
        usize role = UiSkinColorRole::Count;
        for(usize index = 0u; index < UiSkinColorRole::Count; ++index){
            if(name == s_ColorNames[index]){
                role = index;
                break;
            }
        }
        if(role == UiSkinColorRole::Count || seen[role]){
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': unknown or duplicate palette role '{}'")
                , StringConvert(s_DiagnosticPrefix)
                , PathToString<tchar>(path)
                , StringConvert(name)
            );
            return MakeUnexpected(Failure{});
        }
        const Value* rgba = FindField(value, s_RgbaField);
        if(!rgba || !rgba->isList() || rgba->asList().size() != s_RgbaComponentCount){
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': palette role '{}' needs four RGBA components")
                , StringConvert(s_DiagnosticPrefix)
                , PathToString<tchar>(path)
                , StringConvert(name)
            );
            return MakeUnexpected(Failure{});
        }
        f32 components[s_RgbaComponentCount] = {};
        for(usize index = 0u; index < s_RgbaComponentCount; ++index){
            auto indexResultValue = Core::Assets::ReadMetadataFiniteF32Value(path, rgba->asList()[index], s_DiagnosticPrefix, s_RgbaField);
            if(!indexResultValue)
                return MakeUnexpected(Failure{});
            components[index] = *indexResultValue;
            if(components[index] < s_PaletteChannelMin || components[index] > (index == s_AlphaComponentIndex ? s_PaletteAlphaMax : s_PaletteRgbMax)){
                NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': palette role '{}' needs RGB in [0, 16] and alpha in [0, 1]")
                    , StringConvert(s_DiagnosticPrefix)
                    , PathToString<tchar>(path)
                    , StringConvert(name)
                );
                return MakeUnexpected(Failure{});
            }
        }
        palette.colors[role] = { components[0u], components[1u], components[2u], components[3u] };
        seen[role] = true;
    }
    return palette;
}

[[nodiscard]] static Expected<UiSkinTypography> ParseTypography(const Path& path, const Value& asset){
    UiSkinTypography typographyResult{};

    const Value* typography = FindField(asset, s_TypographyField);
    if(!typography){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': skins require typography.default_font_size")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(path)
        );
        return MakeUnexpected(Failure{});
    }
    if(!Core::Assets::CheckMetadataAssetMap(path, *typography, s_DiagnosticPrefix)
        || !Core::Assets::ValidateMetadataAssetFields(path, *typography, s_DiagnosticPrefix, { s_DefaultFontSizeField }))
        return MakeUnexpected(Failure{});
    auto defaultFontSizeResult = Core::Assets::ReadMetadataFiniteF32Field(path, *typography, s_DiagnosticPrefix, s_DefaultFontSizeField, true);
    if(!defaultFontSizeResult)
        return MakeUnexpected(Failure{});
    typographyResult.defaultFontSize = *defaultFontSizeResult;
    if(typographyResult.defaultFontSize < s_MinDefaultFontSize || typographyResult.defaultFontSize > s_MaxDefaultFontSize){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': typography.default_font_size must be in [1/64, 2048]")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(path)
        );
        return MakeUnexpected(Failure{});
    }
    return typographyResult;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<UiSkinCookEntry> ParseUiSkinCookMetadata(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena& scratchArena
){
    using namespace __hidden_ui_skin_cook_metadata;

    UiSkinCookEntry parsed(arena);
    const Value& asset = doc.asset();
    if(!Core::Assets::CheckMetadataAssetMap(nwbFilePath, asset, s_DiagnosticPrefix))
        return MakeUnexpected(Failure{});
    if(!Core::Assets::ValidateMetadataAssetFields(
        nwbFilePath,
        asset,
        s_DiagnosticPrefix,
        { s_TextureField, s_AtlasExtentField, s_ReferenceDensityField, s_ToolkitContractField, s_RegionsField, s_ColorsField, s_TypographyField }
    ))
        return MakeUnexpected(Failure{});

    auto paletteResult = ParsePalette(nwbFilePath, asset);
    if(!paletteResult)
        return MakeUnexpected(Failure{});
    parsed.palette = *paletteResult;
    auto typographyResult = ParseTypography(nwbFilePath, asset);
    if(!typographyResult)
        return MakeUnexpected(Failure{});
    parsed.typography = *typographyResult;
    auto virtualPathResult = Core::Assets::BuildMetadataDerivedAssetVirtualPath(assetRoot, virtualRoot, nwbFilePath, scratchArena);
    if(!virtualPathResult)
        return MakeUnexpected(Failure{});
    parsed.virtualPath = *virtualPathResult;
    auto textureResult = Core::Assets::ReadMetadataAssetRefField<Texture>(nwbFilePath, asset, s_DiagnosticPrefix, s_TextureField, true);
    if(!textureResult)
        return MakeUnexpected(Failure{});
    parsed.texture = *textureResult;

    auto extentResult = ReadU32List<s_AtlasExtentComponentCount>(nwbFilePath, asset, s_AtlasExtentField, true);
    if(!extentResult)
        return MakeUnexpected(Failure{});
    parsed.atlasWidth = (*extentResult)[0u];
    parsed.atlasHeight = (*extentResult)[1u];
    auto referenceDensityResult = Core::Assets::ReadMetadataFiniteF32Field(nwbFilePath, asset, s_DiagnosticPrefix, s_ReferenceDensityField, true);
    if(!referenceDensityResult)
        return MakeUnexpected(Failure{});
    parsed.referenceDensity = *referenceDensityResult;

    AStringView toolkitContract;
    bool hasToolkitContract = false;
    auto toolkitContractResult = Core::Assets::ReadMetadataStringField(nwbFilePath, asset, s_DiagnosticPrefix, s_ToolkitContractField, false);
    if(!toolkitContractResult)
        return MakeUnexpected(Failure{});
    toolkitContract = toolkitContractResult->text;
    hasToolkitContract = toolkitContractResult->present;
    if(hasToolkitContract && toolkitContract != s_WidgetsContract){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': toolkit_contract must be 'widgets'")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
        );
        return MakeUnexpected(Failure{});
    }
    parsed.completeToolkitSkin = hasToolkitContract;

    const Value* regions = Core::Assets::FindMetadataListField(nwbFilePath, asset, s_DiagnosticPrefix, s_RegionsField);
    if(!regions)
        return MakeUnexpected(Failure{});
    if(regions->asList().size() > s_UiSkinMaxRegionCount){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': regions exceed schema limit {}")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
            , s_UiSkinMaxRegionCount
        );
        return MakeUnexpected(Failure{});
    }
    if(regions->asList().empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': regions must be nonempty and fit the supported count")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
        );
        return MakeUnexpected(Failure{});
    }
    parsed.regions.reserve(regions->asList().size());
    for(const Value& value : regions->asList()){
        auto regionResult = ParseRegion(nwbFilePath, value);
        if(!regionResult)
            return MakeUnexpected(Failure{});
        parsed.regions.push_back(*regionResult);
    }

    if(!BuildUiSkinAsset(parsed, arena))
        return MakeUnexpected(Failure{});
    return parsed;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

