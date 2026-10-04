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

static constexpr Core::Assets::NamedEnumCase<UiSkinDrawMode::Enum> s_DrawModeCases[] = {
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


[[nodiscard]] static bool ReadU32Value(const Path& path, const Value& value, const AStringView fieldName, u32& outValue){
    if(!value.isInteger() || !FitsU32(value.asInteger())){
        NWB_LOGGER_ERROR(GLOBAL_TEXT("{} '{}': field '{}' must contain integers in the u32 range")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(path)
            , StringConvert(fieldName)
        );
        return false;
    }
    outValue = static_cast<u32>(value.asInteger());
    return true;
}

template<usize Count>
[[nodiscard]] static bool ReadU32List(
    const Path& path,
    const Value& object,
    const AStringView fieldName,
    const bool required,
    u32 (&outValues)[Count]){
    const Value* field = FindField(object, fieldName);
    if(!field && !required)
        return true;
    if(!field || !field->isList() || field->asList().size() != Count){
        NWB_LOGGER_ERROR(GLOBAL_TEXT("{} '{}': field '{}' must be a {}-component integer list")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(path)
            , StringConvert(fieldName)
            , Count
        );
        return false;
    }
    for(usize index = 0u; index < Count; ++index){
        if(!ReadU32Value(path, field->asList()[index], fieldName, outValues[index]))
            return false;
    }
    return true;
}

template<usize Count>
[[nodiscard]] static bool ReadLogicalList(const Path& path, const Value& object, const AStringView fieldName, f32 (&outValues)[Count]){
    const Value* field = FindField(object, fieldName);
    if(!field)
        return true;
    if(!field->isList() || field->asList().size() != Count){
        NWB_LOGGER_ERROR(GLOBAL_TEXT("{} '{}': field '{}' must be a {}-component numeric list")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(path)
            , StringConvert(fieldName)
            , Count
        );
        return false;
    }
    for(usize index = 0u; index < Count; ++index){
        if(!Core::Assets::ReadMetadataFiniteF32Value(path, field->asList()[index], s_DiagnosticPrefix, fieldName, outValues[index]))
            return false;
    }
    return true;
}

[[nodiscard]] static bool ParseRegion(const Path& path, const Value& object, UiSkinRegion& outRegion){
    if(!Core::Assets::CheckMetadataAssetMap(path, object, s_DiagnosticPrefix))
        return false;
    if(FindField(object, s_DrawModeField)){
        if(!Core::Assets::ParseNamedMetadataEnumField(
            path,
            object,
            s_DiagnosticPrefix,
            s_DrawModeField,
            s_DrawModeCases,
            LengthOf(s_DrawModeCases),
            outRegion.drawMode,
            s_DrawModeHint
        ))
            return false;
    }

    if(!Core::Assets::ValidateMetadataAssetFields(
        path,
        object,
        s_DiagnosticPrefix,
        [hasSlice = outRegion.drawMode == UiSkinDrawMode::NineSlice](const AStringView field){
            return
                field == s_NameField || field == s_RectField || field == s_DrawModeField
                || field == s_PaddingField || field == s_MinimumSizeField || (hasSlice && field == s_SliceField)
            ;
        }
    ))
        return false;
    if(!Core::Assets::ReadMetadataNameField(path, object, s_DiagnosticPrefix, s_NameField, true, outRegion.name))
        return false;

    u32 rectangle[s_RectComponentCount] = {};
    u32 slice[s_SliceComponentCount] = {};
    f32 padding[s_PaddingComponentCount] = {};
    f32 minimumSize[s_MinimumSizeComponentCount] = {};
    if(
        !ReadU32List(path, object, s_RectField, true, rectangle)
        || !ReadU32List(path, object, s_SliceField, outRegion.drawMode == UiSkinDrawMode::NineSlice, slice)
        || !ReadLogicalList(path, object, s_PaddingField, padding)
        || !ReadLogicalList(path, object, s_MinimumSizeField, minimumSize)
    )
        return false;

    outRegion.rectangle = { rectangle[0u], rectangle[1u], rectangle[2u], rectangle[3u] };
    outRegion.sliceInsets = { slice[0u], slice[1u], slice[2u], slice[3u] };
    outRegion.padding = { padding[0u], padding[1u], padding[2u], padding[3u] };
    outRegion.minimumWidth = minimumSize[0u];
    outRegion.minimumHeight = minimumSize[1u];
    return true;
}

[[nodiscard]] static bool ParsePalette(const Path& path, const Value& asset, UiSkinPalette& outPalette){
    const Value* colors = Core::Assets::FindMetadataListField(path, asset, s_DiagnosticPrefix, s_ColorsField);
    if(!colors)
        return false;
    if(colors->asList().size() != UiSkinColorRole::Count){
        NWB_LOGGER_ERROR(GLOBAL_TEXT("{} '{}': colors must contain exactly {} named RGBA roles")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(path)
            , static_cast<u32>(UiSkinColorRole::Count)
        );
        return false;
    }

    Array<bool, UiSkinColorRole::Count> seen{};
    for(const Value& value : colors->asList()){
        if(!Core::Assets::CheckMetadataAssetMap(path, value, s_DiagnosticPrefix)
            || !Core::Assets::ValidateMetadataAssetFields(path, value, s_DiagnosticPrefix, { s_NameField, s_RgbaField }))
            return false;
        AStringView name;
        if(!Core::Assets::ReadMetadataStringField(path, value, s_DiagnosticPrefix, s_NameField, true, name))
            return false;
        usize role = UiSkinColorRole::Count;
        for(usize index = 0u; index < UiSkinColorRole::Count; ++index){
            if(name == s_ColorNames[index]){
                role = index;
                break;
            }
        }
        if(role == UiSkinColorRole::Count || seen[role]){
            NWB_LOGGER_ERROR(GLOBAL_TEXT("{} '{}': unknown or duplicate palette role '{}'")
                , StringConvert(s_DiagnosticPrefix)
                , PathToString<tchar>(path)
                , StringConvert(name)
            );
            return false;
        }
        const Value* rgba = FindField(value, s_RgbaField);
        if(!rgba || !rgba->isList() || rgba->asList().size() != s_RgbaComponentCount){
            NWB_LOGGER_ERROR(GLOBAL_TEXT("{} '{}': palette role '{}' needs four RGBA components")
                , StringConvert(s_DiagnosticPrefix)
                , PathToString<tchar>(path)
                , StringConvert(name)
            );
            return false;
        }
        f32 components[s_RgbaComponentCount] = {};
        for(usize index = 0u; index < s_RgbaComponentCount; ++index){
            if(!Core::Assets::ReadMetadataFiniteF32Value(path, rgba->asList()[index], s_DiagnosticPrefix, s_RgbaField, components[index]))
                return false;
            if(components[index] < s_PaletteChannelMin || components[index] > (index == s_AlphaComponentIndex ? s_PaletteAlphaMax : s_PaletteRgbMax)){
                NWB_LOGGER_ERROR(GLOBAL_TEXT("{} '{}': palette role '{}' needs RGB in [0, 16] and alpha in [0, 1]")
                    , StringConvert(s_DiagnosticPrefix)
                    , PathToString<tchar>(path)
                    , StringConvert(name)
                );
                return false;
            }
        }
        outPalette.colors[role] = { components[0u], components[1u], components[2u], components[3u] };
        seen[role] = true;
    }
    return true;
}

[[nodiscard]] static bool ParseTypography(const Path& path, const Value& asset, UiSkinTypography& outTypography){
    const Value* typography = FindField(asset, s_TypographyField);
    if(!typography){
        NWB_LOGGER_ERROR(GLOBAL_TEXT("{} '{}': skins require typography.default_font_size")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(path)
        );
        return false;
    }
    if(!Core::Assets::CheckMetadataAssetMap(path, *typography, s_DiagnosticPrefix)
        || !Core::Assets::ValidateMetadataAssetFields(path, *typography, s_DiagnosticPrefix, { s_DefaultFontSizeField }))
        return false;
    if(!Core::Assets::ReadMetadataFiniteF32Field(
        path, *typography, s_DiagnosticPrefix, s_DefaultFontSizeField, true, outTypography.defaultFontSize
    ))
        return false;
    if(outTypography.defaultFontSize < s_MinDefaultFontSize || outTypography.defaultFontSize > s_MaxDefaultFontSize){
        NWB_LOGGER_ERROR(GLOBAL_TEXT("{} '{}': typography.default_font_size must be in [1/64, 2048]")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(path)
        );
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ParseUiSkinCookMetadata(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    UiSkinCookEntry& outEntry,
    Core::Alloc::ScratchArena& scratchArena){
    using namespace __hidden_ui_skin_cook_metadata;

    UiSkinCookEntry parsed(outEntry.arena);
    const Value& asset = doc.asset();
    if(!Core::Assets::CheckMetadataAssetMap(nwbFilePath, asset, s_DiagnosticPrefix))
        return false;
    if(!Core::Assets::ValidateMetadataAssetFields(
        nwbFilePath,
        asset,
        s_DiagnosticPrefix,
        { s_TextureField, s_AtlasExtentField, s_ReferenceDensityField, s_ToolkitContractField, s_RegionsField, s_ColorsField, s_TypographyField }
    ))
        return false;

    if(!ParsePalette(nwbFilePath, asset, parsed.palette) || !ParseTypography(nwbFilePath, asset, parsed.typography))
        return false;
    if(!Core::Assets::BuildMetadataDerivedAssetVirtualPath(assetRoot, virtualRoot, nwbFilePath, parsed.virtualPath, scratchArena))
        return false;
    if(!Core::Assets::ReadMetadataAssetRefField(nwbFilePath, asset, s_DiagnosticPrefix, s_TextureField, true, parsed.texture))
        return false;

    u32 extent[s_AtlasExtentComponentCount] = {};
    if(!ReadU32List(nwbFilePath, asset, s_AtlasExtentField, true, extent))
        return false;
    parsed.atlasWidth = extent[0u];
    parsed.atlasHeight = extent[1u];
    if(!Core::Assets::ReadMetadataFiniteF32Field(nwbFilePath, asset, s_DiagnosticPrefix, s_ReferenceDensityField, true, parsed.referenceDensity))
        return false;

    AStringView toolkitContract;
    bool hasToolkitContract = false;
    if(!Core::Assets::ReadMetadataStringField(
        nwbFilePath, asset, s_DiagnosticPrefix, s_ToolkitContractField, false, toolkitContract, &hasToolkitContract
    ))
        return false;
    if(hasToolkitContract && toolkitContract != s_WidgetsContract){
        NWB_LOGGER_ERROR(GLOBAL_TEXT("{} '{}': toolkit_contract must be 'widgets'")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }
    parsed.completeToolkitSkin = hasToolkitContract;

    const Value* regions = Core::Assets::FindMetadataListField(nwbFilePath, asset, s_DiagnosticPrefix, s_RegionsField);
    if(!regions)
        return false;
    if(regions->asList().size() > s_UiSkinMaxRegionCount){
        NWB_LOGGER_ERROR(GLOBAL_TEXT("{} '{}': regions exceed schema limit {}")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
            , s_UiSkinMaxRegionCount
        );
        return false;
    }
    if(regions->asList().empty()){
        NWB_LOGGER_ERROR(GLOBAL_TEXT("{} '{}': regions must be nonempty and fit the supported count")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }
    parsed.regions.reserve(regions->asList().size());
    for(const Value& value : regions->asList()){
        UiSkinRegion region;
        if(!ParseRegion(nwbFilePath, value, region))
            return false;
        parsed.regions.push_back(region);
    }

    UiSkin validated(outEntry.arena);
    if(!BuildUiSkinAsset(parsed, validated))
        return false;

    outEntry.regions = Move(parsed.regions);
    outEntry.texture = parsed.texture;
    outEntry.virtualPath = parsed.virtualPath;
    outEntry.atlasWidth = parsed.atlasWidth;
    outEntry.atlasHeight = parsed.atlasHeight;
    outEntry.referenceDensity = parsed.referenceDensity;
    outEntry.completeToolkitSkin = parsed.completeToolkitSkin;
    outEntry.palette = parsed.palette;
    outEntry.typography = parsed.typography;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

